#include "analyser_rtsp.h"

AnalyserRTSP *classPointer = 0;

void incomingPacket(const DTSC::Packet &pkt){
  classPointer->incoming(pkt);
}

void AnalyserRTSP::init(Util::Config &conf){
  Analyser::init(conf);
}

void AnalyserRTSP::parseStreamHeader(){
//    tcpCon.setBlocking(false);
    std::map<std::string, std::string> extraHeaders;
    sendCommand("OPTIONS", uri.getURI().getUrl(), "");
    extraHeaders["Accept"] = "application/sdp";
    sendCommand("DESCRIBE", uri.getURI().getUrl(), "", &extraHeaders);
    if (uri.isEOF()|| !seenSDP){
      FAIL_MSG("Could not get stream description!");
      return;
    }
    if (sdpState.tracks.size()){
      for (std::map<uint64_t, SDP::Track>::iterator it = sdpState.tracks.begin();
           it != sdpState.tracks.end(); ++it){
//        transportSet = false;
        extraHeaders.clear();
//        extraHeaders["Transport"] = it->second.generateTransport(it->first, url.host, TCPmode);
        sendCommand("SETUP", uri.getURI().link(it->second.control).getUrl(), "", &extraHeaders);

//        if (!tcpCon || !transportSet){
//          FAIL_MSG("Could not setup track %s!", myMeta.tracks[it->first].getIdentifier().c_str());
//          tcpCon.close();
//          return;
//        }
      }
    }
    INFO_MSG("Setup complete");
    extraHeaders.clear();
    extraHeaders["Range"] = "npt=0.000-";
    sendCommand("PLAY", uri.getURI().getUrl(), "", &extraHeaders);
/*
if (!TCPmode){
      connectedAt = Util::epoch() + 2208988800ll;
    }else{
      tcpCon.setBlocking(true);
    }
    */
  }


void AnalyserRTSP::sendCommand(const std::string &cmd, const std::string &cUrl,
                              const std::string &body,
                              const std::map<std::string, std::string> *extraHeaders,
                              bool reAuth){
    ++cSeq;
    sndH.Clean();
    sndH.protocol = "RTSP/1.0";
    sndH.method = cmd;
    sndH.url = cUrl;
    sndH.body = body;
    /*
    if ((username.size() || password.size()) && authRequest.size()){
      sndH.auth(username, password, authRequest);
    }
    */
    sndH.SetHeader("User-Agent", "MistServer " PACKAGE_VERSION);
    sndH.SetHeader("CSeq", JSON::Value(cSeq).asString());
    if (session.size()){sndH.SetHeader("Session", session);}
    if (extraHeaders && extraHeaders->size()){
      for (std::map<std::string, std::string>::const_iterator it = extraHeaders->begin();
           it != extraHeaders->end(); ++it){
        sndH.SetHeader(it->first, it->second);
      }
    }
   //sndH.SendRequest(tcpCon, "", true);

    parsePacket();

/*
    if (reAuth && needAuth && authRequest.size() && (username.size() || password.size()) && tcpCon){
      INFO_MSG("Authenticating %s...", cmd.c_str());
      sendCommand(cmd, cUrl, body, extraHeaders, false);
      if (needAuth){
        FAIL_MSG("Authentication failed! Are the provided credentials correct?");
      }
    }
    */
  }

void AnalyserRTSP::incoming(const DTSC::Packet &pkt){
  char *dataPtr;
  size_t dataSize;
  pkt.getString("data", dataPtr, dataSize);
  DETAIL_MED("Received %zub %sfor track %lu (%s) @ %" PRIu64 "ms", dataSize, pkt.getFlag("keyframe")?"keyframe ":"", pkt.getTrackId(),
             "", pkt.getTime());
  if (detail >= 8){
    for (uint32_t i = 0; i < dataSize; ++i){
      std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)dataPtr[i] << " ";
      if (i % 32 == 31){std::cout << std::endl;}
    }
    std::cout << std::endl;
  }
}

/*
bool AnalyserRTSP::open(const std::string &filename){
  if (!Analyser::open(filename)){return false;}
  myConn.open(1, 0);
  return true;
}
*/

AnalyserRTSP::AnalyserRTSP(Util::Config &conf) : Analyser(conf){
  sdpState.myMeta = &myMeta;
  sdpState.incomingPacketCallback = incomingPacket;
  classPointer = this;
}

bool AnalyserRTSP::isOpen(){
return !uri.isEOF();
  
//return myConn;
}

bool AnalyserRTSP::parsePacket(){
  do{

  //INFO_MSG("in loop");
    // No new data? Sleep and retry, if connection still open
        //        uri.readSome(bytesNeeded - buffer.bytes(bytesNeeded), *this);
        if (buffer.available(1)) {
          if (buffer.copy(1) != "$") {
            //read rtsp commands
//            INFO_MSG("rtsp commands");

//            std::cout << std::endl << std::endl << "dump buffer: " << buffer.get().c_str() << std::endl;
//            FAIL_MSG("buffer size: %llu", buffer.bytes(0xffffffff));
//            HTTP.Clean();

            if (true){//HTTP.Read(buffer, isOpen())) {

              //FAIL_MSG("whole header received, bytes: %d", HTTP.body.size());
//              INFO_MSG("body: %s", HTTP.body.c_str());
//              std::cout << std::endl << "BODY: " << HTTP.body.c_str() << std::endl;

              //FAIL_MSG("buffer: %s", buffer.get().c_str());

                if (HTTP.hasHeader("Content-Type") && HTTP.GetHeader("Content-Type") == "application/sdp"){
                  sdpState.parseSDP(HTTP.body);
                  std::cout << "HTTP BODY: " << HTTP.body.c_str() << std::endl << std::endl;
                  HTTP.Clean();
                  FAIL_MSG("SDP HEADER DATA");
                  Util::sleep(1000);
                  return true;
                }
                
                if (HTTP.hasHeader("Transport")) {
                  uint32_t trackNo = sdpState.parseSetup(HTTP, "", "");
                  if (trackNo) {
                    DETAIL_MED("Parsed transport for track: %" PRIu32, trackNo);
                    INFO_MSG("Parsed transport for track: %" PRIu32, trackNo);
                  } else {
                    DETAIL_MED("Could not parse transport string!");
                    FAIL_MSG("Could not parse transport string!");
                  }
                  Util::sleep(1000);
                  HTTP.Clean();
                  return true;
                }

                std::cout << "REQ: " << HTTP.BuildRequest() << std::endl;

                  Util::sleep(1000);
                HTTP.Clean();
                return true;

            }

            uri.readSome(1024, *this);
          }

          //rtsp data
          
          FAIL_MSG("rtsp data, buf size: %" PRIu32, buffer.bytes(0xffffffff));
          Util::sleep(1000);
          
          if (!buffer.available(4)){
            INFO_MSG("not enough data");
            uri.readSome(4, *this);
            continue;
          }// a TCP RTP packet, but not complete yet

          // We have a TCP packet! Read it...
          // Format: 1 byte '$', 1 byte channel, 2 bytes len, len bytes binary data
          std::string tcpHead = buffer.copy(4);
          uint16_t len = ntohs(*(short *)(tcpHead.data() + 2));
          if (!buffer.available(len + 4)){
            uri.readSome(len+4, *this);
            continue;
          }// a TCP RTP packet, but not complete yet
          // remove whole packet from buffer, including 4 byte header
          std::string tcpPacket = buffer.remove(len + 4);
          RTP::Packet pkt(tcpPacket.data() + 4, len);
          uint8_t chan = tcpHead.data()[1];
          uint32_t trackNo = sdpState.getTrackNoForChannel(chan);
          DETAIL_HI("Received %ub RTP packet #%u on channel %u, time %" PRIu32, len,
                    (unsigned int)pkt.getSequence(), chan, pkt.getTimeStamp());
          if (!trackNo && (chan % 2) != 1){
            DETAIL_MED("Received packet for unknown track number on channel %u", chan);
          }
          if (trackNo){
            sdpState.tracks[trackNo].sorter.rtpSeq = pkt.getSequence();
          }

          if (detail >= 10){
            char *pl = pkt.getPayload();
            uint32_t payLen = pkt.getPayloadSize();
            for (uint32_t i = 0; i < payLen; ++i){
              std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)pl[i] << " ";
              if (i % 32 == 31){std::cout << std::endl;}
            }
            std::cout << std::endl;
          }

          //WARN_MSG("handle sdp");
          sdpState.handleIncomingRTP(trackNo, pkt);

          
          //WARN_MSG("rtsp data, buf size: %llu", buffer.bytes(0xffffffff));
          return true;


        }else{
          WARN_MSG("no data in buffer");
          uri.readSome(1024, *this);
        }
      
//        uri.readSome(2048, *this);
/*
  if (!myConn.Received().size() || !myConn.Received().available(1)){
      if (!myConn.spool() && isOpen()){
        INFO_MSG("sleeping");
      Util::sleep(500);

      }
      continue;
    }
*/


/*
    INFO_MSG("a");
    if (myConn.Received().copy(1) != "$"){
      // not a TCP RTP packet, read RTSP commands
      if (HTTP.Read(myConn)){
        if (HTTP.hasHeader("Content-Type") && HTTP.GetHeader("Content-Type") == "application/sdp"){
          sdpState.parseSDP(HTTP.body);
          HTTP.Clean();
          return true;
        }
        if (HTTP.hasHeader("Transport")){
          uint32_t trackNo = sdpState.parseSetup(HTTP, "", "");
          if (trackNo){
            DETAIL_MED("Parsed transport for track: %lu", trackNo);
          }else{
            DETAIL_MED("Could not parse transport string!");
          }
          HTTP.Clean();
          return true;
        }

        std::cout << HTTP.BuildRequest() << std::endl;

        HTTP.Clean();
        return true;
      }else{
        if (!myConn.spool() && isOpen()){
        }
      }
      continue;
    }
    if (!myConn.Received().available(4)){
      if (!myConn.spool() && isOpen()){
      INFO_MSG("sleeping 2");
      Util::sleep(500);}
      continue;
    }// a TCP RTP packet, but not complete yet

    // We have a TCP packet! Read it...
    // Format: 1 byte '$', 1 byte channel, 2 bytes len, len bytes binary data
    std::string tcpHead = myConn.Received().copy(4);
    uint16_t len = ntohs(*(short *)(tcpHead.data() + 2));
    if (!myConn.Received().available(len + 4)){
      if (!myConn.spool() && isOpen()){
      INFO_MSG("sleeping 3");
      Util::sleep(500);}
      continue;
    }// a TCP RTP packet, but not complete yet
    // remove whole packet from buffer, including 4 byte header
    std::string tcpPacket = myConn.Received().remove(len + 4);
    RTP::Packet pkt(tcpPacket.data() + 4, len);
    uint8_t chan = tcpHead.data()[1];
    uint32_t trackNo = sdpState.getTrackNoForChannel(chan);
    DETAIL_HI("Received %ub RTP packet #%u on channel %u, time %llu", len,
              (unsigned int)pkt.getSequence(), chan, pkt.getTimeStamp());
    if (!trackNo && (chan % 2) != 1){
      DETAIL_MED("Received packet for unknown track number on channel %u", chan);
    }
    if (trackNo){
      sdpState.tracks[trackNo].sorter.rtpSeq = pkt.getSequence();
    }

    if (detail >= 10){
      char *pl = pkt.getPayload();
      uint32_t payLen = pkt.getPayloadSize();
      for (uint32_t i = 0; i < payLen; ++i){
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)pl[i] << " ";
        if (i % 32 == 31){std::cout << std::endl;}
      }
      std::cout << std::endl;
    }

    sdpState.handleIncomingRTP(trackNo, pkt);

    return true;
    */

    
  }while (!uri.isEOF() && buffer.size() <= 0);
  FAIL_MSG("End of file");
  return false;
}

void AnalyserRTSP::dataCallback(const char *ptr, size_t size) {
//  INFO_MSG("appending buffer, size: %d, buffersize: %llu", size, buffer.bytes(0xffffffff));
  buffer.append(ptr, size);
//  INFO_MSG("buffer: %s", tmp.c_str());
//  Util::sleep(100);
}
