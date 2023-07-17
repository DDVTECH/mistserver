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

namespace Mist{

  OutWebRTC *classPointer = 0;

  /* ------------------------------------------------ */

  static uint32_t generateSSRC();
  static void webRTCInputOutputThreadFunc(void *arg);
  static int onDTLSHandshakeWantsToWriteCallback(const uint8_t *data, int *nbytes);
  static void onDTSCConverterHasPacketCallback(const DTSC::Packet &pkt);
  static void onDTSCConverterHasInitDataCallback(const uint64_t track, const std::string &initData);
  static void onRTPSorterHasPacketCallback(const uint64_t track,
                                           const RTP::Packet &p); // when we receive RTP packets we store them in a sorter. Whenever there is a valid,
                                                                  // sorted RTP packet that can be used this function is called.
  static void onRTPPacketizerHasDataCallback(void *socket, const char *data, size_t len, uint8_t channel);
  static void onRTPPacketizerHasRTCPDataCallback(void *socket, const char *data, size_t nbytes, uint8_t channel);

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
    stayLive = true;
    target_rate = 0.0;
    firstKey = true;
    repeatInit = true;

    lastTimeSync = 0;
    maxSkipAhead = 0;
    needsLookAhead = 0;
    webRTCInputOutputThread = NULL;
    udpPort = 0;
    SSRC = generateSSRC();
    rtcpTimeoutInMillis = 0;
    rtcpKeyFrameDelayInMillis = 2000;
    rtcpKeyFrameTimeoutInMillis = 0;
    videoBitrate = 6 * 1000 * 1000;
    videoConstraint = videoBitrate;
    RTP::MAX_SEND = 1350 - 28;
    didReceiveKeyFrame = false;
    doDTLS = true;
    volkswagenMode = false;
    syncedNTPClock = false;

   
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

    if (dtlsHandshake.init(&cert.cert, &cert.key, onDTLSHandshakeWantsToWriteCallback) != 0){
      onFail("Failed to initialize the dtls-srtp handshake helper.", true);
      return;
    }
    sdpAnswer.setFingerprint(cert.getFingerprintSha256());

    classPointer = this;

    setBlocking(false);
  }

  OutWebRTC::~OutWebRTC(){

    if (webRTCInputOutputThread && webRTCInputOutputThread->joinable()){
      webRTCInputOutputThread->join();
      delete webRTCInputOutputThread;
      webRTCInputOutputThread = NULL;
    }

    if (srtpReader.shutdown() != 0){FAIL_MSG("Failed to cleanly shutdown the srtp reader.");}
    if (srtpWriter.shutdown() != 0){FAIL_MSG("Failed to cleanly shutdown the srtp writer.");}
    if (dtlsHandshake.shutdown() != 0){
      FAIL_MSG("Failed to cleanly shutdown the dtls handshake.");
    }
  }

  // Initialize the WebRTC output. This is where we define what
  // codes types are supported and accepted when parsing the SDP
  // offer or when generating the SDP. The `capa` member is
  // inherited from `Output`.
  void OutWebRTC::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "WebRTC";
    capa["desc"] = "Provides WebRTC output";
    capa["url_rel"] = "/webrtc/$";
    capa["url_match"] = "/webrtc/$";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("VP8");
    capa["codecs"][0u][0u].append("VP9");
    capa["codecs"][0u][1u].append("opus");
    capa["codecs"][0u][1u].append("ALAW");
    capa["codecs"][0u][1u].append("ULAW");
    capa["methods"][0u]["handler"] = "ws";
    capa["methods"][0u]["type"] = "webrtc";
    capa["methods"][0u]["hrn"] = "WebRTC";
    capa["methods"][0u]["priority"] = 2;
    capa["methods"][0u]["nobframes"] = 1;

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
      if (!parseData){udp.sendPaced(10000);}
      //After 10s of no packets, abort
      if (Util::bootMS() > lastRecv + 10000){
        Util::logExitReason("received no data for 10+ seconds");
        config->is_active = false;
      }
      return;
    }
    HTTPOutput::requestHandler();
  }

  void OutWebRTC::respondHTTP(const HTTP::Parser & req, bool headersOnly){
    // Check for WHIP payload
    if (req.method == "OPTIONS"){
      H.setCORSHeaders();
      H.StartResponse("200", "All good", req, myConn);
      H.Chunkify(0, 0, myConn);
    }
    if (req.method == "POST"){
      if (req.GetHeader("Content-Type") == "application/sdp"){
        SDP::Session sdpParser;
        const std::string &offerStr = req.body;
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
        srtpWriter.init("SRTP_AES128_CM_SHA1_80", "volkswagen modus", "volkswagenmode");
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

    std::set<size_t> validTracks = M.getValidTracks();
    if (command["type"] == "tracks"){
      if (command.isMember("audio")){
        if (!command["audio"].isNull()){
          targetParams["audio"] = command["audio"].asString();
          if (audTrack && command["audio"].asInt()){
            uint64_t tId = command["audio"].asInt();
            if (validTracks.count(tId) && M.getCodec(tId) != M.getCodec(audTrack)){
              targetParams["audio"] = "none";
              sendSignalingError("tracks", "Cannot select track because it is encoded as " +
                                               M.getCodec(tId) + " but the already negotiated track is " +
                                               M.getCodec(audTrack) + ". Please re-negotiate to play this track.");
            }
          }
        }else{
          targetParams.erase("audio");
        }
      }
      if (command.isMember("video")){
        if (!command["video"].isNull()){
          targetParams["video"] = command["video"].asString();
          if (vidTrack && command["video"].asInt()){
            uint64_t tId = command["video"].asInt();
            if (validTracks.count(tId) && M.getCodec(tId) != M.getCodec(vidTrack)){
              targetParams["video"] = "none";
              sendSignalingError("tracks", "Cannot select track because it is encoded as " +
                                               M.getCodec(tId) + " but the already negotiated track is " +
                                               M.getCodec(vidTrack) + ". Please re-negotiate to play this track.");
            }
          }
        }else{
          targetParams.erase("video");
        }
      }
      // Remember the previous video track, if any.
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        if (M.getType(it->first) == "video"){
          prevVidTrack = it->first;
          break;
        }
      }
      selectDefaultTracks();
      // Add the previous video track back, if we had one.
      if (prevVidTrack != INVALID_TRACK_ID && !userSelect.count(prevVidTrack)){
        uint64_t seekTarget = currentTime();
        userSelect[prevVidTrack].reload(streamName, prevVidTrack);
        seek(seekTarget);
      }
      onIdle();
      return;
    }

    if (command["type"] == "seek"){
      if (!command.isMember("seek_time")){
        sendSignalingError("on_seek", "Received a seek request but no `seek_time` property.");
        return;
      }
      uint64_t seek_time = command["seek_time"].asInt();
      if (!parseData){
        parseData = true;
        selectDefaultTracks();
      }
      stayLive = (target_rate == 0.0) && (endTime() < seek_time + 5000);
      if (command["seek_time"].asStringRef() == "live"){stayLive = true;}
      if (stayLive){seek_time = endTime();}
      seek(seek_time, true);
      JSON::Value commandResult;
      commandResult["type"] = "on_seek";
      commandResult["result"] = true;
      if (M.getLive()){commandResult["live_point"] = stayLive;}
      if (target_rate == 0.0){
        commandResult["play_rate_curr"] = "auto";
      }else{
        commandResult["play_rate_curr"] = target_rate;
      }
      webSock->sendFrame(commandResult.toString());
      onIdle();
      return;
    }

    if (command["type"] == "set_speed"){
      if (!command.isMember("play_rate")){
        sendSignalingError("on_speed", "Received a playback speed setting request but no `play_rate` property.");
        return;
      }
      double set_rate = command["play_rate"].asDouble();
      if (!parseData){
        parseData = true;
        selectDefaultTracks();
      }
      JSON::Value commandResult;
      commandResult["type"] = "on_speed";
      if (target_rate == 0.0){
        commandResult["play_rate_prev"] = "auto";
      }else{
        commandResult["play_rate_prev"] = target_rate;
      }
      if (set_rate == 0.0){
        commandResult["play_rate_curr"] = "auto";
      }else{
        commandResult["play_rate_curr"] = set_rate;
      }
      if (target_rate != set_rate){
        target_rate = set_rate;
        if (target_rate == 0.0){
          realTime = 1000;//set playback speed to default
          firstTime = Util::bootMS() - currentTime();
          maxSkipAhead = 0;//enabled automatic rate control
        }else{
          stayLive = false;
          //Set new realTime speed
          realTime = 1000 / target_rate;
          firstTime = Util::bootMS() - (currentTime() / target_rate);
          maxSkipAhead = 1;//disable automatic rate control
        }
      }
      if (M.getLive()){commandResult["live_point"] = stayLive;}
      webSock->sendFrame(commandResult.toString());
      onIdle();
      return;
    }

    if (command["type"] == "pause"){
      parseData = !parseData;
      JSON::Value commandResult;
      commandResult["type"] = "on_time";
      commandResult["paused"] = !parseData;
      commandResult["current"] = currentTime();
      commandResult["begin"] = startTime();
      commandResult["end"] = endTime();
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        commandResult["tracks"].append((uint64_t)it->first);
      }
      webSock->sendFrame(commandResult.toString());
      return;
    }

    if (command["type"] == "hold") {
      parseData = false;
      JSON::Value commandResult;
      commandResult["type"] = "on_time";
      commandResult["paused"] = !parseData;
      commandResult["current"] = currentTime();
      commandResult["begin"] = startTime();
      commandResult["end"] = endTime();
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        commandResult["tracks"].append((uint64_t)it->first);
      }
      webSock->sendFrame(commandResult.toString());
      return;
    }

    if (command["type"] == "stop"){
      INFO_MSG("Received stop() command.");
      myConn.close();
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
    }

    sdpAnswer.setDirection("sendonly");

    // setup video WebRTC Track.
    if (vidTrack != INVALID_TRACK_ID){
      if (sdpAnswer.enableVideo(M.getCodec(vidTrack))){
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
      if (sdpAnswer.enableAudio(M.getCodec(audTrack))){
        WebRTCTrack &audioTrack = webrtcTracks[audTrack];
        if (!createWebRTCTrackFromAnswer(sdpAnswer.answerAudioMedia, sdpAnswer.answerAudioFormat, audioTrack)){
          FAIL_MSG("Failed to create the WebRTCTrack for the selected audio.");
          webrtcTracks.erase(audTrack);
          return false;
        }
        audioTrack.rtpPacketizer = RTP::Packet(audioTrack.payloadType, rand(), 0, audioTrack.SSRC, 0);
      }
    }

    // this is necessary so that we can get the remote IP when creating STUN replies.
    udp.allocateDestination();

    // we set parseData to `true` to start the data flow. Is also
    // used to break out of our loop in `onHTTP()`.
    parseData = true;
    idleInterval = 1000;

    return true;
  }

  void OutWebRTC::onIdle(){
    if (parseData){
      JSON::Value commandResult;
      commandResult["type"] = "on_time";
      commandResult["current"] = currentTime();
      commandResult["begin"] = startTime();
      commandResult["end"] = endTime();
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        commandResult["tracks"].append((uint64_t)it->first);
      }
      webSock->sendFrame(commandResult.toString());
    }else if (isPushing()){
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
    }
  }

  bool OutWebRTC::onFinish(){
    if (parseData){
      JSON::Value commandResult;
      commandResult["type"] = "on_stop";
      commandResult["current"] = currentTime();
      commandResult["begin"] = startTime();
      commandResult["end"] = endTime();
      webSock->sendFrame(commandResult.toString());
      parseData = false;
    }
    return true;
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
    if (formatAnswer.payloadType == SDP_PAYLOAD_TYPE_NONE){
      FAIL_MSG("Cannot create a WebRTCTrack, the given SDP::MediaFormat has no `payloadType` set.");
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
  }

  // This function is called to handle an offer from a peer that wants to push data towards us.
  bool OutWebRTC::handleSignalingCommandRemoteOfferForInput(SDP::Session &sdpSession){


    if (!meta.getBootMsOffset()){meta.setBootMsOffset(Util::bootMS());}

    if (webRTCInputOutputThread != NULL){
      FAIL_MSG("It seems that we're already have a webrtc i/o thread running.");
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
    if (sdpAnswer.enableVideo(prefVideoCodec)){

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
    if (sdpAnswer.enableAudio(prefAudioCodec)){

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
    webRTCInputOutputThread = new tthread::thread(webRTCInputOutputThreadFunc, NULL);

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
      udpPort = udp.bind(port, bindAddr);
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
        WARN_MSG("UDP bind to best guess failed - using same address as incoming connection as a last resort");
        bindAddr.clear();
      }else{
        udpPort = udp.bind(port, bindAddr);
        if (!udpPort){
          WARN_MSG("UDP bind to best guess failed - using same address as incoming connection as a last resort");
          bindAddr.clear();
        }else{
          INFO_MSG("Bound to public UDP bind address derived from hostname");
        }
      }
    }
    if (!bindAddr.size()){
      bindAddr = myConn.getBoundAddress();
      udpPort = udp.bind(port, bindAddr);
      if (!udpPort){
        FAIL_MSG("UDP bind to connected address failed - we're out of options here, I'm afraid...");
        bindAddr.clear();
      }else{
        INFO_MSG("Bound to same UDP address as TCP address - this is potentially wrong, but used as a last resort");
      }
    }

    Util::Procs::socketList.insert(udp.getSock());
    if (config && config->hasOption("pubhost") && config->getString("pubhost").size()){
      bindAddr = config->getString("pubhost");
    }
    sdpAnswer.setCandidate(bindAddr, udpPort);
    return true;
  }

  /* ------------------------------------------------ */

  // This function is called from the `webRTCInputOutputThreadFunc()`
  // function. The `webRTCInputOutputThreadFunc()` is basically empty
  // and all work for the thread is done here.
  void OutWebRTC::handleWebRTCInputOutputFromThread(){
    udp.allocateDestination();
    while (keepGoing()){
      if (!handleWebRTCInputOutput()){udp.sendPaced(10);}
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

  // Checks if there is data on our UDP socket. The data can be
  // STUN, DTLS, SRTP or SRTCP. When we're receiving media from
  // the browser (e.g. from webcam) this function is called from
  // a separate thread. When we're pushing media to the browser
  // this is called from the main thread.
  bool OutWebRTC::handleWebRTCInputOutput(){

    bool hadPack = false;
    while (udp.Receive()){
      hadPack = true;
      myConn.addDown(udp.data.size());

      uint8_t fb = (uint8_t)udp.data[0];

      if (fb > 127 && fb < 192){
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "Packet " << (int)fb << ": RTP/RTCP" << std::endl;}
        handleReceivedRTPOrRTCPPacket();
      }else if (fb > 19 && fb < 64){
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "Packet " << (int)fb << ": DTLS" << std::endl;}
        handleReceivedDTLSPacket();
      }else if (fb < 2){
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "Packet " << (int)fb << ": STUN" << std::endl;}
        handleReceivedSTUNPacket();
      }else{
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "Packet " << (int)fb << ": Unknown" << std::endl;}
        FAIL_MSG("Unhandled WebRTC data. Type: %02X", fb);
      }
    }

    //If this is an incoming push, handle receiver reports and keyframe interval
    if (isPushing()){
      uint64_t now = Util::bootMS();
      
      //Receiver reports and packet loss calculations
      if (now >= rtcpTimeoutInMillis){
        std::map<uint64_t, WebRTCTrack>::iterator it;
        for (it = webrtcTracks.begin(); it != webrtcTracks.end(); ++it){
          if (M.getType(it->first) != "video"){continue;}//Video-only, at least for now
          sendRTCPFeedbackREMB(it->second);
          sendRTCPFeedbackRR(it->second);
        }
        rtcpTimeoutInMillis = now + 1000; /* was 5000, lowered for FEC */
      }

      //Keyframe requests
      if (now >= rtcpKeyFrameTimeoutInMillis){
        std::map<uint64_t, WebRTCTrack>::iterator it;
        for (it = webrtcTracks.begin(); it != webrtcTracks.end(); ++it){
          if (M.getType(it->first) != "video"){continue;}//Video-only
          sendRTCPFeedbackPLI(it->second);
        }
        rtcpKeyFrameTimeoutInMillis = now + rtcpKeyFrameDelayInMillis;
      }
    }

    if (udp.getSock() == -1){onFail("UDP socket closed", true);}
    return hadPack;
  }

  void OutWebRTC::handleReceivedSTUNPacket(){

    size_t nparsed = 0;
    StunMessage stun_msg;
    if (stunReader.parse((uint8_t *)(char*)udp.data, udp.data.size(), nparsed, stun_msg) != 0){
      FAIL_MSG("Failed to parse a stun message.");
      return;
    }

    if (stun_msg.type != STUN_MSG_TYPE_BINDING_REQUEST){
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

    std::string remoteIP = "";
    uint32_t remotePort = 0;
    udp.GetDestination(remoteIP, remotePort);
    if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "STUN: Bound to " << remoteIP << ":" << remotePort << std::endl;}

    // create the binding success response
    stun_msg.removeAttributes();
    stun_msg.setType(STUN_MSG_TYPE_BINDING_RESPONSE_SUCCESS);

    StunWriter stun_writer;
    stun_writer.begin(stun_msg);
    stun_writer.writeXorMappedAddress(STUN_IP4, remotePort, remoteIP);
    stun_writer.writeMessageIntegrity(passwordLocal);
    stun_writer.writeFingerprint();
    stun_writer.end();
    
    udp.sendPaced((const char *)stun_writer.getBufferPtr(), stun_writer.getBufferSize());
    myConn.addUp(stun_writer.getBufferSize());
  }

  void OutWebRTC::handleReceivedDTLSPacket(){

    if (dtlsHandshake.hasKeyingMaterial()){
      DONTEVEN_MSG("Not feeding data into the handshake .. already done.");
      return;
    }

    if (dtlsHandshake.parse((const uint8_t *)(const char*)udp.data, udp.data.size()) != 0){
      FAIL_MSG("Failed to parse a DTLS packet.");
      return;
    }
    lastRecv = Util::bootMS();

    if (!dtlsHandshake.hasKeyingMaterial()){
      if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "DTLS: No keying material (yet)" << std::endl;}
      return;
    }

    if (srtpReader.init(dtlsHandshake.cipher, dtlsHandshake.remote_key, dtlsHandshake.remote_salt) != 0){
      FAIL_MSG("Failed to initialize the SRTP reader.");
      return;
    }

    if (srtpWriter.init(dtlsHandshake.cipher, dtlsHandshake.local_key, dtlsHandshake.local_salt) != 0){
      FAIL_MSG("Failed to initialize the SRTP writer.");
      return;
    }
    if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "DTLS: Keying material success" << std::endl;}
  }

  void OutWebRTC::ackNACK(uint32_t pSSRC, uint16_t seq){
    totalRetrans++;
    if (!outBuffers.count(pSSRC)){
      WARN_MSG("Could not answer NACK for %" PRIu32 ": we don't know this track", pSSRC);
      return;
    }
    nackBuffer &nb = outBuffers[pSSRC];
    if (!nb.isBuffered(seq)){
      HIGH_MSG("Could not answer NACK for %" PRIu32 " #%" PRIu16 ": packet not buffered", pSSRC, seq);
      return;
    }
    udp.sendPaced(nb.getData(seq), nb.getSize(seq));
    myConn.addUp(nb.getSize(seq));
    HIGH_MSG("Answered NACK for %" PRIu32 " #%" PRIu16, pSSRC, seq);
  }

  void OutWebRTC::handleReceivedRTPOrRTCPPacket(){

    uint8_t pt = udp.data[1] & 0x7F;

    if ((pt < 64) || (pt >= 96)){

      RTP::Packet rtp_pkt((const char *)udp.data, (unsigned int)udp.data.size());
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
      int len = udp.data.size();
      if (srtpReader.unprotectRtp((uint8_t *)(char*)udp.data, &len) != 0){
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "RTP decrypt failure" << std::endl;}
        return;
      }
      if (!len){return;}
      lastRecv = Util::bootMS();
      RTP::Packet unprotPack(udp.data, len);
      DONTEVEN_MSG("%s", unprotPack.toString().c_str());

      rtcTrack.gotPacket(unprotPack.getTimeStamp());

      if (rtp_pkt.getPayloadType() == rtcTrack.REDPayloadType || rtp_pkt.getPayloadType() == rtcTrack.ULPFECPayloadType){
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "RED packet " << rtp_pkt.getPayloadType() << " #" << currSeqNum << std::endl;}
        rtcTrack.sorter.addREDPacket(udp.data, len, rtcTrack.payloadType, rtcTrack.REDPayloadType,
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
      int len = udp.data.size();
      if (doDTLS){
        if (srtpReader.unprotectRtcp((uint8_t *)(char*)udp.data, &len) != 0){
          if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "RTCP decrypt failure" << std::endl;}
          return;
        }
        if (!len){return;}
      }

      lastRecv = Util::bootMS();
      uint8_t fmt = udp.data[0] & 0x1F;
      if (pt == 77 || pt == 65){
        //77/65 = nack
        if (fmt == 1){
          uint32_t pSSRC = Bit::btohl(udp.data + 8);
          uint16_t seq = Bit::btohs(udp.data + 12);
          uint16_t bitmask = Bit::btohs(udp.data + 14);
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
        uint32_t SSRC = Bit::btohl(udp.data + 4);
        std::map<uint64_t, WebRTCTrack>::iterator it;
        for (it = webrtcTracks.begin(); it != webrtcTracks.end(); ++it){
          if (it->second.SSRC == SSRC){
            it->second.sorter.lastBootMS = Util::bootMS();
            it->second.sorter.lastNTP = Bit::btohl(udp.data+10);
            uint64_t ntpTime = Bit::btohll(udp.data + 8);
            uint32_t rtpTime = Bit::btohl(udp.data + 16);
            uint32_t packets = Bit::btohl(udp.data + 20);
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
            uint32_t bytes = Bit::btohl(udp.data + 24);
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
        //73 = receiver report: https://datatracker.ietf.org/doc/html/rfc3550#section-6.4.2
        //Packet may contain more than one report
        char * ptr = udp.data + 8;
        while (ptr + 24 <= udp.data + udp.data.size()){
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
      }else{
        if (packetLog.is_open()){packetLog << "[" << Util::bootMS() << "]" << "Unknown payload type: " << pt << std::endl;}
        WARN_MSG("Unknown RTP feedback payload type: %u", pt);
      }
    }
  }

  /* ------------------------------------------------ */

  int OutWebRTC::onDTLSHandshakeWantsToWrite(const uint8_t *data, int *nbytes){
    udp.sendPaced((const char *)data, (size_t)*nbytes);
    myConn.addUp(*nbytes);
    return 0;
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
    rtpOutBuffer.assign(data, nbytes);

    int protectedSize = nbytes;

    if (doDTLS){
      if (srtpWriter.protectRtp((uint8_t *)(void *)rtpOutBuffer, &protectedSize) != 0){
        ERROR_MSG("Failed to protect the RTP message.");
        return;
      }
    }
    udp.sendPaced(rtpOutBuffer, (size_t)protectedSize);

    RTP::Packet tmpPkt(rtpOutBuffer, protectedSize);
    uint32_t pSSRC = tmpPkt.getSSRC();
    uint16_t seq = tmpPkt.getSequence();
    outBuffers[pSSRC].assign(seq, rtpOutBuffer, protectedSize);
    myConn.addUp(protectedSize);
    totalPkts++;

    if (volkswagenMode){
      if (srtpWriter.protectRtp((uint8_t *)(void *)rtpOutBuffer, &protectedSize) != 0){
        ERROR_MSG("Failed to protect the RTP message.");
        return;
      }
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

    rtpOutBuffer.allocate(nbytes + 256);
    rtpOutBuffer.assign(data, nbytes);
    int rtcpPacketSize = nbytes;
    
    if (doDTLS){
      if (srtpWriter.protectRtcp((uint8_t *)(void *)rtpOutBuffer, &rtcpPacketSize) != 0){
        ERROR_MSG("Failed to protect the RTCP message.");
        return;
      }
    }
    
    udp.sendPaced(rtpOutBuffer, rtcpPacketSize);
    myConn.addUp(rtcpPacketSize);

    if (volkswagenMode){
      if (srtpWriter.protectRtcp((uint8_t *)(void *)rtpOutBuffer, &rtcpPacketSize) != 0){
        ERROR_MSG("Failed to protect the RTCP message.");
        return;
      }
    }
    
  }

  // This function was implemented (it's virtual) to handle
  // pushing of media to the browser. This function blocks until
  // the DTLS handshake has been finished. This prevents
  // `sendNext()` from being called which is correct because we
  // don't want to send packets when we can't protect them with
  // DTLS.
  void OutWebRTC::sendHeader(){

    // first make sure that we complete the DTLS handshake.
    if(doDTLS){
      while (keepGoing() && !dtlsHandshake.hasKeyingMaterial()){
        if (!handleWebRTCInputOutput()){udp.sendPaced(10000);}
        if (lastRecv < Util::bootMS() - 10000){
          WARN_MSG("Killing idle connection in handshake phase");
          onFail("idle connection in handshake phase", false);
          return;
        }
      }
    }
    sentHeader = true;
  }

  void OutWebRTC::sendNext(){
    if (lastRecv < Util::bootMS() - 10000){
      WARN_MSG("Killing idle connection");
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

    rtcTrack.rtpPacketizer.sendData(&udp, onRTPPacketizerHasDataCallback, dataPointer, dataLen,
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
      rtcTrack.rtpPacketizer.sendRTCP_SR((void *)&udp, 0, onRTPPacketizerHasRTCPDataCallback);
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

    if (doDTLS){
      if (srtpWriter.protectRtcp(&buffer[0], &buffer_size_in_bytes) != 0){
        ERROR_MSG("Failed to protect the RTCP message.");
        return;
      }
    }
    
    udp.sendPaced((const char *)&buffer[0], buffer_size_in_bytes);
    myConn.addUp(buffer_size_in_bytes);

    if (volkswagenMode){
      if (srtpWriter.protectRtcp(&buffer[0], &buffer_size_in_bytes) != 0){
        ERROR_MSG("Failed to protect the RTCP message.");
        return;
      }
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

    if (doDTLS){
      if (srtpWriter.protectRtcp(&buffer[0], &buffer_size_in_bytes) != 0){
        ERROR_MSG("Failed to protect the RTCP message.");
        return;
      }
    }

    udp.sendPaced((const char *)&buffer[0], buffer_size_in_bytes);
    myConn.addUp(buffer_size_in_bytes);

    if (volkswagenMode){
      if (srtpWriter.protectRtcp(&buffer[0], &buffer_size_in_bytes) != 0){
        ERROR_MSG("Failed to protect the RTCP message.");
        return;
      }
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

    if (doDTLS){
      if (srtpWriter.protectRtcp(&buffer[0], &buffer_size_in_bytes) != 0){
        ERROR_MSG("Failed to protect the RTCP message.");
        return;
      }
    }
    
    udp.sendPaced((const char *)&buffer[0], buffer_size_in_bytes);
    myConn.addUp(buffer_size_in_bytes);

    if (volkswagenMode){
      if (srtpWriter.protectRtcp(&buffer[0], &buffer_size_in_bytes) != 0){
        ERROR_MSG("Failed to protect the RTCP message.");
        return;
      }
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
    ((RTP::FECPacket *)&(rtcTrack.rtpPacketizer))->sendRTCP_RR(rtcTrack.sorter, SSRC, rtcTrack.SSRC, (void *)&udp, onRTPPacketizerHasRTCPDataCallback, (uint32_t)rtcTrack.jitter);
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

      rtcTrack.rtpPacketizer.sendData(&udp, onRTPPacketizerHasDataCallback, &buf[0], buf.size(),
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

      rtcTrack.rtpPacketizer.sendData(&udp, onRTPPacketizerHasDataCallback, &buf[0], buf.size(),
                                      rtcTrack.payloadType, M.getCodec(dtscIdx));
    }
  }

  /* ------------------------------------------------ */

  // This is our thread function that is started right before we
  // call `allowPush()` and send our answer SDP back to the
  // client.
  static void webRTCInputOutputThreadFunc(void *arg){
    if (!classPointer){
      FAIL_MSG("classPointer hasn't been set. Exiting thread.");
      return;
    }
    classPointer->handleWebRTCInputOutputFromThread();
  }

  static int onDTLSHandshakeWantsToWriteCallback(const uint8_t *data, int *nbytes){
    if (!classPointer){
      FAIL_MSG("Requested to send DTLS handshake data but the `classPointer` hasn't been set.");
      return -1;
    }
    return classPointer->onDTLSHandshakeWantsToWrite(data, nbytes);
  }

  static void onRTPSorterHasPacketCallback(const uint64_t track, const RTP::Packet &p){
    if (!classPointer){
      FAIL_MSG("We received a sorted RTP packet but our `classPointer` is invalid.");
      return;
    }
    classPointer->onRTPSorterHasPacket(track, p);
  }

  static void onDTSCConverterHasInitDataCallback(const uint64_t track, const std::string &initData){
    if (!classPointer){
      FAIL_MSG("Received a init data, but our `classPointer` is invalid.");
      return;
    }
    classPointer->onDTSCConverterHasInitData(track, initData);
  }

  static void onDTSCConverterHasPacketCallback(const DTSC::Packet &pkt){
    if (!classPointer){
      FAIL_MSG("Received a DTSC packet that was created from RTP data, but our `classPointer` is "
               "invalid.");
      return;
    }
    classPointer->onDTSCConverterHasPacket(pkt);
  }

  static void onRTPPacketizerHasDataCallback(void *socket, const char *data, size_t len, uint8_t channel){
    if (!classPointer){
      FAIL_MSG("Received a RTP packet but our `classPointer` is invalid.");
      return;
    }
    classPointer->onRTPPacketizerHasRTPPacket(data, len);
  }

  static void onRTPPacketizerHasRTCPDataCallback(void *socket, const char *data, size_t len, uint8_t){
    if (!classPointer){
      FAIL_MSG("Received a RTCP packet, but out `classPointer` is invalid.");
      return;
    }
    classPointer->onRTPPacketizerHasRTCPPacket(data, len);
  }

  static uint32_t generateSSRC(){

    uint32_t ssrc = 0;

    do{
      ssrc = rand();
      ssrc = ssrc << 16;
      ssrc += rand();
    }while (ssrc == 0 || ssrc == 0xffffffff);

    return ssrc;
  }

}// namespace Mist
