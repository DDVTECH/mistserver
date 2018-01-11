#include "input_rtsp.h"

Mist::InputRTSP *classPointer = 0;
Socket::Connection *mainConn = 0;

void incomingPacket(const DTSC::Packet &pkt){
  classPointer->incoming(pkt);
}

/// Function used to send RTP packets over UDP
///\param socket A UDP Connection pointer, sent as a void*, to keep portability.
///\param data The RTP Packet that needs to be sent
///\param len The size of data
///\param channel Not used here, but is kept for compatibility with sendTCP
void sendUDP(void *socket, char *data, unsigned int len, unsigned int channel){
  ((Socket::UDPConnection *)socket)->SendNow(data, len);
  if (mainConn){mainConn->addUp(len);}
}

namespace Mist{
  InputRTSP::InputRTSP(Util::Config *cfg) : Input(cfg){
    TCPmode = true;
    sdpState.myMeta = &myMeta;
    sdpState.incomingPacketCallback = incomingPacket;
    classPointer = this;
    standAlone = false;
    seenSDP = false;
    cSeq = 0;
    capa["name"] = "RTSP";
    capa["decs"] = "Allows pulling from live RTSP sources";
    capa["source_match"].append("rtsp://*");
    // These can/may be set to always-on mode
    capa["always_match"].append("rtsp://*");
    capa["priority"] = 9ll;
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][0u].append("MPEG2");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("AC3");
    capa["codecs"][0u][1u].append("ALAW");
    capa["codecs"][0u][1u].append("ULAW");
    capa["codecs"][0u][1u].append("PCM");
    capa["codecs"][0u][1u].append("opus");
    capa["codecs"][0u][1u].append("MP2");

    JSON::Value option;
    option["arg"] = "integer";
    option["long"] = "buffer";
    option["short"] = "b";
    option["help"] = "DVR buffer time in ms";
    option["value"].append(50000LL);
    config->addOption("bufferTime", option);
    capa["optional"]["DVR"]["name"] = "Buffer time (ms)";
    capa["optional"]["DVR"]["help"] = "The target available buffer time for this live stream, in "
                                      "milliseconds. This is the time available to seek around in, "
                                      "and will automatically be extended to fit whole keyframes "
                                      "as well as the minimum duration needed for stable playback.";
    capa["optional"]["DVR"]["option"] = "--buffer";
    capa["optional"]["DVR"]["type"] = "uint";
    capa["optional"]["DVR"]["default"] = 50000LL;
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

  void InputRTSP::sendCommand(const std::string &cmd, const std::string &cUrl,
                              const std::string &body,
                              const std::map<std::string, std::string> *extraHeaders){
    ++cSeq;
    sndH.Clean();
    sndH.protocol = "RTSP/1.0";
    sndH.method = cmd;
    sndH.url = cUrl;
    sndH.body = body;
    if ((username.size() || password.size()) && authRequest.size()){
      sndH.auth(username, password, authRequest);
    }
    sndH.SetHeader("User-Agent", "MistServer " PACKAGE_VERSION);
    sndH.SetHeader("CSeq", JSON::Value((long long)cSeq).asString());
    if (session.size()){sndH.SetHeader("Session", session);}
    if (extraHeaders && extraHeaders->size()){
      for (std::map<std::string, std::string>::const_iterator it = extraHeaders->begin();
           it != extraHeaders->end(); ++it){
        sndH.SetHeader(it->first, it->second);
      }
    }
    sndH.SendRequest(tcpCon, "", true);
  }

  bool InputRTSP::checkArguments(){
    const std::string &inpt = config->getString("input");
    if (inpt.substr(0, 7) != "rtsp://"){
      FAIL_MSG("Unsupported RTSP URL: '%s'", inpt.c_str());
      return false;
    }
    const std::string &transport = config->getString("transport");
    if (transport != "TCP" && transport != "UDP" && transport != "tcp" && transport != "udp"){
      FAIL_MSG("Not a supported transport mode: %s", transport.c_str());
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
    tcpCon = Socket::Connection(url.host, url.getPort(), false);
    mainConn = &tcpCon;
    return tcpCon;
  }

  void InputRTSP::parseStreamHeader(){
    std::map<std::string, std::string> extraHeaders;
    sendCommand("OPTIONS", url.getUrl(), "");
    parsePacket();
    extraHeaders["Accept"] = "application/sdp";
    sendCommand("DESCRIBE", url.getUrl(), "", &extraHeaders);
    parsePacket();
    if (!seenSDP && authRequest.size() && (username.size() || password.size()) && tcpCon){
      INFO_MSG("Authenticating...");
      sendCommand("DESCRIBE", url.getUrl(), "", &extraHeaders);
      parsePacket();
    }
    if (!tcpCon || !seenSDP){
      FAIL_MSG("Could not get stream description!");
      return;
    }
    if (sdpState.tracks.size()){
      for (std::map<uint32_t, SDP::Track>::iterator it = sdpState.tracks.begin();
           it != sdpState.tracks.end(); ++it){
        transportSet = false;
        extraHeaders.clear();
        extraHeaders["Transport"] = it->second.generateTransport(it->first, url.host, TCPmode);
        sendCommand("SETUP", url.link(it->second.control).getUrl(), "", &extraHeaders);
        parsePacket();
        if (!tcpCon || !transportSet){
          FAIL_MSG("Could not setup track %s!", myMeta.tracks[it->first].getIdentifier().c_str());
          tcpCon.close();
          return;
        }
      }
    }
    INFO_MSG("Setup complete");
    extraHeaders.clear();
    extraHeaders["Range"] = "npt=0.000-";
    sendCommand("PLAY", url.getUrl(), "", &extraHeaders);
    if (!TCPmode){
      tcpCon.setBlocking(false);
      connectedAt = Util::epoch() + 2208988800ll;
    }
  }

  void InputRTSP::closeStreamSource(){
    sendCommand("TEARDOWN", url.getUrl(), "");
    tcpCon.close();
  }

  std::string InputRTSP::streamMainLoop(){
    uint64_t lastPing = Util::bootSecs();
    while (config->is_active && nProxy.userClient.isAlive() && parsePacket()){
      handleUDP();
      // keep going
      nProxy.userClient.keepAlive();
      if (Util::bootSecs() - lastPing > 30){
        sendCommand("GET_PARAMETER", url.getUrl(), "");
        lastPing = Util::bootSecs();
      }
    }
    if (!tcpCon){return "TCP connection closed";}
    if (!config->is_active){return "received deactivate signal";}
    if (!nProxy.userClient.isAlive()){return "buffer shutdown";}
    return "Unknown";
  }

  bool InputRTSP::parsePacket(){
    uint32_t waitTime = 500;
    if (!TCPmode){waitTime = 50;}
    do{
      // No new data? Sleep and retry, if connection still open
      if (!tcpCon.Received().size() || !tcpCon.Received().available(1)){
        if (!tcpCon.spool() && tcpCon && config->is_active && nProxy.userClient.isAlive()){
          nProxy.userClient.keepAlive();
          Util::sleep(waitTime);
          if (!TCPmode){return true;}
        }
        continue;
      }
      if (tcpCon.Received().copy(1) != "$"){
        // not a TCP RTP packet, read RTSP commands
        if (recH.Read(tcpCon)){
          if (recH.hasHeader("WWW-Authenticate")){
            authRequest = recH.GetHeader("WWW-Authenticate");
          }
          if (recH.url == "401"){
            INFO_MSG("Requires authentication");
            recH.Clean();
            return true;
          }
          if (recH.hasHeader("Content-Location")){
            url = HTTP::URL(recH.GetHeader("Content-Location"));
          }
          if (recH.hasHeader("Content-Base") && recH.GetHeader("Content-Base") != "" && recH.GetHeader("Content-Base") != url.getUrl()){
            INFO_MSG("Changing base URL from %s to %s", url.getUrl().c_str(), recH.GetHeader("Content-Base").c_str());
            url = HTTP::URL(recH.GetHeader("Content-Base"));
          }
          if (recH.hasHeader("Session")){
            session = recH.GetHeader("Session");
            if (session.find(';') != std::string::npos){
              session.erase(session.find(';'), std::string::npos);
            }
          }
          if (recH.hasHeader("Content-Type") &&
              recH.GetHeader("Content-Type") == "application/sdp"){
            INFO_MSG("Received SDP");
            seenSDP = true;
            sdpState.parseSDP(recH.body);
            recH.Clean();
            INFO_MSG("SDP contained %llu tracks", myMeta.tracks.size());
            return true;
          }
          if (recH.hasHeader("Transport")){
            INFO_MSG("Received setup response");
            uint32_t trackNo = sdpState.parseSetup(recH, url.host, "");
            if (trackNo){
              INFO_MSG("Parsed transport for track: %lu", trackNo);
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

          // Print anything possibly interesting to cerr
          std::cerr << recH.BuildRequest() << std::endl;
          recH.Clean();
          return true;
        }
        if (!tcpCon.spool() && tcpCon && config->is_active && nProxy.userClient.isAlive()){
          nProxy.userClient.keepAlive();
          Util::sleep(waitTime);
        }
        continue;
      }
      if (!tcpCon.Received().available(4)){
        if (!tcpCon.spool() && tcpCon && config->is_active && nProxy.userClient.isAlive()){
          nProxy.userClient.keepAlive();
          Util::sleep(waitTime);
        }
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
      uint32_t trackNo = sdpState.getTrackNoForChannel(chan);
      EXTREME_MSG("Received %ub RTP packet #%u on channel %u, time %llu", len,
                  (unsigned int)pkt.getSequence(), chan, pkt.getTimeStamp());
      if (!trackNo && (chan % 2) != 1){
        WARN_MSG("Received packet for unknown track number on channel %u", chan);
      }
      if (trackNo){sdpState.tracks[trackNo].rtpSeq = pkt.getSequence();}

      sdpState.handleIncomingRTP(trackNo, pkt);

      return true;

    }while (tcpCon && config->is_active && nProxy.userClient.isAlive());
    return false;
  }

  /// Reads and handles RTP packets over UDP, if needed
  bool InputRTSP::handleUDP(){
    if (TCPmode){return false;}
    bool r = false;
    for (std::map<uint32_t, SDP::Track>::iterator it = sdpState.tracks.begin();
         it != sdpState.tracks.end(); ++it){
      Socket::UDPConnection &s = it->second.data;
      while (s.Receive()){
        r = true;
        // if (s.getDestPort() != it->second.sPortA){
        //  // wrong sending port, ignore packet
        //  continue;
        //}
        tcpCon.addDown(s.data_len);
        RTP::Packet pack(s.data, s.data_len);
        if (!it->second.rtpSeq){it->second.rtpSeq = pack.getSequence();}
        // packet is very early - assume dropped after 30 packets
        while ((int16_t)(((uint16_t)it->second.rtpSeq) - ((uint16_t)pack.getSequence())) < -30){
          WARN_MSG("Giving up on packet %u", it->second.rtpSeq);
          ++(it->second.rtpSeq);
          ++(it->second.lostTotal);
          ++(it->second.lostCurrent);
          ++(it->second.packTotal);
          ++(it->second.packCurrent);
          // send any buffered packets we may have
          while (it->second.packBuffer.count(it->second.rtpSeq)){
            sdpState.handleIncomingRTP(it->first, pack);
            ++(it->second.rtpSeq);
            ++(it->second.packTotal);
            ++(it->second.packCurrent);
          }
        }
        // send any buffered packets we may have
        while (it->second.packBuffer.count(it->second.rtpSeq)){
          sdpState.handleIncomingRTP(it->first, pack);
          ++(it->second.rtpSeq);
          ++(it->second.packTotal);
          ++(it->second.packCurrent);
        }
        // packet is slightly early - buffer it
        if (((int16_t)(((uint16_t)it->second.rtpSeq) - ((uint16_t)pack.getSequence())) < 0)){
          INFO_MSG("Buffering early packet #%u->%u", it->second.rtpSeq, pack.getSequence());
          it->second.packBuffer[pack.getSequence()] = pack;
        }
        // packet is late
        if ((int16_t)(((uint16_t)it->second.rtpSeq) - ((uint16_t)pack.getSequence())) > 0){
          // negative difference?
          --(it->second.lostTotal);
          --(it->second.lostCurrent);
          ++(it->second.packTotal);
          ++(it->second.packCurrent);
          WARN_MSG("Dropped a packet that arrived too late! (%d packets difference)",
                   (int16_t)(((uint16_t)it->second.rtpSeq) - ((uint16_t)pack.getSequence())));
          return false;
        }
        // packet is in order
        if (it->second.rtpSeq == pack.getSequence()){
          sdpState.handleIncomingRTP(it->first, pack);
          ++(it->second.rtpSeq);
          ++(it->second.packTotal);
          ++(it->second.packCurrent);
          if (!it->second.theirSSRC){it->second.theirSSRC = pack.getSSRC();}
        }
      }
      if (Util::epoch() / 5 != it->second.rtcpSent){
        it->second.rtcpSent = Util::epoch() / 5;
        it->second.pack.sendRTCP_RR(connectedAt, it->second, it->first, myMeta, sendUDP);
      }
    }
    return r;
  }

  void InputRTSP::incoming(const DTSC::Packet &pkt){nProxy.bufferLivePacket(pkt, myMeta);}

}// namespace Mist

