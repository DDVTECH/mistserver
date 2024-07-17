#include "input_rtsp.h"

Mist::InputRTSP *classPointer = 0;
Socket::Connection *mainConn = 0;

void incomingPacketRTSP(const DTSC::Packet &pkt){
  classPointer->incoming(pkt);
}
void insertRTPRTSP(const uint64_t track, const RTP::Packet &p){
  classPointer->incomingRTP(track, p);
}

/// Function used to send RTP packets over UDP
///\param socket A UDP Connection pointer, sent as a void*, to keep portability.
///\param data The RTP Packet that needs to be sent
///\param len The size of data
///\param channel Not used here, but is kept for compatibility with sendTCP
void sendUDPRTSP(void *socket, const char *data, size_t len, uint8_t channel){
  ((Socket::UDPConnection *)socket)->SendNow(data, len);
  if (mainConn){mainConn->addUp(len);}
}

namespace Mist{
  void InputRTSP::incomingRTP(const uint64_t track, const RTP::Packet &p){
    sdpState.handleIncomingRTP(track, p);
  }

  InputRTSP::InputRTSP(Util::Config *cfg) : Input(cfg){
    needAuth = false;
    setPacketOffset = false;
    packetOffset = 0;
    TCPmode = true;
    sdpState.myMeta = &meta;
    sdpState.incomingPacketCallback = incomingPacketRTSP;
    classPointer = this;
    standAlone = false;
    seenSDP = false;
    cSeq = 0;
    capa["name"] = "RTSP";
    capa["desc"] = "This input allows pulling of live RTSP sources over either UDP or TCP.";
    capa["source_match"].append("rtsp://*");
    // These can/may be set to always-on mode
    capa["always_match"].append("rtsp://*");
    capa["priority"] = 9;
    capa["codecs"]["video"].append("H264");
    capa["codecs"]["video"].append("HEVC");
    capa["codecs"]["video"].append("MPEG2");
    capa["codecs"]["video"].append("VP8");
    capa["codecs"]["video"].append("VP9");
    capa["codecs"]["audio"].append("AAC");
    capa["codecs"]["audio"].append("MP3");
    capa["codecs"]["audio"].append("AC3");
    capa["codecs"]["audio"].append("ALAW");
    capa["codecs"]["audio"].append("ULAW");
    capa["codecs"]["audio"].append("PCM");
    capa["codecs"]["audio"].append("opus");
    capa["codecs"]["audio"].append("MP2");

    JSON::Value option;
    option["arg"] = "integer";
    option["long"] = "buffer";
    option["short"] = "b";
    option["help"] = "DVR buffer time in ms";
    option["value"].append(50000);
    config->addOption("bufferTime", option);
    capa["optional"]["DVR"]["name"] = "Buffer time (ms)";
    capa["optional"]["DVR"]["help"] = "The target available buffer time for this live stream, in "
                                      "milliseconds. This is the time available to seek around in, "
                                      "and will automatically be extended to fit whole keyframes "
                                      "as well as the minimum duration needed for stable playback.";
    capa["optional"]["DVR"]["option"] = "--buffer";
    capa["optional"]["DVR"]["type"] = "uint";
    capa["optional"]["DVR"]["default"] = 50000;
    option.null();
    option["arg"] = "string";
    option["long"] = "transport";
    option["short"] = "t";
    option["help"] = "Transport protocol (TCP (default) or UDP)";
    option["value"].append("TCP");
    config->addOption("transport", option);
    capa["optional"]["transport"]["name"] = "Transport protocol";
    capa["optional"]["transport"]["help"] = "Sets the transport protocol to either TCP (default) "
                                            "or UDP. UDP requires ephemeral UDP ports to be open, "
                                            "TCP does not.";
    capa["optional"]["transport"]["option"] = "--transport";
    capa["optional"]["transport"]["type"] = "select";
    capa["optional"]["transport"]["select"].append("TCP");
    capa["optional"]["transport"]["select"].append("UDP");
    capa["optional"]["transport"]["default"] = "TCP";
  }

  void InputRTSP::sendCommand(const std::string &cmd, const std::string &cUrl, const std::string &body,
                              const std::map<std::string, std::string> *extraHeaders, bool reAuth){
    ++cSeq;
    sndH.Clean();
    sndH.protocol = "RTSP/1.0";
    sndH.method = cmd;
    sndH.url = cUrl;
    sndH.body = body;
    if ((username.size() || password.size()) && authRequest.size()){
      sndH.auth(username, password, authRequest);
    }
    sndH.SetHeader("User-Agent", APPIDENT);
    sndH.SetHeader("CSeq", JSON::Value(cSeq).asString());
    if (session.size()){sndH.SetHeader("Session", session);}
    if (extraHeaders && extraHeaders->size()){
      for (std::map<std::string, std::string>::const_iterator it = extraHeaders->begin();
           it != extraHeaders->end(); ++it){
        sndH.SetHeader(it->first, it->second);
      }
    }
    sndH.SendRequest(tcpCon, "", true);
    parsePacket(true);

    if (reAuth && needAuth && authRequest.size() && (username.size() || password.size()) && tcpCon){
      INFO_MSG("Authenticating %s...", cmd.c_str());
      sendCommand(cmd, cUrl, body, extraHeaders, false);
      if (needAuth){FAIL_MSG("Authentication failed! Are the provided credentials correct?");}
    }
  }

  bool InputRTSP::checkArguments(){
    const std::string &inpt = config->getString("input");
    if (inpt.substr(0, 7) != "rtsp://"){
      Util::logExitReason(ER_FORMAT_SPECIFIC, "Unsupported RTSP URL: '%s'", inpt.c_str());
      return false;
    }
    const std::string &transport = config->getString("transport");
    if (transport != "TCP" && transport != "UDP" && transport != "tcp" && transport != "udp"){
      Util::logExitReason(ER_FORMAT_SPECIFIC, "Not a supported transport mode: %s", transport.c_str());
      return false;
    }
    if (transport == "UDP" || transport == "udp"){TCPmode = false;}
    url = HTTP::URL(config->getString("input"));
    username = url.user;
    password = url.pass;
    url.user = "";
    url.pass = "";
    return true;
  }

  bool InputRTSP::openStreamSource(){
    tcpCon.open(url.host, url.getPort(), false);
    mainConn = &tcpCon;
    if (!tcpCon){
      Util::logExitReason(ER_READ_START_FAILURE, "Opening TCP socket `%s:%s` failed", url.host.c_str(), url.getPort());
      return false;
    }
    return true;
  }

  void InputRTSP::parseStreamHeader(){
    tcpCon.setBlocking(false);
    std::map<std::string, std::string> extraHeaders;
    sendCommand("OPTIONS", url.getUrl(), "");
    extraHeaders["Accept"] = "application/sdp";
    sendCommand("DESCRIBE", url.getUrl(), "", &extraHeaders);
    if (!tcpCon || !seenSDP){
      Util::logExitReason(ER_FORMAT_SPECIFIC, "Could not get stream description!");
      return;
    }
    if (sdpState.tracks.size()){
      bool atLeastOne = false;
      for (std::map<uint64_t, SDP::Track>::iterator it = sdpState.tracks.begin();
           it != sdpState.tracks.end(); ++it){
        transportSet = false;
        extraHeaders.clear();
        extraHeaders["Transport"] = it->second.generateTransport(it->first, url.host, TCPmode);
        lastRequestedSetup = HTTP::URL(url.getUrl() + "/").link(it->second.control).getUrl();
        sendCommand("SETUP", lastRequestedSetup, "", &extraHeaders);
        if (tcpCon && transportSet){
          atLeastOne = true;
          continue;
        }
        if (!atLeastOne && tcpCon){
          INFO_MSG("Failed to set up transport for track %s, switching transports...", M.getTrackIdentifier(it->first).c_str());
          TCPmode = !TCPmode;
          extraHeaders["Transport"] = it->second.generateTransport(it->first, url.host, TCPmode);
          sendCommand("SETUP", lastRequestedSetup, "", &extraHeaders);
        }
        if (tcpCon && transportSet){
          atLeastOne = true;
          continue;
        }
        Util::logExitReason(ER_FORMAT_SPECIFIC, "Could not setup track %s!", M.getTrackIdentifier(it->first).c_str());
        tcpCon.close();
        return;
      }
    }
    INFO_MSG("Setup complete");
    extraHeaders.clear();
    extraHeaders["Range"] = "npt=0.000-";
    sendCommand("PLAY", url.getUrl(), "", &extraHeaders);
    if (TCPmode){tcpCon.setBlocking(true);}
  }

  void InputRTSP::closeStreamSource(){
    sendCommand("TEARDOWN", url.getUrl(), "");
    tcpCon.close();
  }

  void InputRTSP::streamMainLoop(){
    Comms::Connections statComm;
    uint64_t startTime = Util::epoch();
    uint64_t lastPing = Util::bootSecs();
    uint64_t lastSecs = 0;
    while (keepAlive() && parsePacket()){
      uint64_t currSecs = Util::bootSecs();
      handleUDP();
      if (Util::bootSecs() - lastPing > 30){
        sendCommand("GET_PARAMETER", url.getUrl(), "");
        lastPing = Util::bootSecs();
      }
      if (lastSecs != currSecs){
        lastSecs = currSecs;
        // Connect to stats for INPUT detection
        statComm.reload(streamName, getConnectedBinHost(), JSON::Value(getpid()).asString(), "INPUT:" + capa["name"].asStringRef(), "");
        if (statComm){
          if (statComm.getStatus() & COMM_STATUS_REQDISCONNECT){
            config->is_active = false;
            Util::logExitReason(ER_CLEAN_CONTROLLER_REQ, "received shutdown request from controller");
            return;
          }
          uint64_t now = Util::bootSecs();
          statComm.setNow(now);
          statComm.setStream(streamName);
          statComm.setConnector("INPUT:" + capa["name"].asStringRef());
          statComm.setUp(tcpCon.dataUp());
          statComm.setDown(tcpCon.dataDown());
          statComm.setTime(now - startTime);
          statComm.setLastSecond(0);
        }
      }
    }
    if (!tcpCon){
      Util::logExitReason(ER_CLEAN_REMOTE_CLOSE, "TCP connection closed");
    }
  }

  bool InputRTSP::parsePacket(bool mustHave){
    uint32_t waitTime = 500;
    if (!TCPmode){waitTime = 50;}
    do{
      // No new data? Sleep and retry, if connection still open
      if (!tcpCon.Received().size() || !tcpCon.Received().available(1)){
        if (!tcpCon.spool() && tcpCon && keepAlive()){
          Util::sleep(waitTime);
          if (!mustHave){return tcpCon;}
        }
        continue;
      }
      if (tcpCon.Received().copy(1) != "$"){
        // not a TCP RTP packet, read RTSP commands
        if (recH.Read(tcpCon)){
          if (recH.hasHeader("WWW-Authenticate")){
            authRequest = recH.GetHeader("WWW-Authenticate");
          }
          needAuth = (recH.url == "401");
          if (needAuth){
            INFO_MSG("Requires authentication");
            recH.Clean();
            return true;
          }
          if (recH.hasHeader("Content-Location")){
            url = HTTP::URL(recH.GetHeader("Content-Location"));
          }
          if (recH.hasHeader("Content-Base") && recH.GetHeader("Content-Base") != "" &&
              recH.GetHeader("Content-Base") != url.getUrl()){
            INFO_MSG("Changing base URL from %s to %s", url.getUrl().c_str(),
                     recH.GetHeader("Content-Base").c_str());
            url = HTTP::URL(recH.GetHeader("Content-Base"));
          }
          if (recH.hasHeader("Session")){
            session = recH.GetHeader("Session");
            if (session.find(';') != std::string::npos){
              session.erase(session.find(';'), std::string::npos);
            }
          }
          if ((recH.hasHeader("Content-Type") &&
               recH.GetHeader("Content-Type") == "application/sdp") ||
              (recH.hasHeader("Content-type") &&
               recH.GetHeader("Content-type") == "application/sdp")){
            INFO_MSG("Received SDP");
            seenSDP = true;
            sdpState.parseSDP(recH.body);
            recH.Clean();
            INFO_MSG("SDP contained %zu tracks", M.getValidTracks().size());
            return true;
          }
          if (recH.hasHeader("Transport")){
            INFO_MSG("Received setup response");
            recH.url = lastRequestedSetup;
            size_t trackNo = sdpState.parseSetup(recH, url.host, "");
            if (trackNo != INVALID_TRACK_ID){
              INFO_MSG("Parsed transport for track: %zu", trackNo);
              transportSet = true;
            }else{
              INFO_MSG("Could not parse transport string!");
            }
            recH.Clean();
            return true;
          }
          if (recH.url == "200" && recH.hasHeader("RTP-Info")){
            INFO_MSG("Playback starting");
            recH.Clean();
            return true;
          }
          // Ignore "OK" replies beyond this point
          if (recH.url == "200"){
            recH.Clean();
            return true;
          }

          // DO NOT Print anything possibly interesting to cerr
          // std::cerr << recH.BuildRequest() << std::endl;
          recH.Clean();
          return true;
        }
        if (!tcpCon.spool() && tcpCon && keepAlive()){Util::sleep(waitTime);}
        continue;
      }
      if (!tcpCon.Received().available(4)){
        if (!tcpCon.spool() && tcpCon && keepAlive()){Util::sleep(waitTime);}
        continue;
      }// a TCP RTP packet, but not complete yet

      // We have a TCP packet! Read it...
      // Format: 1 byte '$', 1 byte channel, 2 bytes len, len bytes binary data
      std::string tcpHead = tcpCon.Received().copy(4);
      uint16_t len = ntohs(*(short *)(tcpHead.data() + 2));
      if (!tcpCon.Received().available(len + 4)){
        if (!tcpCon.spool() && tcpCon){Util::sleep(waitTime);}
        continue;
      }// a TCP RTP packet, but not complete yet
      // remove whole packet from buffer, including 4 byte header
      std::string tcpPacket = tcpCon.Received().remove(len + 4);
      RTP::Packet pkt(tcpPacket.data() + 4, len);
      uint8_t chan = tcpHead.data()[1];
      size_t trackNo = sdpState.getTrackNoForChannel(chan);
      EXTREME_MSG("Received %ub RTP packet #%u on channel %u, time %" PRIu32, len,
                  pkt.getSequence(), chan, pkt.getTimeStamp());
      if (trackNo == INVALID_TRACK_ID){
        if ((chan % 2) != 1){
          WARN_MSG("Received packet for unknown track number on channel %u", chan);
        }
        return true;
      }

      //We override the rtpSeq number because in TCP mode packet loss is not a thing.
      sdpState.tracks[trackNo].sorter.rtpSeq = pkt.getSequence();
      sdpState.handleIncomingRTP(trackNo, pkt);

      return true;

    }while (tcpCon && keepAlive());
    return false;
  }

  /// Reads and handles RTP packets over UDP, if needed
  bool InputRTSP::handleUDP(){
    if (TCPmode){return false;}
    bool r = false;
    for (std::map<uint64_t, SDP::Track>::iterator it = sdpState.tracks.begin();
         it != sdpState.tracks.end(); ++it){
      Socket::UDPConnection &s = it->second.data;
      it->second.sorter.setCallback(it->first, insertRTPRTSP);
      while (s.Receive()){
        r = true;
        // if (s.getDestPort() != it->second.sPortA){
        //  // wrong sending port, ignore packet
        //  continue;
        //}
        tcpCon.addDown(s.data.size());
        RTP::Packet pack(s.data, s.data.size());
        if (!it->second.theirSSRC){it->second.theirSSRC = pack.getSSRC();}
        it->second.sorter.addPacket(pack);
      }
      if (Util::bootSecs() != it->second.rtcpSent){
        it->second.rtcpSent = Util::bootSecs();
        it->second.pack.sendRTCP_RR(it->second, sendUDPRTSP);
      }
    }
    return r;
  }

  void InputRTSP::incoming(const DTSC::Packet &pkt){
    if (!M.getBootMsOffset()){
      meta.setBootMsOffset(Util::bootMS() - pkt.getTime());
      packetOffset = 0;
      setPacketOffset = true;
    }else if (!setPacketOffset){
      packetOffset = (Util::bootMS() - pkt.getTime()) - M.getBootMsOffset();
      setPacketOffset = true;
    }
    static DTSC::Packet newPkt;
    char *pktData;
    size_t pktDataLen;
    pkt.getString("data", pktData, pktDataLen);
    size_t idx = M.trackIDToIndex(pkt.getTrackId(), getpid());

    if (idx == INVALID_TRACK_ID){
      INFO_MSG("Invalid index for track number %zu", pkt.getTrackId());
    }else{
      if (!userSelect.count(idx) || !userSelect[idx]){
        WARN_MSG("Reloading track %zu, index %zu", pkt.getTrackId(), idx);
        userSelect[idx].reload(streamName, idx, COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
      }
      if (!userSelect[idx] || (userSelect[idx].getStatus() & COMM_STATUS_REQDISCONNECT)){
        Util::logExitReason(ER_CLEAN_LIVE_BUFFER_REQ, "buffer requested shutdown");
        tcpCon.close();
      }
    }

    bufferLivePacket(pkt.getTime() + packetOffset, pkt.getInt("offset"), idx, pktData,
                     pktDataLen, 0, pkt.getFlag("keyframe"));
  }

}// namespace Mist
