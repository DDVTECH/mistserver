#include "output_webrtc.h"
#include <ifaddrs.h> // ifaddr, listing ip addresses.
#include <mist/procs.h>
#include <mist/sdp.h>
#include <mist/timing.h>
#include <mist/url.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <netdb.h> // ifaddr, listing ip addresses.
#include <mist/stream.h>
#include <stdarg.h>
/*
This file handles both input and output, and can operate in WHIP/WHEP as well as WebSocket signaling mode.
In case of WHIP/WHEP: the Socket is closed after signaling has happened and the keepGoing function
  is overridden to handle this case
When handling WebRTC Input a second thread is started dedicated to UDP traffic. In this case
  no UDP traffic may be handled on the main thread whatsoever
*/

namespace Mist{

  bool doDTLS = true;
  bool volkswagenMode = false;

  OutWebRTC *classPointer = 0;

  static void webRTCInputOutputThreadFunc(void *arg){
    classPointer->handleWebRTCInputOutputFromThread();
  }

  static void onRTPSorterHasPacketCallback(const uint64_t track, const RTP::Packet &p){
    classPointer->onRTPSorterHasPacket(track, p);
  }

  static void onDTSCConverterHasInitDataCallback(const uint64_t track, const std::string &initData){
    classPointer->onDTSCConverterHasInitData(track, initData);
  }

  static void onDTSCConverterHasPacketCallback(const DTSC::Packet &pkt){
    classPointer->onDTSCConverterHasPacket(pkt);
  }

  static void onRTPPacketizerHasDataCallback(void *socket, const char *data, size_t len, uint8_t channel){
    classPointer->onRTPPacketizerHasRTPPacket(data, len);
  }

  static void onRTPPacketizerHasRTCPDataCallback(void *socket, const char *data, size_t len, uint8_t){
    classPointer->onRTPPacketizerHasRTCPPacket(data, len);
  }


#ifdef WITH_DATACHANNELS
  static int sctp_recv_cb(struct socket *s, union sctp_sockstore addr, void *data, size_t datalen, struct sctp_rcvinfo rcv, int flags, void *ulp_info){
    if (data) {
      if (!(flags & MSG_NOTIFICATION)){
        ((OutWebRTC*)(addr.sconn.sconn_addr))->onSCTP((const char*)data, datalen, ntohs(rcv.rcv_sid), ntohl(rcv.rcv_ppid));
      }
      free(data);
    }else{
      usrsctp_close(s);
    }
    return 1;
  }

  int sctp_send_cb(void *addr, void *buf, size_t length, uint8_t tos, uint8_t set_df){
    ((OutWebRTC*)addr)->sendSCTPPacket((const char*)buf, length);
    return 0;
  }
  
  void sctp_debug_cb(const char * format, ...){
    char msg[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, 1024, format, args);
    va_end(args);
    INFO_MSG("sctp: %s", msg);
  }
#endif

  WebRTCSocket::WebRTCSocket(){
    udpSock = 0;
    if (volkswagenMode){
      srtpWriter.init("SRTP_AES128_CM_SHA1_80", "volkswagen modus", "volkswagenmode");
    }
  }

  size_t WebRTCSocket::sendRTCP(const char * data, size_t len){
    if (doDTLS){
      dataBuffer.allocate(len + 256);
      dataBuffer.assign(data, len);
      int rtcpPacketSize = len;
      if (srtpWriter.protectRtcp((uint8_t *)(void *)dataBuffer, &rtcpPacketSize) != 0){
        ERROR_MSG("Failed to protect the RTCP message.");
        return 0;
      }
      udpSock->sendPaced(dataBuffer, rtcpPacketSize, false);
      return rtcpPacketSize;
    }
    
    udpSock->sendPaced(data, len, false);

    if (volkswagenMode){
      dataBuffer.allocate(len + 256);
      dataBuffer.assign(data, len);
      int rtcpPacketSize = len;
      srtpWriter.protectRtcp((uint8_t *)(void *)dataBuffer, &rtcpPacketSize);
    }
    return len;
  }

  size_t WebRTCSocket:: ackNACK(uint32_t pSSRC, uint16_t seq){
    if (!outBuffers.count(pSSRC)){
      WARN_MSG("Could not answer NACK for %" PRIu32 ": we don't know this track", pSSRC);
      return 0;
    }
    nackBuffer &nb = outBuffers[pSSRC];
    if (!nb.isBuffered(seq)){
      HIGH_MSG("Could not answer NACK for %" PRIu32 " #%" PRIu16 ": packet not buffered", pSSRC, seq);
      return 0;
    }
    udpSock->sendPaced(nb.getData(seq), nb.getSize(seq), false);
    return nb.getSize(seq);
  }

  /* ------------------------------------------------ */

  WebRTCTrack::WebRTCTrack(){
    payloadType = 0;
    SSRC = 0;
    ULPFECPayloadType = 0;
    REDPayloadType = 0;
    RTXPayloadType = 0;
    lastTransit = 0;
    jitter = 0;
    lastPktCount = 0;
  }

  void WebRTCTrack::gotPacket(uint32_t ts){
    uint32_t arrival = Util::bootMS() * rtpToDTSC.multiplier;
    int transit = arrival - ts;
    int d = transit - lastTransit;
    lastTransit = transit;
    if (d < 0) d = -d;
    jitter += (1. / 16.) * ((double)d - jitter);
  }

  /* ------------------------------------------------ */

  OutWebRTC::OutWebRTC(Socket::Connection &myConn) : HTTPOutput(myConn){
#ifdef WITH_DATACHANNELS
    sctpInited = false;
    sctpConnected = false;
#endif
    noSignalling = false;
    totalPkts = 0;
    totalLoss = 0;
    totalRetrans = 0;
    setPacketOffset = false;
    packetOffset = 0;
    lastRecv = Util::bootMS();
    stats_jitter = 0;
    stats_nacknum = 0;
    stats_lossnum = 0;
    stats_lossperc = 100.0;
    lastPackMs = 0;
    vidTrack = INVALID_TRACK_ID;
    prevVidTrack = INVALID_TRACK_ID;
    audTrack = INVALID_TRACK_ID;
    metaTrack = INVALID_TRACK_ID;
    firstKey = true;
    repeatInit = true;
    wsCmds = true;

    lastTimeSync = 0;
    maxSkipAhead = 0;
    needsLookAhead = 0;
    ioThread = 0;
    udpPort = 0;
    Util::getRandomBytes(&SSRC, sizeof(SSRC));
    rtcpTimeoutInMillis = 0;
    rtcpKeyFrameDelayInMillis = 2000;
    rtcpKeyFrameTimeoutInMillis = 0;
    videoBitrate = 6 * 1000 * 1000;
    videoConstraint = videoBitrate;
    RTP::MAX_SEND = 1350 - 28;
    didReceiveKeyFrame = false;
    syncedNTPClock = false;
    lastMediaSocket = 0;
    lastMetaSocket = 0;

   
    JSON::Value & certOpt = config->getOption("cert", true);
    JSON::Value & keyOpt = config->getOption("key", true);

    //Attempt to read certificate config from other connectors
    if (certOpt.size() < 2 || keyOpt.size() < 2){
      Util::DTSCShmReader rProto(SHM_PROTO);
      DTSC::Scan prtcls = rProto.getScan();
      unsigned int pro_cnt = prtcls.getSize();
      for (unsigned int i = 0; i < pro_cnt; ++i){
        if (prtcls.getIndice(i).hasMember("key") && prtcls.getIndice(i).hasMember("cert")){
          std::string conn = prtcls.getIndice(i).getMember("connector").asString();
          INFO_MSG("No cert/key configured for WebRTC explicitly, reading from %s connector config", conn.c_str());
          JSON::Value newCert = prtcls.getIndice(i).getMember("cert").asJSON();
          certOpt.shrink(0);
          jsonForEach(newCert, k){certOpt.append(*k);}
          keyOpt.shrink(0);
          keyOpt.append(prtcls.getIndice(i).getMember("key").asJSON());
          break;
        }
      }
    }

    if (certOpt.size() < 2 || keyOpt.size() < 2){
      if (cert.init("NL", "webrtc", "webrtc") != 0){
        onFail("Failed to create the certificate.", true);
        return;
      }
    }else{
      // Read certificate chain(s)
      jsonForEach(certOpt, it){
        if (!cert.loadCert(it->asStringRef())){
          WARN_MSG("Could not load any certificates from file: %s", it->asStringRef().c_str());
        }
      }

      // Read key
      if (!cert.loadKey(config->getString("key"))){
        FAIL_MSG("Could not load any keys from file: %s", config->getString("key").c_str());
        return;
      }
    }

    sdpAnswer.setFingerprint(cert.getFingerprintSha256());

    classPointer = this;

    setBlocking(false);
  }

  OutWebRTC::~OutWebRTC(){
    if (ioThread && ioThread->joinable()){
      ioThread->join();
      delete ioThread;
      ioThread = 0;
    }
  }

  // Initialize the WebRTC output. This is where we define what
  // codes types are supported and accepted when parsing the SDP
  // offer or when generating the SDP. The `capa` member is
  // inherited from `Output`.
  void OutWebRTC::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "WebRTC";
    capa["friendly"] = "WebRTC";
    capa["desc"] = "Provides WebRTC output";
    capa["url_rel"] = "/webrtc/$";
    capa["url_match"] = "/webrtc/$";

    capa["incoming_push_url"] = "http://$host:$port/webrtc/$stream";

    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("VP8");
    capa["codecs"][0u][0u].append("VP9");
    capa["codecs"][0u][1u].append("opus");
    capa["codecs"][0u][1u].append("ALAW");
    capa["codecs"][0u][1u].append("ULAW");
    capa["codecs"][0u][2u].append("JSON");
    capa["codecs"][0u][2u].append("subtitle");
    capa["methods"][0u]["handler"] = "ws";
    capa["methods"][0u]["type"] = "webrtc";
    capa["methods"][0u]["hrn"] = "WebRTC with WebSocket signalling";
    capa["methods"][0u]["priority"] = 2;
    capa["methods"][0u]["nobframes"] = 1;

    capa["methods"][1u]["handler"] = "http";
    capa["methods"][1u]["type"] = "whep";
    capa["methods"][1u]["hrn"] = "WebRTC with WHEP signalling";
    capa["methods"][1u]["priority"] = 5;
    capa["methods"][1u]["nobframes"] = 1;

    capa["optional"]["preferredvideocodec"]["name"] = "Preferred video codecs";
    capa["optional"]["preferredvideocodec"]["help"] =
        "Comma separated list of video codecs you want to support in preferred order. e.g. "
        "H264,VP8";
    capa["optional"]["preferredvideocodec"]["default"] = "H264,VP9,VP8";
    capa["optional"]["preferredvideocodec"]["type"] = "str";
    capa["optional"]["preferredvideocodec"]["option"] = "--webrtc-video-codecs";
    capa["optional"]["preferredvideocodec"]["short"] = "V";

    capa["optional"]["preferredaudiocodec"]["name"] = "Preferred audio codecs";
    capa["optional"]["preferredaudiocodec"]["help"] =
        "Comma separated list of audio codecs you want to support in preferred order. e.g. "
        "opus,ALAW,ULAW";
    capa["optional"]["preferredaudiocodec"]["default"] = "opus,ALAW,ULAW";
    capa["optional"]["preferredaudiocodec"]["type"] = "str";
    capa["optional"]["preferredaudiocodec"]["option"] = "--webrtc-audio-codecs";
    capa["optional"]["preferredaudiocodec"]["short"] = "A";

    capa["optional"]["bindhost"]["name"] = "UDP bind address (internal)";
    capa["optional"]["bindhost"]["help"] = "Interface address or hostname to bind SRTP UDP socket "
                                           "to. Defaults to originating interface address.";
    capa["optional"]["bindhost"]["default"] = "";
    capa["optional"]["bindhost"]["type"] = "str";
    capa["optional"]["bindhost"]["option"] = "--bindhost";
    capa["optional"]["bindhost"]["short"] = "B";

    capa["optional"]["pubhost"]["name"] = "UDP bind address (public)";
    capa["optional"]["pubhost"]["help"] = "Interface address or hostname for clients to connect to. Defaults to internal address.";
    capa["optional"]["pubhost"]["default"] = "";
    capa["optional"]["pubhost"]["type"] = "str";
    capa["optional"]["pubhost"]["option"] = "--pubhost";
    capa["optional"]["pubhost"]["short"] = "H";

    capa["optional"]["mergesessions"]["name"] = "merge sessions";
    capa["optional"]["mergesessions"]["help"] =
        "if enabled, merges together all views from a single user into a single combined session. "
        "if disabled, each view (reconnection of the signalling websocket) is a separate session.";
    capa["optional"]["mergesessions"]["option"] = "--mergesessions";
    capa["optional"]["mergesessions"]["short"] = "m";
    capa["optional"]["mergesessions"]["default"] = 0;

    capa["optional"]["nackdisable"]["name"] = "Disallow NACKs for viewers";
    capa["optional"]["nackdisable"]["help"] = "Disallows viewers to send NACKs for lost packets";
    capa["optional"]["nackdisable"]["option"] = "--nackdisable";
    capa["optional"]["nackdisable"]["short"] = "n";
    capa["optional"]["nackdisable"]["default"] = 0;

    capa["optional"]["jitterlog"]["name"] = "Write jitter log";
    capa["optional"]["jitterlog"]["help"] = "Writes log of frame transmit jitter to /tmp/ for each outgoing connection";
    capa["optional"]["jitterlog"]["option"] = "--jitterlog";
    capa["optional"]["jitterlog"]["short"] = "J";
    capa["optional"]["jitterlog"]["default"] = 0;

    capa["optional"]["packetlog"]["name"] = "Write packet log";
    capa["optional"]["packetlog"]["help"] = "Writes log of full packet contents to /tmp/ for each connection";
    capa["optional"]["packetlog"]["option"] = "--packetlog";
    capa["optional"]["packetlog"]["short"] = "P";
    capa["optional"]["packetlog"]["default"] = 0;

    capa["optional"]["nacktimeout"]["name"] = "RTP NACK timeout";
    capa["optional"]["nacktimeout"]["help"] = "Amount of packets any track will wait for a packet to arrive before NACKing it";
    capa["optional"]["nacktimeout"]["option"] = "--nacktimeout";
    capa["optional"]["nacktimeout"]["short"] = "x";
    capa["optional"]["nacktimeout"]["type"] = "uint";
    capa["optional"]["nacktimeout"]["default"] = 5;

    capa["optional"]["losttimeout"]["name"] = "RTP lost timeout";
    capa["optional"]["losttimeout"]["help"] = "Amount of packets any track will wait for a packet to arrive before considering it lost";
    capa["optional"]["losttimeout"]["option"] = "--losttimeout";
    capa["optional"]["losttimeout"]["short"] = "l";
    capa["optional"]["losttimeout"]["type"] = "uint";
    capa["optional"]["losttimeout"]["default"] = 30;

    capa["optional"]["nacktimeoutmobile"]["name"] = "RTP NACK timeout (mobile)";
    capa["optional"]["nacktimeoutmobile"]["help"] = "Amount of packets any track will wait for a packet to arrive before NACKing it, on mobile connections";
    capa["optional"]["nacktimeoutmobile"]["option"] = "--nacktimeoutmobile";
    capa["optional"]["nacktimeoutmobile"]["short"] = "X";
    capa["optional"]["nacktimeoutmobile"]["type"] = "uint";
    capa["optional"]["nacktimeoutmobile"]["default"] = 15;

    capa["optional"]["losttimeoutmobile"]["name"] = "RTP lost timeout (mobile)";
    capa["optional"]["losttimeoutmobile"]["help"] = "Amount of packets any track will wait for a packet to arrive before considering it lost, on mobile connections";
    capa["optional"]["losttimeoutmobile"]["option"] = "--losttimeoutmobile";
    capa["optional"]["losttimeoutmobile"]["short"] = "L";
    capa["optional"]["losttimeoutmobile"]["type"] = "uint";
    capa["optional"]["losttimeoutmobile"]["default"] = 90;

    capa["optional"]["cert"]["name"] = "Certificate";
    capa["optional"]["cert"]["help"] = "(Root) certificate(s) file(s) to append to chain";
    capa["optional"]["cert"]["option"] = "--cert";
    capa["optional"]["cert"]["short"] = "C";
    capa["optional"]["cert"]["default"] = "";
    capa["optional"]["cert"]["type"] = "str";
    capa["optional"]["key"]["name"] = "Key";
    capa["optional"]["key"]["help"] = "Private key for SSL";
    capa["optional"]["key"]["option"] = "--key";
    capa["optional"]["key"]["short"] = "K";
    capa["optional"]["key"]["default"] = "";
    capa["optional"]["key"]["type"] = "str";

    capa["optional"]["iceservers"]["name"] = "STUN/TURN config";
    capa["optional"]["iceservers"]["help"] = "An array of RTCIceServer objects, each describing one server which may be used by the ICE agent; these are typically STUN and/or TURN servers. These will be passed verbatim to the RTCPeerConnection constructor as the 'iceServers' property.";
    capa["optional"]["iceservers"]["option"] = "--iceservers";
    capa["optional"]["iceservers"]["short"] = "z";
    capa["optional"]["iceservers"]["default"] = "";
    capa["optional"]["iceservers"]["type"] = "json";


    config->addOptionsFromCapabilities(capa);
  }

  void OutWebRTC::onFail(const std::string &msg, bool critical){
    if (!webSock){return HTTPOutput::onFail(msg, critical);}
    sendSignalingError("error", msg);
    Output::onFail(msg, critical);
  }

  void OutWebRTC::preWebsocketConnect(){
    HTTP::URL tmpUrl("http://" + H.GetHeader("Host"));
    externalAddr = tmpUrl.host;
    if (UA.find("Mobi") != std::string::npos){
      RTP::PACKET_REORDER_WAIT = config->getInteger("nacktimeoutmobile");
      RTP::PACKET_DROP_TIMEOUT = config->getInteger("losttimeoutmobile");
      INFO_MSG("Using mobile RTP configuration: NACK at %u, drop at %u", RTP::PACKET_REORDER_WAIT, RTP::PACKET_DROP_TIMEOUT);
    }else{
      RTP::PACKET_REORDER_WAIT = config->getInteger("nacktimeout");
      RTP::PACKET_DROP_TIMEOUT = config->getInteger("losttimeout");
      INFO_MSG("Using regular RTP configuration: NACK at %u, drop at %u", RTP::PACKET_REORDER_WAIT, RTP::PACKET_DROP_TIMEOUT);
    }
  }

  void OutWebRTC::requestHandler(){
    if (noSignalling){
      // For WHEP, make sure we keep listening for packets while waiting for new data to come in for sending
      if (parseData && !handleWebRTCInputOutput()){
        sendPaced(10);
      }else{
        if (ioThread){Util::sleep(500);}
      }
      // After 10s of no packets, abort
      if (thisBootMs > lastRecv + 10000){
        Util::logExitReason(ER_CLEAN_INACTIVE, "received no data for 10+ seconds");
        config->is_active = false;
      }
      return;
    }
    HTTPOutput::requestHandler();
  }

  void OutWebRTC::respondHTTP(const HTTP::Parser & req, bool headersOnly){
    // Generic header/parameter handling
    HTTPOutput::respondHTTP(req, headersOnly);
    INFO_MSG("HTTP: %s", req.method.c_str());
    // Check for WHIP payload
    if (headersOnly){
      H.setCORSHeaders();
      H.StartResponse("200", "All good", req, myConn);
      H.Chunkify(0, 0, myConn);
      return;
    }
    if (req.method == "POST"){
      INFO_MSG("POST");
      if (req.GetHeader("Content-Type") == "application/sdp"){
        SDP::Session sdpParser;
        const std::string &offerStr = req.body;
        if (config && config->hasOption("packetlog") && config->getBool("packetlog")){
          std::string fileName = "/tmp/wrtcpackets_"+JSON::Value(getpid()).asString();
          packetLog.open(fileName.c_str());
        }
        if (packetLog.is_open()){
          packetLog << "[" << Util::bootMS() << "]" << offerStr << std::endl << std::endl;
        }
        if (!sdpParser.parseSDP(offerStr) || !sdpAnswer.parseOffer(offerStr)){
          H.setCORSHeaders();
          H.StartResponse("400", "Could not parse", req, myConn);
          H.Chunkify("Failed to parse offer SDP", myConn);
          H.Chunkify(0, 0, myConn);
          return;
        }

        bool ret = false;
        if (sdpParser.hasSendOnlyMedia()){
          ret = handleSignalingCommandRemoteOfferForInput(sdpParser);
        }else{
          ret = handleSignalingCommandRemoteOfferForOutput(sdpParser);
        }
        if (ret){
          noSignalling = true;
          H.SetHeader("Content-Type", "application/sdp");
          H.SetHeader("Location", streamName + "/" + JSON::Value(getpid()).asString());
          if (config->getString("iceservers").size()){
            std::deque<std::string> links;
            JSON::Value iceConf = JSON::fromString(config->getString("iceservers"));
            jsonForEach(iceConf, i){
              if (i->isMember("url") && (*i)["url"].isString()){
                JSON::Value &u = (*i)["url"];
                std::string str = u.asString()+"; rel=\"ice-server\";";
                if (i->isMember("username")){
                  str += " username=" + (*i)["username"].toString() + ";";
                }
                if (i->isMember("credential")){
                  str += " credential=" + (*i)["credential"].toString() + ";";
                }
                if (i->isMember("credentialType")){
                  str += " credential-type=" + (*i)["credentialType"].toString() + ";";
                }
                links.push_back(str);
              }
              if (i->isMember("urls") && (*i)["urls"].isString()){
                JSON::Value &u = (*i)["urls"];
                std::string str = u.asString()+"; rel=\"ice-server\";";
                if (i->isMember("username")){
                  str += " username=" + (*i)["username"].toString() + ";";
                }
                if (i->isMember("credential")){
                  str += " credential=" + (*i)["credential"].toString() + ";";
                }
                if (i->isMember("credentialType")){
                  str += " credential-type=" + (*i)["credentialType"].toString() + ";";
                }
                links.push_back(str);
              }
              if (i->isMember("urls") && (*i)["urls"].isArray()){
                jsonForEach((*i)["urls"], j){
                  JSON::Value &u = *j;
                  std::string str = u.asString()+"; rel=\"ice-server\";";
                  if (i->isMember("username")){
                    str += " username=" + (*i)["username"].toString() + ";";
                  }
                  if (i->isMember("credential")){
                    str += " credential=" + (*i)["credential"].toString() + ";";
                  }
                  if (i->isMember("credentialType")){
                    str += " credential-type=" + (*i)["credentialType"].toString() + ";";
                  }
                  links.push_back(str);
                }
              }
            }
            if (links.size()){
              if (links.size() == 1){
                H.SetHeader("Link", *links.begin());
              }else{
                std::deque<std::string>::iterator it = links.begin();
                std::string linkHeader = *it;
                ++it;
                while (it != links.end()){
                  linkHeader += "\r\nLink: " + *it;
                  ++it;
                }
                H.SetHeader("Link", linkHeader);
              }
            }
          }
          H.setCORSHeaders();
          H.StartResponse("201", "Created", req, myConn);
          H.Chunkify(sdpAnswer.toString(), myConn);
          H.Chunkify(0, 0, myConn);
          myConn.close();
          return;
        }else{
          H.setCORSHeaders();
          H.StartResponse("403", "Not allowed", req, myConn);
          H.Chunkify("Request not allowed", myConn);
          H.Chunkify(0, 0, myConn);
          return;
        }
      }
    }

    // We don't implement PATCH requests
    if (req.method == "PATCH"){
      H.setCORSHeaders();
      H.StartResponse("405", "PATCH not supported", req, myConn);
      H.Chunkify("This endpoint only supports WHIP/WHEP/WISH POST requests or WebSocket connections", myConn);
      H.Chunkify(0, 0, myConn);
      return;
    }

    //Generic response handler
    H.setCORSHeaders();
    H.StartResponse("405", "Must POST or use websocket", req, myConn);
    H.Chunkify("This endpoint only supports WHIP/WHEP/WISH POST requests or WebSocket connections", myConn);
    H.Chunkify(0, 0, myConn);
  }

  // This function is executed when we receive a signaling data.
  // The signaling data contains commands that are used to start
  // an input or output stream.
  void OutWebRTC::onWebsocketFrame(){
    lastRecv = Util::bootMS();
    if (webSock->frameType != 1){
      HIGH_MSG("Ignoring non-text websocket frame");
      return;
    }
    
    JSON::Value command = JSON::fromString(webSock->data, webSock->data.size());
    JSON::Value commandResult;

    if(command.isMember("encrypt")){
      doDTLS = false;
      volkswagenMode = false;
      if(command["encrypt"].asString() == "no" || command["encrypt"].asString() == "none"){
        INFO_MSG("Disabling encryption");
      }else if(command["encrypt"].asString() == "placebo" || command["encrypt"].asString() == "volkswagen"){
        INFO_MSG("Entering volkswagen mode: encrypt data, but send plaintext for easier analysis");
        volkswagenMode = true;
      }else{
        doDTLS = true;
      }
    }


    // Check if there's a command type
    if (!command.isMember("type")){
      sendSignalingError("error", "Received a command but no type property was given.");
      return;
    }

    if (command["type"] == "offer_sdp"){
      if (!command.isMember("offer_sdp")){
        sendSignalingError("on_offer_sdp",
                           "An `offer_sdp` command needs the offer SDP in the `offer_sdp` field.");
        return;
      }
      if (command["offer_sdp"].asString() == ""){
        sendSignalingError("on_offer_sdp", "The given `offer_sdp` field is empty.");
        return;
      }

      if (config && config->hasOption("packetlog") && config->getBool("packetlog")){
        std::string fileName = "/tmp/wrtcpackets_"+JSON::Value(getpid()).asString();
        packetLog.open(fileName.c_str());
      }

      // get video and supported video formats from offer.
      SDP::Session sdpParser;
      const std::string &offerStr = command["offer_sdp"].asStringRef();
      if (packetLog.is_open()){
        packetLog << "[" << Util::bootMS() << "]" << offerStr << std::endl << std::endl;
      }
      if (!sdpParser.parseSDP(offerStr) || !sdpAnswer.parseOffer(offerStr)){
        sendSignalingError("offer_sdp", "Failed to parse the offered SDP");
        WARN_MSG("offer parse failed");
        return;
      }

      bool ret = false;
      if (sdpParser.hasSendOnlyMedia()){
        ret = handleSignalingCommandRemoteOfferForInput(sdpParser);
      }else{
        ret = handleSignalingCommandRemoteOfferForOutput(sdpParser);
      }
      // create result message.
      JSON::Value commandResult;
      commandResult["type"] = "on_answer_sdp";
      commandResult["result"] = ret;
      if (ret){commandResult["answer_sdp"] = sdpAnswer.toString();}
      webSock->sendFrame(commandResult.toString());
      return;
    }

    if (command["type"] == "video_bitrate"){
      if (!command.isMember("video_bitrate")){
        sendSignalingError("on_video_bitrate", "No video_bitrate attribute found.");
        return;
      }
      videoBitrate = command["video_bitrate"].asInt();
      if (videoBitrate == 0){
        FAIL_MSG("We received an invalid video_bitrate; resetting to default.");
        videoBitrate = 6 * 1000 * 1000;
        sendSignalingError("on_video_bitrate",
                           "Failed to handle the video bitrate change request.");
        return;
      }
      videoConstraint = videoBitrate;
      if (videoConstraint < 1024){videoConstraint = 1024;}
      JSON::Value commandResult;
      commandResult["type"] = "on_video_bitrate";
      commandResult["result"] = true;
      commandResult["video_bitrate"] = videoBitrate;
      commandResult["video_bitrate_constraint"] = videoConstraint;
      webSock->sendFrame(commandResult.toString());
      return;
    }

    if (command["type"] == "rtp_props"){
      if (command.isMember("nack")){
        RTP::PACKET_REORDER_WAIT = command["nack"].asInt();
      }
      if (command.isMember("drop")){
        RTP::PACKET_DROP_TIMEOUT = command["drop"].asInt();
      }
      JSON::Value commandResult;
      commandResult["type"] = "on_rtp_props";
      commandResult["result"] = true;
      commandResult["nack"] = RTP::PACKET_REORDER_WAIT;
      commandResult["drop"] = RTP::PACKET_DROP_TIMEOUT;
      webSock->sendFrame(commandResult.toString());
      return;
    }

    if (command["type"] == "keyframe_interval"){
      if (!command.isMember("keyframe_interval_millis")){
        sendSignalingError("on_keyframe_interval", "No keyframe_interval_millis attribute found.");
        return;
      }

      rtcpKeyFrameDelayInMillis = command["keyframe_interval_millis"].asInt();
      if (rtcpKeyFrameDelayInMillis < 500){
        WARN_MSG("Requested a keyframe delay < 500ms; 500ms is the minimum you can set.");
        rtcpKeyFrameDelayInMillis = 500;
      }

      rtcpKeyFrameTimeoutInMillis = Util::bootMS() + rtcpKeyFrameDelayInMillis;
      JSON::Value commandResult;
      commandResult["type"] = "on_keyframe_interval";
      commandResult["result"] = rtcpKeyFrameDelayInMillis;
      webSock->sendFrame(commandResult.toString());
      return;
    }

    // Unhandled
    sendSignalingError(command["type"].asString(), "Unhandled command type: " + command["type"].asString());
  }

  bool OutWebRTC::dropPushTrack(uint32_t trackId, const std::string & dropReason){
    if (!noSignalling){
      JSON::Value commandResult;
      commandResult["type"] = "on_track_drop";
      commandResult["track"] = trackId;
      commandResult["mediatype"] = M.getType(trackId);
      webSock->sendFrame(commandResult.toString());
    }
    return Output::dropPushTrack(trackId, dropReason);
  }

  void OutWebRTC::sendSignalingError(const std::string &commandType, const std::string &errorMessage){
    JSON::Value commandResult;
    commandResult["type"] = "on_error";
    commandResult["command"] = commandType;
    commandResult["message"] = errorMessage;
    webSock->sendFrame(commandResult.toString());
  }

  // This function is called for a peer that wants to receive
  // data from us. First we update our capabilities by checking
  // what codecs the peer and we support. After updating the
  // codecs we `initialize()` and use `selectDefaultTracks()` to
  // pick the right tracks based on our supported (and updated)
  // capabilities.
  bool OutWebRTC::handleSignalingCommandRemoteOfferForOutput(SDP::Session &sdpSession){

    updateCapabilitiesWithSDPOffer(sdpSession);
    initialize();
    selectDefaultTracks();

    if (config && config->hasOption("jitterlog") && config->getBool("jitterlog")){
      std::string fileName = "/tmp/jitter_"+JSON::Value(getpid()).asString();
      jitterLog.open(fileName.c_str());
      lastPackMs = Util::bootMS();
    }

    if (0 == udpPort){bindUDPSocketOnLocalCandidateAddress(0);}

    std::string videoCodec;
    std::string audioCodec;
    std::string metaCodec;
    capa["codecs"][0u][0u].null();
    capa["codecs"][0u][1u].null();

    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      if (M.getType(it->first) == "video"){
        vidTrack = it->first;
        videoCodec = M.getCodec(it->first);
        capa["codecs"][0u][0u].append(videoCodec);
      }
      if (M.getType(it->first) == "audio"){
        audTrack = it->first;
        audioCodec = M.getCodec(it->first);
        capa["codecs"][0u][1u].append(audioCodec);
      }
      if (M.getType(it->first) == "meta"){
        metaTrack = it->first;
        metaCodec = M.getCodec(it->first);
        capa["codecs"][0u][2u].append(std::string("+") + metaCodec);
      }
    }

    sdpAnswer.setDirection("sendonly");

    // setup video WebRTC Track.
    if (vidTrack != INVALID_TRACK_ID){
      if (sdpAnswer.enableVideo(M.getCodec(vidTrack), sdpSession)){
        WebRTCTrack &videoTrack = webrtcTracks[vidTrack];
        if (!createWebRTCTrackFromAnswer(sdpAnswer.answerVideoMedia, sdpAnswer.answerVideoFormat, videoTrack)){
          FAIL_MSG("Failed to create the WebRTCTrack for the selected video.");
          webrtcTracks.erase(vidTrack);
          return false;
        }
        videoTrack.rtpPacketizer = RTP::Packet(videoTrack.payloadType, rand(), 0, videoTrack.SSRC, 0);
        if (!config || !config->hasOption("nackdisable") || !config->getBool("nackdisable")){
          // Enable NACKs
          sdpAnswer.videoLossPrevention = SDP_LOSS_PREVENTION_NACK;
        }
        videoTrack.sorter.tmpVideoLossPrevention = sdpAnswer.videoLossPrevention;
      }
    }

    // setup audio WebRTC Track
    if (audTrack != INVALID_TRACK_ID){
      if (sdpAnswer.enableAudio(M.getCodec(audTrack), sdpSession)){
        WebRTCTrack &audioTrack = webrtcTracks[audTrack];
        if (!createWebRTCTrackFromAnswer(sdpAnswer.answerAudioMedia, sdpAnswer.answerAudioFormat, audioTrack)){
          FAIL_MSG("Failed to create the WebRTCTrack for the selected audio.");
          webrtcTracks.erase(audTrack);
          return false;
        }
        audioTrack.rtpPacketizer = RTP::Packet(audioTrack.payloadType, rand(), 0, audioTrack.SSRC, 0);
      }
    }

    // setup meta WebRTC Track
    if (metaTrack != INVALID_TRACK_ID){
      if (sdpAnswer.enableMeta(M.getCodec(metaTrack), sdpSession)){
        WebRTCTrack &mTrack = webrtcTracks[metaTrack];
        if (!createWebRTCTrackFromAnswer(sdpAnswer.answerMetaMedia, sdpAnswer.answerMetaFormat, mTrack)){
          FAIL_MSG("Failed to create the WebRTCTrack for the selected metadata.");
          webrtcTracks.erase(metaTrack);
          return false;
        }
      }
    }

    // we set parseData to `true` to start the data flow. Is also
    // used to break out of our loop in `onHTTP()`.
    parseData = true;
    idleInterval = 1000;

    return true;
  }

  void OutWebRTC::handleWebsocketIdle(){
    if (!parseData && isPushing()){
      JSON::Value commandResult;
      commandResult["type"] = "on_media_receive";
      commandResult["millis"] = endTime();
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        commandResult["tracks"].append(M.getCodec(it->first));
      }
      commandResult["stats"]["nack_num"] = stats_nacknum;
      commandResult["stats"]["loss_num"] = stats_lossnum;
      commandResult["stats"]["jitter_ms"] = stats_jitter;
      commandResult["stats"]["loss_perc"] = stats_lossperc;
      webSock->sendFrame(commandResult.toString());
      return;
    }
    HTTPOutput::handleWebsocketIdle();
  }

  // Creates a WebRTCTrack for the given `SDP::Media` and
  // `SDP::MediaFormat`. The `SDP::MediaFormat` must contain the
  // `icePwd` and `iceUFrag` which are used when we're handling
  // STUN messages (these can be used to verify the integrity).
  //
  // When the `SDP::Media` has it's SSRC set (not 0) we copy it.
  // This is the case when we're receiving data from another peer
  // and the `WebRTCTrack` is used to handle input data. When the
  // SSRC is 0 we generate a new one and we assume that the other
  // peer expect us to send data.
  bool OutWebRTC::createWebRTCTrackFromAnswer(const SDP::Media &mediaAnswer,
                                              const SDP::MediaFormat &formatAnswer, WebRTCTrack &result){
    if (formatAnswer.payloadType == SDP_PAYLOAD_TYPE_NONE && formatAnswer.encodingName != "WEBRTC-DATACHANNEL"){
      FAIL_MSG("Cannot create a WebRTCTrack, the given %s SDP::MediaFormat has no `payloadType` set.", formatAnswer.encodingName.c_str());
      return false;
    }

    if (formatAnswer.icePwd.empty()){
      FAIL_MSG("Cannot create a WebRTCTrack, the given SDP::MediaFormat has no `icePwd` set.");
      return false;
    }

    if (formatAnswer.iceUFrag.empty()){
      FAIL_MSG("Cannot create a WebRTCTrack, the given SDP::MediaFormat has no `iceUFrag` set.");
      return false;
    }

    result.payloadType = formatAnswer.getPayloadType();
    result.localIcePwd = formatAnswer.icePwd;
    result.localIceUFrag = formatAnswer.iceUFrag;

    if (mediaAnswer.SSRC){
      result.SSRC = mediaAnswer.SSRC;
    }else{
      result.SSRC = rand();
    }

    return true;
  }

  // This function checks what capabilities the offer has and
  // updates the `capa[codecs]` member with the codecs that we
  // and the offer supports. Note that the names in the preferred
  // codec array below should use the codec names as MistServer
  // defines them. SDP internally uses fullcaps.
  //
  // Jaron advised me to use this approach: when we receive an
  // offer we update the capabilities with the matching codecs
  // and once we've updated those we call `selectDefaultTracks()`
  // which sets up the tracks in `myMeta`.
  void OutWebRTC::updateCapabilitiesWithSDPOffer(SDP::Session &sdpSession){

    capa["codecs"].null();

    const char *videoCodecPreference[] ={"H264", "VP9", "VP8", NULL};
    const char **videoCodec = videoCodecPreference;
    SDP::Media *videoMediaOffer = sdpSession.getMediaForType("video");
    if (videoMediaOffer){
      while (*videoCodec){
        if (sdpSession.getMediaFormatByEncodingName("video", *videoCodec)){
          capa["codecs"][0u][0u].append(*videoCodec);
        }
        videoCodec++;
      }
    }

    const char *audioCodecPreference[] ={"opus", "ALAW", "ULAW", NULL};
    const char **audioCodec = audioCodecPreference;
    SDP::Media *audioMediaOffer = sdpSession.getMediaForType("audio");
    if (audioMediaOffer){
      while (*audioCodec){
        if (sdpSession.getMediaFormatByEncodingName("audio", *audioCodec)){
          capa["codecs"][0u][1u].append(*audioCodec);
        }
        audioCodec++;
      }
    }

    const char *metaCodecPreference[] ={"JSON", "subtitle", NULL};
    const char **metaCodec = metaCodecPreference;
    SDP::Media *metaMediaOffer = sdpSession.getMediaForType("meta");
    if (metaMediaOffer){
      INFO_MSG("Has meta offer!");
      while (*metaCodec){
        if (sdpSession.getMediaFormatByEncodingName("meta", *metaCodec)){
          capa["codecs"][0u][2u].append(std::string("+") + *metaCodec);
        }
        metaCodec++;
      }
    }
  }

  // This function is called to handle an offer from a peer that wants to push data towards us.
  bool OutWebRTC::handleSignalingCommandRemoteOfferForInput(SDP::Session &sdpSession){


    if (!meta.getBootMsOffset()){meta.setBootMsOffset(Util::bootMS());}

    if (ioThread){
      FAIL_MSG("It seems that we're already have a WebRTC I/O thread running, aborting input request");
      return false;
    }

    if (0 == udpPort){bindUDPSocketOnLocalCandidateAddress(0);}

    std::string prefVideoCodec = "VP9,VP8,H264";
    if (config && config->hasOption("preferredvideocodec")){
      prefVideoCodec = config->getString("preferredvideocodec");
      if (prefVideoCodec.empty()){
        WARN_MSG("No preferred video codec value set; resetting to default.");
        prefVideoCodec = "VP9,VP8,H264";
      }
    }

    std::string prefAudioCodec = "opus,ALAW,ULAW";
    if (config && config->hasOption("preferredaudiocodec")){
      prefAudioCodec = config->getString("preferredaudiocodec");
      if (prefAudioCodec.empty()){
        WARN_MSG("No preferred audio codec value set; resetting to default.");
        prefAudioCodec = "opus,ALAW,ULAW";
      }
    }


    if (Triggers::shouldTrigger("PUSH_REWRITE")){
      std::string payload = reqUrl + "\n" + getConnectedHost() + "\n" + streamName;
      std::string newStream = streamName;
      Triggers::doTrigger("PUSH_REWRITE", payload, "", false, newStream);
      if (!newStream.size()){
        FAIL_MSG("Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                 getConnectedHost().c_str(), reqUrl.c_str());
        return false;
      }else{
        streamName = newStream;
        Util::sanitizeName(streamName);
        Util::setStreamName(streamName);
      }
    }

    // allow peer to push video/audio
    if (!allowPush("")){
      FAIL_MSG("Failed to allow push");
      return false;
    }
    INFO_MSG("Push accepted");

    meta.reInit(streamName, false);

    // video
    if (sdpAnswer.enableVideo(prefVideoCodec, sdpSession)){

      size_t vIdx = meta.addDelayedTrack();
      if (!sdpAnswer.setupVideoDTSCTrack(meta, vIdx)){
        FAIL_MSG("Failed to setup video DTSC track.");
        return false;
      }

      WebRTCTrack &videoTrack = webrtcTracks[vIdx];
      videoTrack.payloadType = sdpAnswer.answerVideoFormat.getPayloadType();
      videoTrack.localIcePwd = sdpAnswer.answerVideoFormat.icePwd;
      videoTrack.localIceUFrag = sdpAnswer.answerVideoFormat.iceUFrag;
      videoTrack.SSRC = sdpAnswer.answerVideoMedia.SSRC;

      SDP::MediaFormat *fmtRED = sdpSession.getMediaFormatByEncodingName("video", "RED");
      SDP::MediaFormat *fmtULPFEC = sdpSession.getMediaFormatByEncodingName("video", "ULPFEC");
      if (fmtRED || fmtULPFEC){
        videoTrack.ULPFECPayloadType = fmtULPFEC->payloadType;
        videoTrack.REDPayloadType = fmtRED->payloadType;
        payloadTypeToWebRTCTrack[fmtRED->payloadType] = videoTrack.payloadType;
        payloadTypeToWebRTCTrack[fmtULPFEC->payloadType] = videoTrack.payloadType;
      }
      sdpAnswer.videoLossPrevention = SDP_LOSS_PREVENTION_NACK;
      videoTrack.sorter.tmpVideoLossPrevention = sdpAnswer.videoLossPrevention;

      if (!sdpAnswer.setupVideoDTSCTrack(meta, vIdx)){
        FAIL_MSG("Failed to setup video DTSC track.");
        return false;
      }

      videoTrack.rtpToDTSC.setProperties(meta, vIdx);
      videoTrack.rtpToDTSC.setCallbacks(onDTSCConverterHasPacketCallback, onDTSCConverterHasInitDataCallback);
      videoTrack.sorter.setCallback(M.getID(vIdx), onRTPSorterHasPacketCallback);

      userSelect[vIdx].reload(streamName, vIdx, COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE);
      INFO_MSG("Video push received on track %zu", vIdx);
    }

    // audio setup
    if (sdpAnswer.enableAudio(prefAudioCodec, sdpSession)){

      size_t aIdx = meta.addDelayedTrack();
      if (!sdpAnswer.setupAudioDTSCTrack(meta, aIdx)){
        FAIL_MSG("Failed to setup audio DTSC track.");
      }

      WebRTCTrack &audioTrack = webrtcTracks[aIdx];
      audioTrack.payloadType = sdpAnswer.answerAudioFormat.getPayloadType();
      audioTrack.localIcePwd = sdpAnswer.answerAudioFormat.icePwd;
      audioTrack.localIceUFrag = sdpAnswer.answerAudioFormat.iceUFrag;
      audioTrack.SSRC = sdpAnswer.answerAudioMedia.SSRC;

      audioTrack.rtpToDTSC.setProperties(meta, aIdx);
      audioTrack.rtpToDTSC.setCallbacks(onDTSCConverterHasPacketCallback, onDTSCConverterHasInitDataCallback);
      audioTrack.sorter.setCallback(M.getID(aIdx), onRTPSorterHasPacketCallback);

      userSelect[aIdx].reload(streamName, aIdx, COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE);
      INFO_MSG("Audio push received on track %zu", aIdx);
    }

    sdpAnswer.setDirection("recvonly");

    // start our receive thread (handles STUN, DTLS, RTP input)
    rtcpTimeoutInMillis = Util::bootMS() + 2000;
    rtcpKeyFrameTimeoutInMillis = Util::bootMS() + 2000;
    ioThread = new tthread::thread(webRTCInputOutputThreadFunc, NULL);

    idleInterval = 1000;

    return true;
  }

  bool OutWebRTC::bindUDPSocketOnLocalCandidateAddress(uint16_t port){

    if (udpPort != 0){
      FAIL_MSG("Already bound the UDP socket.");
      return false;
    }

    std::string bindAddr;
    //If a bind host has been put in as override, use it
    if (config && config->hasOption("bindhost") && config->getString("bindhost").size()){
      bindAddr = config->getString("bindhost");
      udpPort = mainSocket.bind(port, bindAddr);
      if (!udpPort){
        WARN_MSG("UDP bind address not valid - ignoring setting and using best guess instead");
        bindAddr.clear();
      }else{
        INFO_MSG("Bound to pre-configured UDP bind address");
      }
    }
    //use the best IPv4 guess we have
    if (!bindAddr.size()){
      bindAddr = Socket::resolveHostToBestExternalAddrGuess(externalAddr, AF_INET, myConn.getBoundAddress());
      if (!bindAddr.size()){
        INFO_MSG("UDP bind to best guess failed - using same address as incoming connection as a last resort");
        bindAddr.clear();
      }else{
        udpPort = mainSocket.bind(port, bindAddr);
        if (!udpPort){
          INFO_MSG("UDP bind to best guess failed - using same address as incoming connection as a last resort");
          bindAddr.clear();
        }else{
          INFO_MSG("Bound to public UDP bind address derived from hostname");
        }
      }
    }
    if (!bindAddr.size()){
      bindAddr = myConn.getBoundAddress();
      udpPort = mainSocket.bind(port, bindAddr);
      if (!udpPort){
        FAIL_MSG("UDP bind to connected address failed - we're out of options here, I'm afraid...");
        bindAddr.clear();
      }else{
        INFO_MSG("Bound to same UDP address as TCP address - this is potentially wrong, but used as a last resort");
      }
    }

    Util::Procs::socketList.insert(mainSocket.getSock());

    // this is necessary so that we can get the remote IP when creating STUN replies.
    mainSocket.allocateDestination();

    if (config && config->hasOption("pubhost") && config->getString("pubhost").size()){
      bindAddr = config->getString("pubhost");
    }
    sdpAnswer.setCandidate(bindAddr, udpPort);
    return true;
  }

  /* ------------------------------------------------ */

  /// Worker function for ioThread
  void OutWebRTC::handleWebRTCInputOutputFromThread(){
    while (keepGoing()){
      if (!handleWebRTCInputOutput()){sendPaced(10);}
    }
  }

  void OutWebRTC::connStats(uint64_t now, Comms::Connections &statComm){
    statComm.setUp(myConn.dataUp());
    statComm.setDown(myConn.dataDown());
    statComm.setPacketCount(totalPkts);
    statComm.setPacketLostCount(totalLoss);
    statComm.setPacketRetransmitCount(totalRetrans);
    statComm.setTime(now - myConn.connTime());
  }

  bool OutWebRTC::handleUDPSocket(Socket::UDPConnection & sock){
    bool hadPack = false;
    while(sock.Receive()){
      std::string remoteIP, localIP;
      uint32_t remotePort, localPort;
      sock.GetDestination(remoteIP, remotePort);
      sock.GetLocalDestination(localIP, localPort);

      // Check if we already have a socket handling this exact connection, if so, don't create a new one
      bool existsAlready = false;
      for (std::map<int, WebRTCSocket>::iterator it = sockets.begin(); it != sockets.end(); it++){
        if (!*(it->second.udpSock)){
          int sockNo = it->first;
          sockets.erase(sockNo);
          rtpSockets.erase(sockNo);
          sctpSockets.erase(sockNo);
          it = sockets.begin();
          if (!sockets.size()){break;}
        }
        if (*(it->second.udpSock) == sock){
          existsAlready = true;
          INFO_MSG("Duplicate socket, not spawning another, inserting packet instead");
          break;
        }
      }
      if (existsAlready){continue;}

      // No existing socket? Create a new one specifically for this exact connection
      Socket::UDPConnection * s = new Socket::UDPConnection(sock);
      if (s->connect()){
        s->initDTLS(&(cert.cert), &(cert.key));
        sockets[s->getSock()].udpSock = s;
        Util::Procs::socketList.insert(s->getSock());
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "Connected new socket " << s->getSock() << " for: " << localIP << ":" << localPort << " <-> " << remoteIP << ":" << remotePort << std::endl;}
      }else{
        delete s;
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "Failed to connect new socket for: " << remoteIP << ":" << remotePort << std::endl;}
      }
      hadPack = true;
    }
    return hadPack;
  }

  bool OutWebRTC::handleUDPSocket(WebRTCSocket & wSock){
    bool hadPack = false;
    while(wSock.udpSock->Receive()){
      hadPack = true;
      myConn.addDown(wSock.udpSock->data.size());

      if (wSock.udpSock->data.size() && wSock.udpSock->wasEncrypted){
        lastRecv = Util::bootMS();
#ifdef WITH_DATACHANNELS
        if (packetLog.is_open()){
          packetLog << "[" << Util::bootMS() << "]" << "SCTP packet (" << wSock.udpSock->data.size() << "b): " << std::endl;
          char * buffer = usrsctp_dumppacket(wSock.udpSock->data, wSock.udpSock->data.size(), SCTP_DUMP_INBOUND);
          packetLog << buffer;
          usrsctp_freedumpbuffer(buffer);
        }
        if (!sctpSockets.count(wSock.udpSock->getSock())){
          int s = wSock.udpSock->getSock();
          rtpSockets.erase(s);
          sctpSockets.insert(s);
        }
        if (!sctpInited){
          INFO_MSG("Initializing SCTP library");
          usrsctp_init(0, sctp_send_cb, sctp_debug_cb);
          usrsctp_register_address((void *)this);
          sctp_sock = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, sctp_recv_cb, NULL, 0, NULL);
          struct sockaddr_conn sconn;
          memset(&sconn, 0, sizeof(struct sockaddr_conn));
          sconn.sconn_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
          sconn.sconn_len = sizeof(struct sockaddr_conn);
#endif
          sconn.sconn_port = htons(5000);
          sconn.sconn_addr = (void *)this;
          usrsctp_bind(sctp_sock, (struct sockaddr *)&sconn, sizeof(struct sockaddr_conn));
          usrsctp_listen(sctp_sock, 1);
          sctpInited = true;
        }
        usrsctp_conninput(this, wSock.udpSock->data, wSock.udpSock->data.size(), 0);
        //usrsctp_accept(sctp_sock, 0, 0);
#endif
        continue;
      }

      uint8_t fb = (uint8_t)wSock.udpSock->data[0];
      if (fb > 127 && fb < 192){
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "Packet " << (int)fb << ": RTP/RTCP" << std::endl;}
        handleReceivedRTPOrRTCPPacket(wSock);
      }else if (fb > 19 && fb < 64){
        if (packetLog.is_open()){
          std::string remoteIP;
          uint32_t remotePort;
          wSock.udpSock->GetDestination(remoteIP, remotePort);
          packetLog << "[" << Util::bootMS() << "]" << "DTLS (" << remoteIP << ":" << remotePort << ") - Non-application-level data" << std::endl;
        }
      }else if (fb < 2){
        handleReceivedSTUNPacket(wSock);
      }else{
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "Packet " << (int)fb << ": Unknown" << std::endl;}
        FAIL_MSG("Unhandled WebRTC data. Type: %02X", fb);
      }
    }
    return hadPack;
  }

  // Checks if there is data on our UDP socket. The data can be
  // STUN, DTLS, SRTP or SRTCP. When we're receiving media from
  // the browser (e.g. from webcam) this function is called from
  // a separate thread. When we're pushing media to the browser
  // this is called from the main thread.
  bool OutWebRTC::handleWebRTCInputOutput(){

    bool hadPack = false;
    hadPack |= handleUDPSocket(mainSocket);


    for (std::map<int, WebRTCSocket>::iterator it = sockets.begin(); it != sockets.end(); it++){
      bool wasInited = it->second.udpSock->cipher.size();
      hadPack |= handleUDPSocket(it->second);
      if (!wasInited && it->second.udpSock->cipher.size()){
        if (it->second.srtpReader.init(it->second.udpSock->cipher, it->second.udpSock->remote_key, it->second.udpSock->remote_salt) != 0){
          FAIL_MSG("Failed to initialize the SRTP reader.");
        }
        if (it->second.srtpWriter.init(it->second.udpSock->cipher, it->second.udpSock->local_key, it->second.udpSock->local_salt) != 0){
          FAIL_MSG("Failed to initialize the SRTP writer.");
        }
        rtpSockets.insert(it->first);
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "SRTP reader/writer " << it->first << " initialized" << std::endl;}
      }

      if (!*(it->second.udpSock)){
        int sockNo = it->first;
        sockets.erase(sockNo);
        rtpSockets.erase(sockNo);
        sctpSockets.erase(sockNo);
        it = sockets.begin();
        if (!sockets.size()){break;}
      }
    }

    //If this is an incoming push, handle receiver reports and keyframe interval
    if (isPushing()){
      uint64_t now = Util::bootMS();
      
      //Receiver reports and packet loss calculations
      if (now >= rtcpTimeoutInMillis){
        std::map<uint64_t, WebRTCTrack>::iterator it;
        for (it = webrtcTracks.begin(); it != webrtcTracks.end(); ++it){
          if (!M || M.getType(it->first) != "video"){continue;}//Video-only, at least for now
          sendRTCPFeedbackREMB(it->second);
          sendRTCPFeedbackRR(it->second);
        }
        rtcpTimeoutInMillis = now + 1000; /* was 5000, lowered for FEC */
      }

      //Keyframe requests
      if (now >= rtcpKeyFrameTimeoutInMillis){
        std::map<uint64_t, WebRTCTrack>::iterator it;
        for (it = webrtcTracks.begin(); it != webrtcTracks.end(); ++it){
          if (!M || M.getType(it->first) != "video"){continue;}//Video-only
          sendRTCPFeedbackPLI(it->second);
        }
        rtcpKeyFrameTimeoutInMillis = now + rtcpKeyFrameDelayInMillis;
      }
    }

    if (mainSocket.getSock() == -1){onFail("UDP socket closed", true);}
    return hadPack;
  }

  void OutWebRTC::handleReceivedSTUNPacket(WebRTCSocket &wSock){

    size_t nparsed = 0;
    StunMessage stun_msg;
    if (stunReader.parse((uint8_t *)(char*)wSock.udpSock->data, wSock.udpSock->data.size(), nparsed, stun_msg) != 0){
      if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "STUN: (unparsable)" << std::endl;}
      FAIL_MSG("Failed to parse a stun message.");
      return;
    }

    if (stun_msg.type != STUN_MSG_TYPE_BINDING_REQUEST){
      if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "STUN: (non-binding request, ignored)" << std::endl;}
      INFO_MSG("We only handle STUN binding requests as we're an ice-lite implementation.");
      return;
    }

    // get the username for whom we got a binding request.
    StunAttribute *usernameAttrib = stun_msg.getAttributeByType(STUN_ATTR_TYPE_USERNAME);
    if (!usernameAttrib){
      ERROR_MSG("No username attribute found in the STUN binding request. Cannot create success "
                "binding response.");
      return;
    }
    if (usernameAttrib->username.value == 0){
      ERROR_MSG("The username attribute is empty.");
      return;
    }
    std::string username(usernameAttrib->username.value, usernameAttrib->length);
    std::size_t usernameColonPos = username.find(":");
    if (usernameColonPos == std::string::npos){
      ERROR_MSG("The username in the STUN attribute has an invalid format: %s.", username.c_str());
      return;
    }
    std::string usernameLocal = username.substr(0, usernameColonPos);

    // get the password for the username that is used to create our message-integrity.
    std::string passwordLocal;
    std::map<uint64_t, WebRTCTrack>::iterator rtcTrackIt = webrtcTracks.begin();
    while (rtcTrackIt != webrtcTracks.end()){
      WebRTCTrack &tr = rtcTrackIt->second;
      if (tr.localIceUFrag == usernameLocal){passwordLocal = tr.localIcePwd;}
      ++rtcTrackIt;
    }
    if (passwordLocal.empty()){
      ERROR_MSG("No local ICE password found for username %s. Did you create a WebRTCTrack?",
                usernameLocal.c_str());
      return;
    }
    lastRecv = Util::bootMS();

    std::string remoteIP;
    uint32_t remotePort;
    wSock.udpSock->GetDestination(remoteIP, remotePort);
    if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "STUN: " << remoteIP << ":" << remotePort << std::endl;}

    // create the binding success response
    stun_msg.removeAttributes();
    stun_msg.setType(STUN_MSG_TYPE_BINDING_RESPONSE_SUCCESS);

    StunWriter stun_writer;
    stun_writer.begin(stun_msg);
    stun_writer.writeXorMappedAddress(STUN_IP4, remotePort, remoteIP);
    stun_writer.writeMessageIntegrity(passwordLocal);
    stun_writer.writeFingerprint();
    stun_writer.end();
    
    wSock.udpSock->SendNow((const char *)stun_writer.getBufferPtr(), stun_writer.getBufferSize());
    myConn.addUp(stun_writer.getBufferSize());
  }

  void OutWebRTC::ackNACK(uint32_t pSSRC, uint16_t seq){
    
    for (std::set<int>::iterator it = rtpSockets.begin(); it != rtpSockets.end(); ++it){
      if (!*(sockets[*it].udpSock)){continue;}
      size_t sent = sockets[*it].ackNACK(pSSRC, seq);
      if (sent){
        totalRetrans++;
        myConn.addUp(sent);
      }
    }
    HIGH_MSG("Answered NACK for %" PRIu32 " #%" PRIu16, pSSRC, seq);
  }

  void OutWebRTC::handleReceivedRTPOrRTCPPacket(WebRTCSocket &wSock){

    // Mark this socket as an (S)RTP socket, if not already marked
    if (!rtpSockets.count(wSock.udpSock->getSock())){rtpSockets.insert(wSock.udpSock->getSock());}

    uint8_t pt = wSock.udpSock->data[1] & 0x7F;

    if ((pt < 64) || (pt >= 96)){

      RTP::Packet rtp_pkt((const char *)wSock.udpSock->data, (unsigned int)wSock.udpSock->data.size());
      uint16_t currSeqNum = rtp_pkt.getSequence();

      size_t idx = M.trackIDToIndex(rtp_pkt.getPayloadType(), getpid());

      // Do we need to map the payload type to a WebRTC Track? (e.g. RED)
      if (payloadTypeToWebRTCTrack.count(rtp_pkt.getPayloadType()) != 0){
        idx = M.trackIDToIndex(payloadTypeToWebRTCTrack[rtp_pkt.getPayloadType()], getpid());
      }

      if (idx == INVALID_TRACK_ID || !webrtcTracks.count(idx)){
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "RTP payload type " << rtp_pkt.getPayloadType() << " not prepared, ignoring" << std::endl;}
        FAIL_MSG("Received an RTP packet for a track that we didn't prepare for. PayloadType is "
                 "%" PRIu32 ", idx %zu",
                 rtp_pkt.getPayloadType(), idx);
        return;
      }

      // Find the WebRTCTrack corresponding to the packet we received
      WebRTCTrack &rtcTrack = webrtcTracks[idx];

      // Decrypt the SRTP to RTP
      int len = wSock.udpSock->data.size();
      if (wSock.srtpReader.unprotectRtp((uint8_t *)(char*)wSock.udpSock->data, &len) != 0){
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "RTP decrypt failure" << std::endl;}
        return;
      }
      if (!len){return;}
      lastRecv = Util::bootMS();
      RTP::Packet unprotPack(wSock.udpSock->data, len);
      DONTEVEN_MSG("%s", unprotPack.toString().c_str());

      rtcTrack.gotPacket(unprotPack.getTimeStamp());

      if (rtp_pkt.getPayloadType() == rtcTrack.REDPayloadType || rtp_pkt.getPayloadType() == rtcTrack.ULPFECPayloadType){
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "RED packet " << rtp_pkt.getPayloadType() << " #" << currSeqNum << std::endl;}
        rtcTrack.sorter.addREDPacket(wSock.udpSock->data, len, rtcTrack.payloadType, rtcTrack.REDPayloadType,
                                     rtcTrack.ULPFECPayloadType);
      }else{
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "Basic packet " << rtp_pkt.getPayloadType() << " #" << currSeqNum << std::endl;}
        rtcTrack.sorter.addPacket(unprotPack);
      }

      //Send NACKs for packets that we still need
      while (rtcTrack.sorter.wantedSeqs.size()){
        uint16_t sNum = *(rtcTrack.sorter.wantedSeqs.begin());
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "Sending NACK for sequence #" << sNum << std::endl;}
        stats_nacknum++;
        totalRetrans++;
        sendRTCPFeedbackNACK(rtcTrack, sNum);
        rtcTrack.sorter.wantedSeqs.erase(sNum);
      }

    }else{
      //Decrypt feedback packet
      int len = wSock.udpSock->data.size();
      if (doDTLS){
        if (wSock.srtpReader.unprotectRtcp((uint8_t *)(char*)wSock.udpSock->data, &len) != 0){
          if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "RTCP decrypt failure" << std::endl;}
          return;
        }
        if (!len){return;}
      }

      lastRecv = Util::bootMS();
      uint8_t fmt = wSock.udpSock->data[0] & 0x1F;
      if (pt == 77 || pt == 65){
        //77/65 = nack
        if (fmt == 1){
          uint32_t pSSRC = Bit::btohl(wSock.udpSock->data + 8);
          uint16_t seq = Bit::btohs(wSock.udpSock->data + 12);
          uint16_t bitmask = Bit::btohs(wSock.udpSock->data + 14);
          ackNACK(pSSRC, seq);
          size_t missed = 1;
          if (bitmask & 1){ackNACK(pSSRC, seq + 1); missed++;}
          if (bitmask & 2){ackNACK(pSSRC, seq + 2); missed++;}
          if (bitmask & 4){ackNACK(pSSRC, seq + 3); missed++;}
          if (bitmask & 8){ackNACK(pSSRC, seq + 4); missed++;}
          if (bitmask & 16){ackNACK(pSSRC, seq + 5); missed++;}
          if (bitmask & 32){ackNACK(pSSRC, seq + 6); missed++;}
          if (bitmask & 64){ackNACK(pSSRC, seq + 7); missed++;}
          if (bitmask & 128){ackNACK(pSSRC, seq + 8); missed++;}
          if (bitmask & 256){ackNACK(pSSRC, seq + 9); missed++;}
          if (bitmask & 512){ackNACK(pSSRC, seq + 10); missed++;}
          if (bitmask & 1024){ackNACK(pSSRC, seq + 11); missed++;}
          if (bitmask & 2048){ackNACK(pSSRC, seq + 12); missed++;}
          if (bitmask & 4096){ackNACK(pSSRC, seq + 13); missed++;}
          if (bitmask & 8192){ackNACK(pSSRC, seq + 14); missed++;}
          if (bitmask & 16384){ackNACK(pSSRC, seq + 15); missed++;}
          if (bitmask & 32768){ackNACK(pSSRC, seq + 16); missed++;}
          if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "NACK: " << missed << " missed packet(s)" << std::endl;}
        }else{
          if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "Feedback: Unimplemented (type " << fmt << ")" << std::endl;}
          INFO_MSG("Received unimplemented RTP feedback message (%d)", fmt);
        }
      }else if (pt == 78){
        //78 = PLI
        if (fmt == 1){
          if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "PLI: Picture Loss Indication ( = keyframe request = ignored)" << std::endl;}
          DONTEVEN_MSG("Received picture loss indication");
        }else{
          if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "Feedback: Unimplemented (payload specific type " << fmt << ")" << std::endl;}
          INFO_MSG("Received unimplemented payload-specific feedback message (%d)", fmt);
        }
      }else if (pt == 72){
        //72 = sender report
        uint32_t SSRC = Bit::btohl(wSock.udpSock->data + 4);
        std::map<uint64_t, WebRTCTrack>::iterator it;
        for (it = webrtcTracks.begin(); it != webrtcTracks.end(); ++it){
          if (it->second.SSRC == SSRC){
            it->second.sorter.lastBootMS = Util::bootMS();
            it->second.sorter.lastNTP = Bit::btohl(wSock.udpSock->data+10);
            uint64_t ntpTime = Bit::btohll(wSock.udpSock->data + 8);
            uint32_t rtpTime = Bit::btohl(wSock.udpSock->data + 16);
            uint32_t packets = Bit::btohl(wSock.udpSock->data + 20);
            if (packets > it->second.lastPktCount){
              //counter went up; check if it was less than half the range
              if ((packets - it->second.lastPktCount) <= 0x7FFFFFFF){
                //It was; great; let's trust it and move on
                totalPkts += packets - it->second.lastPktCount;
                it->second.lastPktCount = packets;
              }
              //The else case is a no-op:
              //If it went up too much we likely just received an older packet from before last wraparound.
            }else{
              //counter decreased; could be a wraparound!
              //if our new number is in the first 25% and the old number in the last 25%, we assume it was
              if (packets <= 0x3FFFFFFF && it->second.lastPktCount >= 0xBFFFFFFF){
                //Account for the wraparound, save the new packet counter
                totalPkts += (packets - it->second.lastPktCount);
                it->second.lastPktCount = packets;
              }
              //The else case is a no-op:
              //If it went down outside those ranges, this is an older packet we should just ignore
            }
            uint32_t bytes = Bit::btohl(wSock.udpSock->data + 24);
            HIGH_MSG("Received sender report for track %s (%" PRIu32 " pkts, %" PRIu32 "b) time: %" PRIu32 " RTP = %" PRIu64 " NTP", it->second.rtpToDTSC.codec.c_str(), packets, bytes, rtpTime, ntpTime);
            if (rtpTime && ntpTime){
              //msDiff is the amount of millis our current NTP time is ahead of the sync moment NTP time
              //May be negative, if we're behind instead of ahead.
              uint64_t ntpDiff = Util::getNTP()-ntpTime;
              int64_t msDiff = (ntpDiff>>32) * 1000 + (ntpDiff & 0xFFFFFFFFul) / 4294967.295;
              if (!syncedNTPClock){
                syncedNTPClock = true;
                ntpClockDifference = -msDiff;
              }
              it->second.rtpToDTSC.timeSync(rtpTime, msDiff+ntpClockDifference);
            }
            break;
          }
        }
      }else if (pt == 73){
        //73 = 201 = receiver report: https://datatracker.ietf.org/doc/html/rfc3550#section-6.4.2
        //Packet may contain more than one report
        char * ptr = wSock.udpSock->data + 8;
        while (ptr + 24 <= wSock.udpSock->data + wSock.udpSock->data.size()){
          //Update the counter for this ssrc
          uint32_t ssrc = Bit::btoh24(ptr);
          lostPackets[ssrc] = Bit::btoh24(ptr + 5);
          //Update pointer to next report
          ptr += 24;
        }
        //Count total lost packets
        totalLoss = 0;
        for (std::map<uint32_t, uint32_t>::iterator it = lostPackets.begin(); it != lostPackets.end(); ++it){
          totalLoss += it->second;
        }
      }else if (pt == 74){
        // 74 = 202 = SDES: https://www.ietf.org/rfc/rfc1889.html#section-6.4
        Util::ResizeablePointer & p = wSock.udpSock->data;
        // Check padding bit
        if (p[0] & 0x20){
          // Padding count is stored in the last octet
          size_t padding = p[p.size() - 1];
          if (padding > p.size()){padding = p.size();}
          p.truncate(p.size() - padding);
        }
        size_t offset = 4;
        while (offset + 5 <= p.size()){
          uint32_t ssrc = Bit::btohl((char*)p+offset);
          offset += 4;
          while (offset + 2 <= p.size()){
            uint8_t type = p[offset];
            if (!type){
              ++offset;
              break;
            }
            uint8_t len = p[offset+1];
            if (offset+2+len <= p.size()){
              std::string val(offset+2, len);
              // Ignore blank SDES messages
              if (len){
                INFO_MSG("SDES for %" PRIu32 ": type %" PRIu8 " = %s", ssrc, type, val.c_str());
              }
            }
            offset += len +2;
          }
          // Ensure alignment
          if (offset % 4){offset += 4 - (offset % 4);}
        }
      }else{
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "Unknown payload type: " << pt << std::endl;}
        WARN_MSG("Unknown RTP feedback payload type: %u", pt);
      }
    }
  }

  /* ------------------------------------------------ */

  void OutWebRTC::sendSCTPPacket(const char * data, size_t len){
    for (std::set<int>::iterator it = sctpSockets.begin(); it != sctpSockets.end(); ++it){
      if (!*(sockets[*it].udpSock)){continue;}
      sockets[*it].udpSock->sendPaced(data, len);
    }
  }

  /// Use select to wait until a packet arrives or until the next packet should be sent
  void OutWebRTC::sendPaced(uint64_t uSendWindow){
    uint64_t currPace = Util::getMicros();
    uint64_t uTime = currPace;
    do{
      uint64_t sleepTime = uSendWindow - (uTime - currPace);

      fd_set rfds;
      FD_ZERO(&rfds);
      int maxFD = mainSocket.getSock();
      FD_SET(maxFD, &rfds);

      for (std::map<int, WebRTCSocket>::iterator it = sockets.begin(); it != sockets.end(); it++){
        if (!*(it->second.udpSock)){continue;}
        uint64_t nextPace = it->second.udpSock->timeToNextPace(uTime);
        // Not sleeping? Send now!
        if (!nextPace){
          it->second.udpSock->sendPaced(0);
          nextPace = it->second.udpSock->timeToNextPace(uTime);
        }
        if (sleepTime > nextPace){sleepTime = nextPace;}

        int s = it->second.udpSock->getSock();
        FD_SET(s, &rfds);
        if (maxFD < s){maxFD = s;}
      }

      struct timeval T;
      T.tv_sec = sleepTime / 1000000;
      T.tv_usec = sleepTime % 1000000;
      int r = select(maxFD + 1, &rfds, NULL, NULL, &T);
      // If we can read the socket, immediately return and stop waiting
      if (r > 0){return;}

      uTime = Util::getMicros();
    }while(uTime - currPace < uSendWindow);
  }

  void OutWebRTC::onSCTP(const char * data, size_t len, uint16_t stream, uint32_t ppid){
#ifdef WITH_DATACHANNELS
    if (!sctpConnected){
      // We have to call accept (at least) once, otherwise the SCTP library considers our socket not connected
      // Accept blocks if there is no peer, so we do this as soon as the first message is received, which means we have a peer.
      sctp_sock = usrsctp_accept(sctp_sock, 0, 0);
      sctpConnected = true;
    }
    if (ppid == 50){
      // DCEP message. Spec: https://www.rfc-editor.org/rfc/rfc8832.html
      if (data[0] == 3){
        uint8_t chanType = data[1];
        uint32_t reliParam = Bit::btohl(data+4);
        uint16_t lblLen = Bit::btohs(data+8);
        uint16_t proLen = Bit::btohs(data+10);
        std::string chanTypeStr;
        switch (chanType){
          case 0x00: chanTypeStr = "reliable"; break;
          case 0x80: chanTypeStr = "reliable, unordered"; break;
          case 0x01: chanTypeStr = "max " + JSON::Value(reliParam).asString() + " retrans"; break;
          case 0x81: chanTypeStr = "max " + JSON::Value(reliParam).asString() + " retrans, unordered"; break;
          case 0x02: chanTypeStr = "max " + JSON::Value(reliParam).asString() + " millis"; break;
          case 0x82: chanTypeStr = "max " + JSON::Value(reliParam).asString() + " millis, unordered"; break;
        }
        std::string label(data+12, lblLen);
        std::string protocol(data+12+lblLen, proLen);
        INFO_MSG("New data channel %" PRIu16 ": %s/%s (%s)", stream, label.c_str(), protocol.c_str(), chanTypeStr.c_str());

        sctp_sndinfo sndinfo;
        sndinfo.snd_sid = stream;
        sndinfo.snd_flags = SCTP_EOR;
        sndinfo.snd_ppid = htonl(50);
        sndinfo.snd_context = 0;
        sndinfo.snd_assoc_id = 0;
        int ret = usrsctp_sendv(sctp_sock, "2", 1, NULL, 0, (void *)&sndinfo, (socklen_t)sizeof(struct sctp_sndinfo), SCTP_SENDV_SNDINFO, 0);
	if (ret < 0){
          WARN_MSG("Could not send data channel ACK, error: %s", strerror(errno));
        }else{
          if ((protocol == "JSON" || label == "JSON" || protocol == "*" || label == "*") && !dataChannels.count("JSON")){
            dataChannels["JSON"] = stream;
            while (queuedJSON.size()){
              sctp_sndinfo sndinfo;
              sndinfo.snd_sid = stream;
              sndinfo.snd_flags = SCTP_EOR;
              sndinfo.snd_ppid = htonl(51);
              sndinfo.snd_context = 0;
              sndinfo.snd_assoc_id = 0;
              int ret = usrsctp_sendv(sctp_sock, queuedJSON.begin()->data(), queuedJSON.begin()->size(), NULL, 0, (void *)&sndinfo, (socklen_t)sizeof(struct sctp_sndinfo), SCTP_SENDV_SNDINFO, 0);
              if (ret < 0){
                  WARN_MSG("Could not send data channel message: %s", strerror(errno));
              }
              queuedJSON.pop_front();
            }
          }
          if ((protocol == "subtitle" || label == "subtitle" || protocol == "*" || label == "*") && !dataChannels.count("subtitle")){
            dataChannels["subtitle"] = stream;
          }
        }
        if (packetLog.is_open()){
          packetLog << "Data channel " << stream << " opened: " << label << "/" << protocol << " (" << chanTypeStr << ")" << std::endl;
        }
      }else if (data[0] == 2){
        INFO_MSG("Data channel acknowledged by remote");
        if (packetLog.is_open()){
          packetLog << "Data channel " << stream << " acknowledged by remote" << std::endl;
        }
      }else{
        WARN_MSG("Received invalid DCEP message!");
        return;
      }
    }else if (ppid == 51){
      std::string txt(data, len);
      INFO_MSG("Received text: %s", txt.c_str());
      if (packetLog.is_open()){
        packetLog << "Received SCTP text (data stream " << stream << "): " << txt << std::endl;
      }
    }else{
      INFO_MSG("Received unknown PPID datachannel message: %" PRIu32, ppid);
      if (packetLog.is_open()){
        packetLog << "Received SCTP data (" << len << "b):" << std::endl;
        for (unsigned int i = 0; i < len; ++i){
          if (!(i % 32)){packetLog << std::endl;}
          packetLog << std::hex << std::setw(2) << std::setfill('0')
                 << (unsigned int)(data[i]) << " ";
          if ((i % 4) == 3){packetLog << " ";}
        }
        packetLog << std::dec << std::endl;
      }
    }
#endif
  }

  void OutWebRTC::onDTSCConverterHasPacket(const DTSC::Packet &pkt){
    if (!M.getBootMsOffset()){
      meta.setBootMsOffset(Util::bootMS() - pkt.getTime());
      packetOffset = 0;
      setPacketOffset = true;
    }else if (!setPacketOffset){
      packetOffset = (Util::bootMS() - pkt.getTime()) - M.getBootMsOffset();
      setPacketOffset = true;
    }

    // extract meta data (init data, width/height, etc);
    size_t idx = M.trackIDToIndex(pkt.getTrackId(), getpid());
    std::string codec = M.getCodec(idx);
    if (codec == "H264"){
      if (M.getInit(idx).empty()){
        FAIL_MSG("No init data found for track on index %zu, payloadType %zu", idx, M.getID(idx));
        return;
      }
    }

    if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << codec << " data: " << pkt.toSummary() << std::endl;}

    if (codec == "VP8" && pkt.getFlag("keyframe")){extractFrameSizeFromVP8KeyFrame(pkt);}
    if (codec == "VP9" && pkt.getFlag("keyframe")){extractFrameSizeFromVP8KeyFrame(pkt);}

    if (!M.trackValid(idx)){
      INFO_MSG("Validated track %zu in meta", idx);
      meta.validateTrack(idx);
    }
    DONTEVEN_MSG("DTSC: %s", pkt.toSummary().c_str());
    char *pktData;
    size_t pktDataLen;
    pkt.getString("data", pktData, pktDataLen);
    bufferLivePacket(pkt.getTime() + packetOffset, pkt.getInt("offset"), idx, pktData,
                     pktDataLen, 0, pkt.getFlag("keyframe"));
  }

  void OutWebRTC::onDTSCConverterHasInitData(size_t trackId, const std::string &initData){
    size_t idx = M.trackIDToIndex(trackId, getpid());
    if (idx == INVALID_TRACK_ID || !webrtcTracks.count(idx)){
      ERROR_MSG(
          "Recieved init data for a track that we don't manage. TrackID %zu /PayloadType: %zu",
          idx, M.getID(idx));
      return;
    }

    MP4::AVCC avccbox;
    avccbox.setPayload(initData);
    if (avccbox.getSPSLen() == 0 || avccbox.getPPSLen() == 0){
      WARN_MSG("Received init data, but partially. SPS nbytes: %u, PPS nbytes: %u.",
               avccbox.getSPSLen(), avccbox.getPPSLen());
      return;
    }

    h264::sequenceParameterSet sps(avccbox.getSPS(), avccbox.getSPSLen());
    h264::SPSMeta hMeta = sps.getCharacteristics();

    meta.setWidth(idx, hMeta.width);
    meta.setHeight(idx, hMeta.height);
    meta.setFpks(idx, hMeta.fps * 1000);

    avccbox.multiplyPPS(57); // Inject all possible PPS packets into init
    meta.setInit(idx, avccbox.payload(), avccbox.payloadSize());
  }

  void OutWebRTC::onRTPSorterHasPacket(size_t trackId, const RTP::Packet &pkt){
    size_t idx = M.trackIDToIndex(trackId, getpid());
    if (idx == INVALID_TRACK_ID || !webrtcTracks.count(idx)){
      ERROR_MSG("Received a sorted RTP packet for payload %zu (idx %zu) but we don't manage this track.", trackId, idx);
      return;
    }
    if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "Sorted packet type " << pkt.getPayloadType() << " #" << pkt.getSequence() << std::endl;}
    webrtcTracks[idx].rtpToDTSC.addRTP(pkt);
  }

  // This function will be called when we're sending data
  // to the browser (other peer).
  void OutWebRTC::onRTPPacketizerHasRTPPacket(const char *data, size_t nbytes){

    rtpOutBuffer.allocate(nbytes + 256);

    for (std::set<int>::iterator it = rtpSockets.begin(); it != rtpSockets.end(); ++it){
      if (!*(sockets[*it].udpSock)){continue;}
      rtpOutBuffer.assign(data, nbytes);
      int protectedSize = nbytes;
      if (doDTLS){
        if (sockets[*it].srtpWriter.protectRtp((uint8_t *)(void *)rtpOutBuffer, &protectedSize) != 0){
          ERROR_MSG("Failed to protect the RTP message.");
          return;
        }
      }
      
      sockets[*it].udpSock->sendPaced(rtpOutBuffer, (size_t)protectedSize, false);
      myConn.addUp(protectedSize);
      RTP::Packet tmpPkt(rtpOutBuffer, protectedSize);
      uint32_t pSSRC = tmpPkt.getSSRC();
      uint16_t seq = tmpPkt.getSequence();
      if (packetLog.is_open()){
        packetLog << "[" << Util::bootMS() << "]" << "Sending RTP packet #" << seq << " to socket " << sockets[*it].udpSock->getSock() << std::endl;
      }
      sockets[*it].outBuffers[pSSRC].assign(seq, rtpOutBuffer, protectedSize);
      totalPkts++;

      if (volkswagenMode){sockets[*it].srtpWriter.protectRtp((uint8_t *)(void *)rtpOutBuffer, &protectedSize);}
    }
  }

  void OutWebRTC::onRTPPacketizerHasRTCPPacket(const char *data, uint32_t nbytes){
    if (nbytes > 2048){
      FAIL_MSG("The received RTCP packet is too big to handle.");
      return;
    }
    if (!data){
      FAIL_MSG("Invalid RTCP packet given.");
      return;
    }
    for (std::set<int>::iterator it = rtpSockets.begin(); it != rtpSockets.end(); ++it){
      if (!*(sockets[*it].udpSock)){continue;}
      myConn.addUp(sockets[*it].sendRTCP(data, nbytes));
    }
  }

  void OutWebRTC::sendNext(){
    HTTPOutput::sendNext();
    // first make sure that we complete the DTLS handshake.
    if(doDTLS){
      while (keepGoing() && !rtpSockets.size()){
        if (!handleWebRTCInputOutput()){sendPaced(10);}else{sendPaced(0);}
        if (lastRecv < Util::bootMS() - 10000){
          INFO_MSG("Killing idle connection in handshake phase");
          onFail("idle connection in handshake phase", false);
          return;
        }
      }
    }
    if (lastRecv < Util::bootMS() - 10000){
      INFO_MSG("Killing idle connection");
      onFail("idle connection", false);
      return;
    }

    // Handle nice move-over to new track ID
    if (prevVidTrack != INVALID_TRACK_ID && thisIdx != prevVidTrack && M.getType(thisIdx) == "video"){
      if (!thisPacket.getFlag("keyframe")){
        // Ignore the packet if not a keyframe
        return;
      }
      dropTrack(prevVidTrack, "Smoothly switching to new video track", false);
      prevVidTrack = INVALID_TRACK_ID;
      repeatInit = true;
      firstKey = true;
      onIdle();
    }

    if (M.getLive() && stayLive && lastTimeSync + 666 < thisTime){
      lastTimeSync = thisTime;
      if (liveSeek()){return;}
    }

    // once the DTLS handshake has been done, we still have to
    // deal with STUN consent messages and RTCP.
    handleWebRTCInputOutput();

    char *dataPointer = 0;
    size_t dataLen = 0;
    thisPacket.getString("data", dataPointer, dataLen);


    if (M.getType(thisIdx) == "meta"){
#ifdef WITH_DATACHANNELS
      JSON::Value jPack;
      if (M.getCodec(thisIdx) == "JSON"){
        if (dataLen == 0 || (dataLen == 1 && dataPointer[0] == ' ')){return;}
        jPack["data"] = JSON::fromString(dataPointer, dataLen);
        jPack["time"] = thisTime;
        jPack["track"] = (uint64_t)thisIdx;
      }else if (M.getCodec(thisIdx) == "subtitle"){
        //Ignore blank subtitles
        if (dataLen == 0 || (dataLen == 1 && dataPointer[0] == ' ')){return;}

        //Get duration, or calculate if missing
        uint64_t duration = thisPacket.getInt("duration");
        if (!duration){duration = dataLen * 75 + 800;}

        //Build JSON data to transmit
        jPack["duration"] = duration;
        jPack["time"] = thisTime;
        jPack["track"] = (uint64_t)thisIdx;
        jPack["data"] = std::string(dataPointer, dataLen);
      }else{
        jPack = thisPacket.toJSON();
        jPack.removeMember("bpos");
        jPack["generic_converter_used"] = true;
      }
      std::string packed = jPack.toString();

      if (dataChannels.count(M.getCodec(thisIdx))){
        sctp_sndinfo sndinfo;
        sndinfo.snd_sid = dataChannels[M.getCodec(thisIdx)];
        sndinfo.snd_flags = SCTP_EOR;
        sndinfo.snd_ppid = htonl(51);
        sndinfo.snd_context = 0;
        sndinfo.snd_assoc_id = 0;
        int ret = usrsctp_sendv(sctp_sock, packed.data(), packed.size(), NULL, 0, (void *)&sndinfo, (socklen_t)sizeof(struct sctp_sndinfo), SCTP_SENDV_SNDINFO, 0);
        if (ret < 0){
            WARN_MSG("Could not send data channel message: %s", strerror(errno));
        }
      }else{
        if (M.getCodec(thisIdx) == "JSON"){
          queuedJSON.push_back(packed);
        }else{
          WARN_MSG("I don't have a data channel for %s data!", M.getCodec(thisIdx).c_str());
        }
      }
#endif
      return;
    }

    // make sure the webrtcTracks were setup correctly for output.
    uint32_t tid = thisIdx;

    WebRTCTrack *trackPointer = 0;

    // If we see this is audio or video, use the webrtc track we negotiated
    if (M.getType(tid) == "video" && webrtcTracks.count(vidTrack)){
      trackPointer = &webrtcTracks[vidTrack];

      if (lastPackMs){
        uint64_t newMs = Util::bootMS();
        jitterLog << (newMs - lastPackMs) << std::endl;
        lastPackMs = newMs;
      }


    }
    if (M.getType(tid) == "audio" && webrtcTracks.count(audTrack)){
      trackPointer = &webrtcTracks[audTrack];
    }

    // If we negotiated this track, always use it directly.
    if (webrtcTracks.count(tid)){trackPointer = &webrtcTracks[tid];}

    // Abort if no negotiation happened
    if (!trackPointer){
      WARN_MSG("Don't know how to send data for track %" PRIu32, tid);
      return;
    }

    WebRTCTrack &rtcTrack = *trackPointer;
    double mult = SDP::getMultiplier(&M, thisIdx);
    // This checks if we have a whole integer multiplier, and if so,
    // ensures only integer math is used to prevent rounding errors
    if (mult == (uint64_t)mult){
      rtcTrack.rtpPacketizer.setTimestamp(thisTime * (uint64_t)mult);
    }else{
      rtcTrack.rtpPacketizer.setTimestamp(thisTime * mult);
    }

    bool isKeyFrame = thisPacket.getFlag("keyframe");
    didReceiveKeyFrame = isKeyFrame;
    if (M.getCodec(thisIdx) == "H264"){
      if (isKeyFrame && firstKey){
        size_t offset = 0;
        while (offset + 4 < dataLen){
          size_t nalLen = Bit::btohl(dataPointer + offset);
          uint8_t nalType = dataPointer[offset + 4] & 0x1F;
          if (nalType == 7 || nalType == 8){// Init data already provided in-band, skip repeating
                                              // it.
            repeatInit = false;
            break;
          }
          offset += 4 + nalLen;
        }
        firstKey = false;
      }
      if (repeatInit && isKeyFrame){sendSPSPPS(thisIdx, rtcTrack);}
    }

    rtcTrack.rtpPacketizer.sendData(0, onRTPPacketizerHasDataCallback, dataPointer, dataLen,
                                    rtcTrack.payloadType, M.getCodec(thisIdx));

    //Trigger a re-send of the Sender Report for every track every ~250ms
    if (lastSR+250 < Util::bootMS()){
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        mustSendSR.insert(it->first);
      }
      lastSR = Util::bootMS();
    }
    //If this track hasn't sent yet, actually sent
    if (mustSendSR.count(thisIdx)){
      mustSendSR.erase(thisIdx);
      rtcTrack.rtpPacketizer.sendRTCP_SR(0, 0, onRTPPacketizerHasRTCPDataCallback);
    }
  }

  // When the RTP::toDTSC converter collected a complete VP8
  // frame, it wil call our callback `onDTSCConverterHasPacket()`
  // with a valid packet that can be fed into
  // MistServer. Whenever we receive a keyframe we update the
  // width and height of the corresponding track.
  void OutWebRTC::extractFrameSizeFromVP8KeyFrame(const DTSC::Packet &pkt){

    char *vp8PayloadBuffer = 0;
    size_t vp8PayloadLen = 0;
    pkt.getString("data", vp8PayloadBuffer, vp8PayloadLen);

    if (!vp8PayloadBuffer || vp8PayloadLen < 9){
      FAIL_MSG("Cannot extract vp8 frame size. Failed to get data.");
      return;
    }

    if (vp8PayloadBuffer[3] != 0x9d || vp8PayloadBuffer[4] != 0x01 || vp8PayloadBuffer[5] != 0x2a){
      FAIL_MSG("Invalid signature. It seems that either the VP8 frames is incorrect or our parsing "
               "is wrong.");
      return;
    }

    uint32_t width = ((vp8PayloadBuffer[7] << 8) + vp8PayloadBuffer[6]) & 0x3FFF;
    uint32_t height = ((vp8PayloadBuffer[9] << 8) + vp8PayloadBuffer[8]) & 0x3FFF;

    DONTEVEN_MSG("Recieved VP8 keyframe with resolution: %u x %u", width, height);

    if (width == 0){
      FAIL_MSG("VP8 frame width is 0; parse error?");
      return;
    }

    if (height == 0){
      FAIL_MSG("VP8 frame height is 0; parse error?");
      return;
    }

    size_t idx = M.trackIDToIndex(pkt.getTrackId(), getpid());
    if (idx == INVALID_TRACK_ID){
      FAIL_MSG("No track found with ID %zu.", pkt.getTrackId());
      return;
    }

    meta.setWidth(idx, width);
    meta.setHeight(idx, height);
  }

  void OutWebRTC::sendRTCPFeedbackREMB(const WebRTCTrack &rtcTrack){
    // create the `BR Exp` and `BR Mantissa parts.
    uint32_t br_exponent = 0;
    uint32_t br_mantissa = videoConstraint;
    while (br_mantissa > 0x3FFFF){
      br_mantissa >>= 1;
      ++br_exponent;
    }

    std::vector<uint8_t> buffer;
    buffer.push_back(0x80 | 0x0F);         // V =2 (0x80) | FMT=15 (0x0F)
    buffer.push_back(0xCE);                // payload type = 206
    buffer.push_back(0x00);                // tmp length
    buffer.push_back(0x00);                // tmp length
    buffer.push_back((SSRC >> 24) & 0xFF); // ssrc of sender
    buffer.push_back((SSRC >> 16) & 0xFF); // ssrc of sender
    buffer.push_back((SSRC >> 8) & 0xFF);  // ssrc of sender
    buffer.push_back((SSRC)&0xFF);         // ssrc of sender
    buffer.push_back(0x00);                // ssrc of media source (always 0)
    buffer.push_back(0x00);                // ssrc of media source (always 0)
    buffer.push_back(0x00);                // ssrc of media source (always 0)
    buffer.push_back(0x00);                // ssrc of media source (always 0)
    buffer.push_back('R');                 // `R`, `E`, `M`, `B`
    buffer.push_back('E');                 // `R`, `E`, `M`, `B`
    buffer.push_back('M');                 // `R`, `E`, `M`, `B`
    buffer.push_back('B');                 // `R`, `E`, `M`, `B`
    buffer.push_back(0x01);                // num ssrc
    buffer.push_back((uint8_t)(br_exponent << 2) + ((br_mantissa >> 16) & 0x03)); // br-exp and br-mantissa
    buffer.push_back((uint8_t)(br_mantissa >> 8));  // br-exp and br-mantissa
    buffer.push_back((uint8_t)br_mantissa);         // br-exp and br-mantissa
    buffer.push_back((rtcTrack.SSRC >> 24) & 0xFF); // ssrc to which this remb packet applies to.
    buffer.push_back((rtcTrack.SSRC >> 16) & 0xFF); // ssrc to which this remb packet applies to.
    buffer.push_back((rtcTrack.SSRC >> 8) & 0xFF);  // ssrc to which this remb packet applies to.
    buffer.push_back((rtcTrack.SSRC) & 0xFF);       // ssrc to which this remb packet applies to.

    // rewrite size
    int buffer_size_in_bytes = (int)buffer.size();
    int buffer_size_in_words_minus1 = ((int)buffer.size() / 4) - 1;
    buffer[2] = (buffer_size_in_words_minus1 >> 8) & 0xFF;
    buffer[3] = buffer_size_in_words_minus1 & 0xFF;

    // protect.
    size_t trailer_space = SRTP_MAX_TRAILER_LEN + 4;
    for (size_t i = 0; i < trailer_space; ++i){buffer.push_back(0x00);}

    for (std::set<int>::iterator it = rtpSockets.begin(); it != rtpSockets.end(); ++it){
      if (!*(sockets[*it].udpSock)){continue;}
      myConn.addUp(sockets[*it].sendRTCP((const char *)&buffer[0], buffer_size_in_bytes));
    }
  }

  void OutWebRTC::sendRTCPFeedbackPLI(const WebRTCTrack &rtcTrack){

    std::vector<uint8_t> buffer;
    buffer.push_back(0x80 | 0x01);                  // V=2 (0x80) | FMT=1 (0x01)
    buffer.push_back(0xCE);                         // payload type = 206
    buffer.push_back(0x00);                         // payload size in words minus 1 (2)
    buffer.push_back(0x02);                         // payload size in words minus 1 (2)
    buffer.push_back((SSRC >> 24) & 0xFF);          // ssrc of sender
    buffer.push_back((SSRC >> 16) & 0xFF);          // ssrc of sender
    buffer.push_back((SSRC >> 8) & 0xFF);           // ssrc of sender
    buffer.push_back((SSRC)&0xFF);                  // ssrc of sender
    buffer.push_back((rtcTrack.SSRC >> 24) & 0xFF); // ssrc of receiver
    buffer.push_back((rtcTrack.SSRC >> 16) & 0xFF); // ssrc of receiver
    buffer.push_back((rtcTrack.SSRC >> 8) & 0xFF);  // ssrc of receiver
    buffer.push_back((rtcTrack.SSRC) & 0xFF);       // ssrc of receiver

    // protect.
    int buffer_size_in_bytes = (int)buffer.size();

    // space for protection
    size_t trailer_space = SRTP_MAX_TRAILER_LEN + 4;
    for (size_t i = 0; i < trailer_space; ++i){buffer.push_back(0x00);}

    for (std::set<int>::iterator it = rtpSockets.begin(); it != rtpSockets.end(); ++it){
      if (!*(sockets[*it].udpSock)){continue;}
      myConn.addUp(sockets[*it].sendRTCP((const char *)&buffer[0], buffer_size_in_bytes));
    }
  }

  // Notify sender that we lost a packet. See
  // https://tools.ietf.org/html/rfc4585#section-6.1 which
  // describes the use of the `BLP` field; when more successive
  // sequence numbers are lost it makes sense to implement this
  // too.
  void OutWebRTC::sendRTCPFeedbackNACK(const WebRTCTrack &rtcTrack, uint16_t lostSequenceNumber){
    VERYHIGH_MSG("Requesting missing sequence number %u", lostSequenceNumber);

    std::vector<uint8_t> buffer;
    buffer.push_back(0x80 | 0x01); // V=2 (0x80) | FMT=1 (0x01)
    buffer.push_back(0xCD); // payload type = 205, RTPFB, https://tools.ietf.org/html/rfc4585#section-6.1
    buffer.push_back(0x00);                             // payload size in words minus 1 (3)
    buffer.push_back(0x03);                             // payload size in words minus 1 (3)
    buffer.push_back((SSRC >> 24) & 0xFF);              // ssrc of sender
    buffer.push_back((SSRC >> 16) & 0xFF);              // ssrc of sender
    buffer.push_back((SSRC >> 8) & 0xFF);               // ssrc of sender
    buffer.push_back((SSRC)&0xFF);                      // ssrc of sender
    buffer.push_back((rtcTrack.SSRC >> 24) & 0xFF);     // ssrc of receiver
    buffer.push_back((rtcTrack.SSRC >> 16) & 0xFF);     // ssrc of receiver
    buffer.push_back((rtcTrack.SSRC >> 8) & 0xFF);      // ssrc of receiver
    buffer.push_back((rtcTrack.SSRC) & 0xFF);           // ssrc of receiver
    buffer.push_back((lostSequenceNumber >> 8) & 0xFF); // PID: missing sequence number
    buffer.push_back((lostSequenceNumber)&0xFF);        // PID: missing sequence number
    buffer.push_back(0x00); // BLP: Bitmask of following losses. (not implemented atm).
    buffer.push_back(0x00); // BLP: Bitmask of following losses. (not implemented atm).

    // protect.
    int buffer_size_in_bytes = (int)buffer.size();

    // space for protection
    size_t trailer_space = SRTP_MAX_TRAILER_LEN + 4;
    for (size_t i = 0; i < trailer_space; ++i){buffer.push_back(0x00);}

    for (std::set<int>::iterator it = rtpSockets.begin(); it != rtpSockets.end(); ++it){
      if (!*(sockets[*it].udpSock)){continue;}
      myConn.addUp(sockets[*it].sendRTCP((const char *)&buffer[0], buffer_size_in_bytes));
    }
  }

  void OutWebRTC::sendRTCPFeedbackRR(WebRTCTrack &rtcTrack){
    if ((rtcTrack.sorter.lostCurrent + rtcTrack.sorter.packCurrent) < 1){
      stats_lossperc = 100.0;
    }else{
      stats_lossperc = (double)(rtcTrack.sorter.lostCurrent * 100.) / (double)(rtcTrack.sorter.lostCurrent + rtcTrack.sorter.packCurrent);
    }
    stats_jitter = rtcTrack.jitter/rtcTrack.rtpToDTSC.multiplier;
    stats_lossnum = rtcTrack.sorter.lostTotal;
    totalLoss = stats_lossnum;

    //Print stats at appropriate log levels
    if (stats_lossperc > 1 || stats_jitter > 20){
      INFO_MSG("Receiver Report (%s): %.2f%% loss, %" PRIu32 " total lost, %.2f ms jitter", rtcTrack.rtpToDTSC.codec.c_str(), stats_lossperc, rtcTrack.sorter.lostTotal, stats_jitter);
    }else{
      HIGH_MSG("Receiver Report (%s): %.2f%% loss, %" PRIu32 " total lost, %.2f ms jitter", rtcTrack.rtpToDTSC.codec.c_str(), stats_lossperc, rtcTrack.sorter.lostTotal, stats_jitter);
    }

    //Calculate loss percentage average over moving window
    stats_loss_avg.push_back(stats_lossperc);
    while (stats_loss_avg.size() > 5){stats_loss_avg.pop_front();}
    if (stats_loss_avg.size()){
      double curr_avg_loss = 0;
      for (std::deque<double>::iterator it = stats_loss_avg.begin(); it != stats_loss_avg.end(); ++it){
        curr_avg_loss += *it;
      }
      curr_avg_loss /= stats_loss_avg.size();

      uint32_t preConstraint = videoConstraint;

      if (curr_avg_loss > 50){
        //If we have > 50% loss, constrain video by 9%
        videoConstraint *= 0.91;
      }else if (curr_avg_loss > 10){
        //If we have > 10% loss, constrain video by 5%
        videoConstraint *= 0.95;
      }else if (curr_avg_loss > 5){
        //If we have > 5% loss, constrain video by 1%
        videoConstraint *= 0.99;
      }

      //Do not reduce under 32 kbps
      if (videoConstraint < 1024*32){videoConstraint = 1024*32;}

      if (!noSignalling && videoConstraint != preConstraint){
        INFO_MSG("Reduced video bandwidth maximum to %" PRIu32 " because average loss is %.2f", videoConstraint, curr_avg_loss);
        JSON::Value commandResult;
        commandResult["type"] = "on_video_bitrate";
        commandResult["result"] = true;
        commandResult["video_bitrate"] = videoBitrate;
        commandResult["video_bitrate_constraint"] = videoConstraint;
        webSock->sendFrame(commandResult.toString());
      }
    }


    if (packetLog.is_open()){
      packetLog << "[" << Util::bootMS() << "] Receiver Report (" << rtcTrack.rtpToDTSC.codec << "): " << stats_lossperc << " percent loss, " << rtcTrack.sorter.lostTotal << " total lost, " << stats_jitter << " ms jitter" << std::endl;
    }
    ((RTP::FECPacket *)&(rtcTrack.rtpPacketizer))->sendRTCP_RR(rtcTrack.sorter, SSRC, rtcTrack.SSRC, 0, onRTPPacketizerHasRTCPDataCallback, (uint32_t)rtcTrack.jitter);
  }

  void OutWebRTC::sendSPSPPS(size_t dtscIdx, WebRTCTrack &rtcTrack){

    if (M.getInit(dtscIdx).empty()){
      WARN_MSG("No init data found in the DTSC::Track. Not sending SPS and PPS");
      return;
    }

    std::vector<char> buf;
    MP4::AVCC avcc;
    avcc.setPayload(M.getInit(dtscIdx));

    /* SPS */
    for (uint32_t i = 0; i < avcc.getSPSCount(); ++i){

      uint32_t len = avcc.getSPSLen(i);
      if (len == 0){
        WARN_MSG("Empty SPS stored?");
        continue;
      }

      buf.clear();
      buf.assign(4, 0);
      *(uint32_t *)&buf[0] = htonl(len);
      std::copy(avcc.getSPS(i), avcc.getSPS(i) + avcc.getSPSLen(i), std::back_inserter(buf));

      rtcTrack.rtpPacketizer.sendData(0, onRTPPacketizerHasDataCallback, &buf[0], buf.size(),
                                      rtcTrack.payloadType, M.getCodec(dtscIdx));
    }

    /* PPS */
    for (uint32_t i = 0; i < avcc.getPPSCount(); ++i){

      uint32_t len = avcc.getPPSLen(i);
      if (len == 0){
        WARN_MSG("Empty PPS stored?");
        continue;
      }

      buf.clear();
      buf.assign(4, 0);
      *(uint32_t *)&buf[0] = htonl(len);
      std::copy(avcc.getPPS(i), avcc.getPPS(i) + avcc.getPPSLen(i), std::back_inserter(buf));

      rtcTrack.rtpPacketizer.sendData(0, onRTPPacketizerHasDataCallback, &buf[0], buf.size(),
                                      rtcTrack.payloadType, M.getCodec(dtscIdx));
    }
  }

}// namespace Mist
