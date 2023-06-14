#include "output_rtsp.h"
#include <mist/adts.h>
#include <mist/auth.h>
#include <mist/bitfields.h>
#include <mist/bitstream.h>
#include <mist/defines.h>
#include <mist/encode.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <sys/stat.h>

namespace Mist{

  Socket::Connection *mainConn = 0;
  OutRTSP *classPointer = 0;

  /// Helper functions for passing packets into the OutRTSP class
  void insertPacket(const DTSC::Packet &pkt){classPointer->incomingPacket(pkt);}
  void insertRTP(const uint64_t track, const RTP::Packet &p){
    classPointer->incomingRTP(track, p);
  }

  /// Takes incoming packets and buffers them.
  void OutRTSP::incomingPacket(const DTSC::Packet &pkt){
    if (!M.getBootMsOffset()){
      meta.setBootMsOffset(Util::bootMS() - pkt.getTime());
      packetOffset = 0;
      setPacketOffset = true;
    }else if (!setPacketOffset){
      packetOffset = (Util::bootMS() - pkt.getTime()) - M.getBootMsOffset();
      setPacketOffset = true;
    }
    char *pktData;
    size_t pktDataLen;
    pkt.getString("data", pktData, pktDataLen);
    bufferLivePacket(pkt.getTime() + packetOffset, pkt.getInt("offset"), pkt.getTrackId(), pktData,
                     pktDataLen, 0, pkt.getFlag("keyframe"));
    // bufferLivePacket(DTSC::RetimedPacket(pkt.getTime() + packetOffset, pkt));
  }
  void OutRTSP::incomingRTP(const uint64_t track, const RTP::Packet &p){
    sdpState.handleIncomingRTP(track, p);
  }

  OutRTSP::OutRTSP(Socket::Connection &myConn) : Output(myConn){
    pausepoint = 0;
    setPacketOffset = false;
    packetOffset = 0;
    setBlocking(false);
    maxSkipAhead = 0;
    expectTCP = false;
    checkPort = !config->getBool("ignsendport");
    lastTimeSync = 0;
    mainConn = &myConn;
    classPointer = this;
    sdpState.incomingPacketCallback = insertPacket;
    sdpState.myMeta = &meta;
  }

  /// Function used to send RTP packets over UDP
  ///\param socket A UDP Connection pointer, sent as a void*, to keep portability.
  ///\param data The RTP Packet that needs to be sent
  ///\param len The size of data
  ///\param channel Not used here, but is kept for compatibility with sendTCP
  void sendUDP(void *socket, const char *data, size_t len, uint8_t){
    ((Socket::UDPConnection *)socket)->SendNow(data, len);
    if (mainConn){mainConn->addUp(len);}
  }

  /// Function used to send RTP packets over TCP
  ///\param socket A TCP Connection pointer, sent as a void*, to keep portability.
  ///\param data The RTP Packet that needs to be sent
  ///\param len The size of data
  ///\param channel Used to distinguish different data streams when sending RTP over TCP
  void sendTCP(void *socket, const char *data, size_t len, uint8_t channel){
    // 1 byte '$', 1 byte channel, 2 bytes length
    char buf[] = "$$$$";
    buf[1] = channel;
    ((short *)buf)[1] = htons(len);
    ((Socket::Connection *)socket)->SendNow(buf, 4);
    ((Socket::Connection *)socket)->SendNow(data, len);
  }

  void OutRTSP::init(Util::Config *cfg){
    Output::init(cfg);
    capa["name"] = "RTSP";
    capa["friendly"] = "RTSP";
    capa["desc"] = "Real Time Streaming in RTSP, over both RTP UDP and TCP";
    capa["deps"] = "";
    capa["url_rel"] = "/$";
    capa["incoming_push_url"] = "rtsp://$host:$port/$stream?pass=$password";
    capa["codecs"][0u][0u].append("+H264");
    capa["codecs"][0u][1u].append("+HEVC");
    capa["codecs"][0u][2u].append("+MPEG2");
    capa["codecs"][0u][3u].append("+VP8");
    capa["codecs"][0u][4u].append("+VP9");
    capa["codecs"][0u][5u].append("+AAC");
    capa["codecs"][0u][6u].append("+MP3");
    capa["codecs"][0u][7u].append("+AC3");
    capa["codecs"][0u][8u].append("+ALAW");
    capa["codecs"][0u][9u].append("+ULAW");
    capa["codecs"][0u][10u].append("+PCM");
    capa["codecs"][0u][11u].append("+opus");
    capa["codecs"][0u][12u].append("+MP2");

    capa["methods"][0u]["handler"] = "rtsp";
    capa["methods"][0u]["type"] = "rtsp";
    capa["methods"][0u]["hrn"] = "RTSP";
    capa["methods"][0u]["priority"] = 2;

    capa["optional"]["maxsend"]["name"] = "Max RTP packet size";
    capa["optional"]["maxsend"]["help"] = "Maximum size of RTP packets in bytes";
    capa["optional"]["maxsend"]["default"] = RTP::MAX_SEND;
    capa["optional"]["maxsend"]["type"] = "uint";
    capa["optional"]["maxsend"]["option"] = "--max-packet-size";
    capa["optional"]["maxsend"]["short"] = "m";

    capa["optional"]["ignsendport"]["name"] = "Ignore sending port #";
    capa["optional"]["ignsendport"]["help"] = "Ignore the sending port number of incoming data";
    capa["optional"]["ignsendport"]["default"] = false;
    capa["optional"]["ignsendport"]["option"] = "--ignore-sending-port";
    capa["optional"]["ignsendport"]["short"] = "I";

    cfg->addConnectorOptions(5554, capa);
    config = cfg;
  }

  void OutRTSP::sendNext(){
    char *dataPointer = 0;
    size_t dataLen = 0;
    thisPacket.getString("data", dataPointer, dataLen);
    uint64_t timestamp = thisPacket.getTime();

    // if we're past the pausing point, seek to it, and pause immediately
    if (pausepoint && timestamp > pausepoint){
      pausepoint = 0;
      stop();
      return;
    }

    if (M.getLive() && lastTimeSync + 200 < timestamp){
      lastTimeSync = timestamp;
      if (liveSeek()){return;}
    }

    void *socket = 0;
    void (*callBack)(void *, const char *, size_t, uint8_t) = 0;

    if (sdpState.tracks[thisIdx].channel == -1){// UDP connection
      socket = &sdpState.tracks[thisIdx].data;
      callBack = sendUDP;
    }else{
      socket = &myConn;
      callBack = sendTCP;
    }

    uint64_t offset = thisPacket.getInt("offset");
    sdpState.tracks[thisIdx].pack.setTimestamp((timestamp + offset) * SDP::getMultiplier(&M, thisIdx));
    sdpState.tracks[thisIdx].pack.sendData(socket, callBack, dataPointer, dataLen,
                                           sdpState.tracks[thisIdx].channel, meta.getCodec(thisIdx));


    if (Util::bootSecs() != sdpState.tracks[thisIdx].rtcpSent){
      if (sdpState.tracks[thisIdx].channel == -1){// UDP connection
        sdpState.tracks[thisIdx].pack.sendRTCP_SR(&sdpState.tracks[thisIdx].rtcp, 0, callBack);
      }else{
        sdpState.tracks[thisIdx].pack.sendRTCP_SR(socket, sdpState.tracks[thisIdx].channel, callBack);
      }
      sdpState.tracks[thisIdx].rtcpSent = Util::bootSecs();
    }

    static uint64_t lastAnnounce = Util::bootSecs();
    if (reqUrl.size() && lastAnnounce + 5 < Util::bootSecs()){
      INFO_MSG("Sending announce");
      lastAnnounce = Util::bootSecs();
      std::string transportString = generateSDP(reqUrl);
      HTTP_S.Clean();
      HTTP_S.SetHeader("Content-Type", "application/sdp");
      HTTP_S.SetHeader("Content-Base", reqUrl);
      HTTP_S.method = "ANNOUNCE";
      HTTP_S.url = reqUrl;
      HTTP_S.protocol = "RTSP/1.0";
      HTTP_S.SendRequest(myConn, transportString);
      HTTP_R.Clean();
    }
  }

  /// This request handler also checks for UDP packets
  void OutRTSP::requestHandler(){
    if (!expectTCP){handleUDP();}
    Output::requestHandler();
  }

  void OutRTSP::onRequest(){
    RTP::MAX_SEND = config->getInteger("maxsend");
    // if needed, parse TCP packets, and cancel if it is not safe (yet) to read HTTP/RTSP packets
    while ((!expectTCP || handleTCP()) && HTTP_R.Read(myConn)){
      // check for response codes. Assume a 3-letter URL is a response code.
      if (HTTP_R.url.size() == 3){
        INFO_MSG("Received response: %s %s", HTTP_R.url.c_str(), HTTP_R.protocol.c_str());
        if (HTTP_R.url == "501"){
          // Not implemented = probably a response to our ANNOUNCE. Turn them off.
          reqUrl.clear();
        }
        HTTP_R.Clean();
        continue;
      }
      // cancel broken URLs
      if (HTTP_R.url.size() < 8){
        WARN_MSG("Invalid data found in RTSP input around ~%" PRIu64 "b - disconnecting!", myConn.dataDown());
        onFail("Invalid RTSP Data", true);
        break;
      }

      if (HTTP_R.GetVar("audio") != ""){targetParams["audio"] = HTTP_R.GetVar("audio");}
      if (HTTP_R.GetVar("video") != ""){targetParams["video"] = HTTP_R.GetVar("video");}
      if (HTTP_R.GetVar("subtitle") != ""){targetParams["subtitle"] = HTTP_R.GetVar("subtitle");}
      if (HTTP_R.GetVar("start") != ""){targetParams["start"] = HTTP_R.GetVar("start");}
      if (HTTP_R.GetVar("stop") != ""){targetParams["stop"] = HTTP_R.GetVar("stop");}
      if (HTTP_R.GetVar("startunix") != ""){targetParams["startunix"] = HTTP_R.GetVar("startunix");}
      if (HTTP_R.GetVar("stopunix") != ""){targetParams["stopunix"] = HTTP_R.GetVar("stopunix");}
      if (HTTP_R.hasHeader("User-Agent")){UA = HTTP_R.GetHeader("User-Agent");}

      HTTP_S.Clean();
      HTTP_S.protocol = "RTSP/1.0";

      // set the streamname and session
      if (!source.size()){
        std::string source = HTTP_R.url.substr(7);
        source = source.substr(0, std::min(source.find(':'), source.find('/')));
      }
      size_t found = HTTP_R.url.find('/', 7);
      if (found != std::string::npos && !streamName.size()){
        streamName = HTTP_R.url.substr(found + 1, HTTP_R.url.substr(found + 1).find('/'));
        Util::sanitizeName(streamName);
      }
      if (streamName.size()){
        HTTP_S.SetHeader("Session", Secure::md5(HTTP_S.GetHeader("User-Agent") + getConnectedHost()) +
                                        "_" + streamName);
      }

      // allow setting of max lead time through buffer variable.
      // max lead time is set in MS, but the variable is in integer seconds for simplicity.
      if (HTTP_R.GetVar("buffer") != ""){
        maxSkipAhead = JSON::Value(HTTP_R.GetVar("buffer")).asInt() * 1000;
      }
      // allow setting of play back rate through buffer variable.
      // play back rate is set in MS per second, but the variable is a simple multiplier.
      if (HTTP_R.GetVar("rate") != ""){
        double multiplier = atof(HTTP_R.GetVar("rate").c_str());
        if (multiplier){
          realTime = 1000 / multiplier;
        }else{
          realTime = 0;
        }
      }
      if (HTTP_R.GetHeader("X-Mist-Rate") != ""){
        double multiplier = atof(HTTP_R.GetHeader("X-Mist-Rate").c_str());
        if (multiplier){
          realTime = 1000 / multiplier;
        }else{
          realTime = 0;
        }
      }

      // set the date
      time_t timer;
      time(&timer);
      struct tm *timeNow = gmtime(&timer);
      char dString[42];
      strftime(dString, 42, "%a, %d %h %Y, %X GMT", timeNow);
      HTTP_S.SetHeader("Date", dString);

      // set the sequence number to match the received sequence number
      if (HTTP_R.hasHeader("CSeq")){HTTP_S.SetHeader("CSeq", HTTP_R.GetHeader("CSeq"));}
      if (HTTP_R.hasHeader("Cseq")){HTTP_S.SetHeader("CSeq", HTTP_R.GetHeader("Cseq"));}

      INFO_MSG("Handling %s", HTTP_R.method.c_str());

      // handle the request
      if (HTTP_R.method == "OPTIONS"){
        HTTP_S.SetHeader("Public",
                         "SETUP, TEARDOWN, PLAY, PAUSE, DESCRIBE, GET_PARAMETER, ANNOUNCE, RECORD");
        HTTP_S.SendResponse("200", "OK", myConn);
        HTTP_R.Clean();
        continue;
      }
      if (HTTP_R.method == "GET_PARAMETER"){
        HTTP_S.SendResponse("200", "OK", myConn);
        HTTP_R.Clean();
        continue;
      }
      if (HTTP_R.method == "DESCRIBE"){
        reqUrl = HTTP::URL(HTTP_R.url).link(streamName).getProxyUrl();
        initialize();
        userSelect.clear();
        std::string transportString = generateSDP(reqUrl);
        HIGH_MSG("Reply: %s", transportString.c_str());
        HTTP_S.SetHeader("Content-Base", reqUrl);
        HTTP_S.SetHeader("Content-Type", "application/sdp");
        HTTP_S.SetBody(transportString);
        HTTP_S.SendResponse("200", "OK", myConn);
        HTTP_R.Clean();
        continue;
      }
      if (HTTP_R.method == "SETUP"){
        size_t trackNo = sdpState.parseSetup(HTTP_R, getConnectedHost(), source);
        HTTP_S.SetHeader("Expires", HTTP_S.GetHeader("Date"));
        HTTP_S.SetHeader("Cache-Control", "no-cache");
        if (trackNo != INVALID_TRACK_ID){
          userSelect[trackNo].reload(streamName, trackNo);
          if (isPushing()){userSelect[trackNo].setStatus(COMM_STATUS_SOURCE | userSelect[trackNo].getStatus());}
          SDP::Track &sdpTrack = sdpState.tracks[trackNo];
          if (sdpTrack.channel != -1){expectTCP = true;}
          HTTP_S.SetHeader("Transport", sdpTrack.transportString);
          HTTP_S.SendResponse("200", "OK", myConn);
          INFO_MSG("Setup completed for track %zu (%s): %s", trackNo, M.getCodec(trackNo).c_str(),
                   sdpTrack.transportString.c_str());
        }else{
          HTTP_S.SendResponse("404", "Track not known or allowed", myConn);
          FAIL_MSG("Could not handle setup for %s", HTTP_R.url.c_str());
        }
        HTTP_R.Clean();
        continue;
      }
      if (HTTP_R.method == "PLAY"){
        initialSeek();
        std::string range = HTTP_R.GetHeader("Range");
        if (range != ""){
          range = range.substr(range.find("npt=") + 4);
          if (!range.empty()){
            range = range.substr(0, range.find('-'));
            uint64_t targetPos = 1000 * atoll(range.c_str());
            if (targetPos || meta.getVod()){seek(targetPos);}
          }
        }
        std::stringstream rangeStr;
        if (meta.getLive()){
          rangeStr << "npt=" << currentTime() / 1000 << "." << std::setw(3) << std::setfill('0')
                   << currentTime() % 1000 << "-";
        }else{
          rangeStr << "npt=" << currentTime() / 1000 << "." << std::setw(3) << std::setfill('0')
                   << currentTime() % 1000 << "-" << std::setw(1) << endTime() / 1000 << "."
                   << std::setw(3) << std::setfill('0') << endTime() % 1000;
        }
        HTTP_S.SetHeader("Range", rangeStr.str());
        std::stringstream infoString;
        for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
          if (!infoString.str().empty()){infoString << ", ";}
          infoString << sdpState.tracks[it->first].rtpInfo(M, it->first, source + "/" + streamName,
                                                           currentTime());
        }
        HTTP_S.SetHeader("RTP-Info", infoString.str());
        HTTP_S.SendResponse("200", "OK", myConn);
        parseData = true;
        HTTP_R.Clean();
        continue;
      }
      if (HTTP_R.method == "PAUSE"){
        HTTP_S.SendResponse("200", "OK", myConn);
        std::string range = HTTP_R.GetHeader("Range");
        if (!range.empty()){range = range.substr(range.find("npt=") + 4);}
        if (range.empty()){
          stop();
        }else{
          pausepoint = 1000 * atoll(range.c_str());
          if (pausepoint > currentTime()){
            pausepoint = 0;
            stop();
          }
        }
        HTTP_R.Clean();
        continue;
      }
      if (HTTP_R.method == "TEARDOWN"){
        myConn.close();
        stop();
        HTTP_R.Clean();
        continue;
      }
      if (HTTP_R.method == "ANNOUNCE"){
        if (Triggers::shouldTrigger("PUSH_REWRITE")){
          HTTP::URL qUrl("rtsp://"+HTTP_R.GetHeader("Host")+"/"+HTTP_R.url);
          if (!qUrl.host.size()){qUrl.host = myConn.getBoundAddress();}
          if (!qUrl.port.size()){qUrl.port = config->getOption("port").asString();}
          std::string payload = qUrl.getUrl() + "\n" + getConnectedHost() + "\n" + streamName;
          std::string newStream = streamName;
          Triggers::doTrigger("PUSH_REWRITE", payload, "", false, newStream);
          if (!newStream.size()){
            FAIL_MSG("Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                     getConnectedHost().c_str(), qUrl.getUrl().c_str());
            Util::logExitReason(
                "Push from %s to URL %s rejected - PUSH_REWRITE trigger blanked the URL",
                getConnectedHost().c_str(), qUrl.getUrl().c_str());
            onFinish();
            return;
          }else{
            streamName = newStream;
            Util::sanitizeName(streamName);
            Util::setStreamName(streamName);
          }
        }
        if (!allowPush(HTTP_R.GetVar("pass"))){
          onFinish();
          return;
        }
        INFO_MSG("Pushing to stream %s", streamName.c_str());
        sdpState.parseSDP(HTTP_R.body);
        HTTP_S.SendResponse("200", "OK", myConn);
        HTTP_R.Clean();
        continue;
      }
      if (HTTP_R.method == "RECORD"){
        HTTP_S.SendResponse("200", "OK", myConn);
        HTTP_R.Clean();
        continue;
      }
      WARN_MSG("Unhandled command received (protocol corruption?)");
      HTTP_R.Clean();
    }
  }

  /// Disconnects the user
  bool OutRTSP::onFinish(){
    if (myConn){myConn.close();}
    return false;
  }

  /// \param reqUrl: URI to additional session information
  std::string OutRTSP::generateSDP(std::string reqUrl){
    std::stringstream transportString;
    transportString.precision(3);
    // Add session description & time description
    transportString << std::fixed <<
      "v=0\r\n"
      "o=- " << Util::getMS() << " 1 IN IP4 127.0.0.1\r\n"
      "s=" << streamName << "\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "i=" << streamName << "\r\n"
      "u=" << reqUrl << "\r\n"
      "t=0 0\r\n"
      "a=tool:" APPIDENT "\r\n"
      "a=type:broadcast\r\n"
      "a=control:*\r\n";
     if (M.getLive()){
       transportString << "a=range:npt=" << ((double)startTime()) / 1000.0 << "-\r\n";
     }else{
       transportString << "a=range:npt=" << ((double)startTime()) / 1000.0 << "-" << ((double)endTime()) / 1000.0 << "\r\n";
     }
    // Add Media description
    std::set<size_t> validTracks = M.getValidTracks();
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
      transportString << SDP::mediaDescription(&M, *it);
    }
    return transportString.str();
  }

  /// Attempts to parse TCP RTP packets at the beginning of the header.
  /// Returns whether it is safe to attempt to read HTTP/RTSP packets (true) or not (false).
  bool OutRTSP::handleTCP(){
    if (!myConn.Received().size() || !myConn.Received().available(1)){return false;}// no data
    if (myConn.Received().copy(1) != "$"){return true;}// not a TCP RTP packet
    if (!myConn.Received().available(4)){return false;}// a TCP RTP packet, but not complete yet
    // We have a TCP packet! Read it...
    // Format: 1 byte '$', 1 byte channel, 2 bytes len, len bytes binary data
    std::string tcpHead = myConn.Received().copy(4);
    uint16_t len = Bit::btohs(tcpHead.data() + 2);
    if (!myConn.Received().available(len + 4)){
      return false;
    }// a TCP RTP packet, but not complete yet
    // remove whole packet from buffer, including 4 byte header
    std::string tcpPacket = myConn.Received().remove(len + 4);
    size_t trackNo = sdpState.getTrackNoForChannel(tcpHead.data()[1]);
    if ((trackNo != INVALID_TRACK_ID) && isPushing()){
      RTP::Packet pkt(tcpPacket.data() + 4, len);
      sdpState.tracks[trackNo].sorter.rtpSeq = pkt.getSequence();
      incomingRTP(trackNo, pkt);
    }
    // attempt to read more packets
    return handleTCP();
  }

  /// Reads and handles RTP packets over UDP, if needed
  void OutRTSP::handleUDP(){
    if (!isPushing()){return;}
    for (std::map<uint64_t, SDP::Track>::iterator it = sdpState.tracks.begin();
         it != sdpState.tracks.end(); ++it){
      Socket::UDPConnection &s = it->second.data;
      it->second.sorter.setCallback(it->first, insertRTP);
      while (s.Receive()){
        if (s.getDestPort() != it->second.cPortA && checkPort){
          // wrong sending port, ignore packet
          continue;
        }
        lastRecv = Util::bootSecs(); // prevent disconnect of idle TCP connection when using UDP
        myConn.addDown(s.data.size());
        RTP::Packet pack(s.data, s.data.size());
        if (!it->second.theirSSRC){it->second.theirSSRC = pack.getSSRC();}
        it->second.sorter.addPacket(pack);
      }
      if (userSelect.count(it->first) && Util::bootSecs() / 5 != it->second.rtcpSent){
        it->second.rtcpSent = Util::bootSecs() / 5;
        it->second.pack.sendRTCP_RR(it->second, sendUDP);
      }
    }
  }
}// namespace Mist
