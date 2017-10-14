#include "analyser_rtsp.h"

AnalyserRTSP *classPointer = 0;

void incomingPacket(const DTSC::Packet &pkt){
  classPointer->incoming(pkt);
}

void AnalyserRTSP::init(Util::Config &conf){
  Analyser::init(conf);
}

void AnalyserRTSP::incoming(const DTSC::Packet &pkt){
  char *dataPtr;
  uint32_t dataSize;
  pkt.getString("data", dataPtr, dataSize);
  DETAIL_MED("Received %ub %sfor track %lu (%s) @ %llums", dataSize, pkt.getFlag("keyframe")?"keyframe ":"", pkt.getTrackId(),
             myMeta.tracks[pkt.getTrackId()].getIdentifier().c_str(), pkt.getTime());
  if (detail >= 8){
    for (uint32_t i = 0; i < dataSize; ++i){
      std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)dataPtr[i] << " ";
      if (i % 32 == 31){std::cout << std::endl;}
    }
    std::cout << std::endl;
  }
}

AnalyserRTSP::AnalyserRTSP(Util::Config &conf) : Analyser(conf){
  myConn = Socket::Connection(1, 0);
  sdpState.myMeta = &myMeta;
  sdpState.incomingPacketCallback = incomingPacket;
  classPointer = this;
}

bool AnalyserRTSP::isOpen(){
  return myConn;
}

bool AnalyserRTSP::parsePacket(){
  do{
    // No new data? Sleep and retry, if connection still open
    if (!myConn.Received().size() || !myConn.Received().available(1)){
      if (!myConn.spool() && isOpen()){Util::sleep(500);}
      continue;
    }
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
        if (!myConn.spool() && isOpen()){Util::sleep(500);}
      }
      continue;
    }
    if (!myConn.Received().available(4)){
      if (!myConn.spool() && isOpen()){Util::sleep(500);}
      continue;
    }// a TCP RTP packet, but not complete yet

    // We have a TCP packet! Read it...
    // Format: 1 byte '$', 1 byte channel, 2 bytes len, len bytes binary data
    std::string tcpHead = myConn.Received().copy(4);
    uint16_t len = ntohs(*(short *)(tcpHead.data() + 2));
    if (!myConn.Received().available(len + 4)){
      if (!myConn.spool() && isOpen()){Util::sleep(500);}
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
      sdpState.tracks[trackNo].rtpSeq = pkt.getSequence();
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

  }while (isOpen());
  return false;
}

