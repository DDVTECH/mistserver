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

  /// Helper function for passing packets into the OutRTSP class
  void insertPacket(const DTSC::Packet &pkt){classPointer->incomingPacket(pkt);}

  /// Takes incoming packets and buffers them.
  void OutRTSP::incomingPacket(const DTSC::Packet &pkt){bufferLivePacket(pkt);}

  OutRTSP::OutRTSP(Socket::Connection &myConn) : Output(myConn){
    connectedAt = Util::epoch() + 2208988800ll;
    pausepoint = 0;
    setBlocking(false);
    maxSkipAhead = 0;
    expectTCP = false;
    checkPort = !config->getBool("ignsendport");
    lastTimeSync = 0;
    mainConn = &myConn;
    classPointer = this;
    sdpState.incomingPacketCallback = insertPacket;
    sdpState.myMeta = &myMeta;
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

  /// Function used to send RTP packets over TCP
  ///\param socket A TCP Connection pointer, sent as a void*, to keep portability.
  ///\param data The RTP Packet that needs to be sent
  ///\param len The size of data
  ///\param channel Used to distinguish different data streams when sending RTP over TCP
  void sendTCP(void *socket, char *data, unsigned int len, unsigned int channel){
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
    capa["desc"] =
        "Provides Real Time Streaming Protocol output, supporting both UDP and TCP transports.";
    capa["deps"] = "";
    capa["url_rel"] = "/$";
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

    capa["methods"][0u]["handler"] = "rtsp";
    capa["methods"][0u]["type"] = "rtsp";
    capa["methods"][0u]["priority"] = 2ll;

    capa["optional"]["maxsend"]["name"] = "Max RTP packet size";
    capa["optional"]["maxsend"]["help"] = "Maximum size of RTP packets in bytes";
    capa["optional"]["maxsend"]["default"] = (long long)RTP::MAX_SEND;
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
    unsigned int dataLen = 0;
    thisPacket.getString("data", dataPointer, dataLen);
    uint32_t tid = thisPacket.getTrackId();
    uint64_t timestamp = thisPacket.getTime();

    // if we're past the pausing point, seek to it, and pause immediately
    if (pausepoint && timestamp > pausepoint){
      pausepoint = 0;
      stop();
      return;
    }

    if (myMeta.live && lastTimeSync + 666 < timestamp){
      lastTimeSync = timestamp;
      updateMeta();
      if (liveSeek()){return;}
    }

    void *socket = 0;
    void (*callBack)(void *, char *, unsigned int, unsigned int) = 0;

    if (sdpState.tracks[tid].channel == -1){// UDP connection
      socket = &sdpState.tracks[tid].data;
      callBack = sendUDP;
      if (Util::epoch() / 5 != sdpState.tracks[tid].rtcpSent){
        sdpState.tracks[tid].rtcpSent = Util::epoch() / 5;
        sdpState.tracks[tid].pack.sendRTCP_SR(connectedAt, &sdpState.tracks[tid].rtcp, tid, myMeta,
                                           sendUDP);
      }
    }else{
      socket = &myConn;
      callBack = sendTCP;
    }

    uint64_t offset = thisPacket.getInt("offset");
    sdpState.tracks[tid].pack.setTimestamp((timestamp+offset) * SDP::getMultiplier(myMeta.tracks[tid]));
    sdpState.tracks[tid].pack.sendData(socket, callBack, dataPointer, dataLen,
                                       sdpState.tracks[tid].channel, myMeta.tracks[tid].codec);
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
      // cancel broken URLs
      if (HTTP_R.url.size() < 8){
        WARN_MSG("Invalid data found in RTSP input around ~%llub - disconnecting!",
                 myConn.dataDown());
        myConn.close();
        break;
      }
      HTTP_S.Clean();
      HTTP_S.protocol = "RTSP/1.0";

      // set the streamname and session
      if (!source.size()){
        std::string source = HTTP_R.url.substr(7);
        unsigned int loc = std::min(source.find(':'), source.find('/'));
        source = source.substr(0, loc);
      }
      size_t found = HTTP_R.url.find('/', 7);
      if (!streamName.size()){
        streamName = HTTP_R.url.substr(found + 1, HTTP_R.url.substr(found + 1).find('/'));
        Util::sanitizeName(streamName);
      }
      if (streamName.size()){
        HTTP_S.SetHeader("Session",
                         Secure::md5(HTTP_S.GetHeader("User-Agent") + getConnectedHost()) + "_" +
                             streamName);
      }

      //allow setting of max lead time through buffer variable.
      //max lead time is set in MS, but the variable is in integer seconds for simplicity.
      if (HTTP_R.GetVar("buffer") != ""){
        maxSkipAhead = JSON::Value(HTTP_R.GetVar("buffer")).asInt() * 1000;
      }
      //allow setting of play back rate through buffer variable.
      //play back rate is set in MS per second, but the variable is a simple multiplier.
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
        initialize();
        selectedTracks.clear();
        std::stringstream transportString;
        transportString << "v=0\r\n"
                           "o=- "
                        << Util::getMS() << " 1 IN IP4 127.0.0.1\r\n"
                                            "s="
                        << streamName << "\r\n"
                                         "c=IN IP4 0.0.0.0\r\n"
                                         "i="
                        << streamName << "\r\n"
                                         "u="
                        << HTTP_R.url.substr(0, HTTP_R.url.rfind('/')) << "/" << streamName
                        << "\r\n"
                           "t=0 0\r\n"
                           "a=tool:MistServer\r\n"
                           "a=type:broadcast\r\n"
                           "a=control:*\r\n";
        if (myMeta.live){
          transportString << "a=range:npt=" << ((double)startTime()) / 1000.0 << "-\r\n";
        }else{
          transportString << "a=range:npt=" << ((double)startTime()) / 1000.0 << "-"
                          << ((double)endTime()) / 1000.0 << "\r\n";
        }

        for (std::map<unsigned int, DTSC::Track>::iterator objIt = myMeta.tracks.begin();
             objIt != myMeta.tracks.end(); ++objIt){
          transportString << SDP::mediaDescription(objIt->second);
        }
        transportString << "\r\n";
        HIGH_MSG("Reply: %s", transportString.str().c_str());
        HTTP_S.SetHeader("Content-Base",
                         HTTP_R.url.substr(0, HTTP_R.url.rfind('/')) + "/" + streamName);
        HTTP_S.SetHeader("Content-Type", "application/sdp");
        HTTP_S.SetBody(transportString.str());
        HTTP_S.SendResponse("200", "OK", myConn);
        HTTP_R.Clean();
        continue;
      }
      if (HTTP_R.method == "SETUP"){
        uint32_t trackNo = sdpState.parseSetup(HTTP_R, getConnectedHost(), source);
        HTTP_S.SetHeader("Expires", HTTP_S.GetHeader("Date"));
        HTTP_S.SetHeader("Cache-Control", "no-cache");
        if (trackNo){
          selectedTracks.insert(trackNo);
          SDP::Track &sdpTrack = sdpState.tracks[trackNo];
          if (sdpTrack.channel != -1){expectTCP = true;}
          HTTP_S.SetHeader("Transport", sdpTrack.transportString);
          HTTP_S.SendResponse("200", "OK", myConn);
          INFO_MSG("Setup completed for track %lu (%s): %s", trackNo,
                   myMeta.tracks[trackNo].codec.c_str(), sdpTrack.transportString.c_str());
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
            uint64_t targetPos = 1000 * atof(range.c_str());
            if (targetPos || myMeta.vod){seek(targetPos);}
          }
        }
        std::stringstream rangeStr;
        if (myMeta.live){
          rangeStr << "npt=" << currentTime() / 1000 << "." << std::setw(3) << std::setfill('0')
                   << currentTime() % 1000 << "-";
        }else{
          rangeStr << "npt=" << currentTime() / 1000 << "." << std::setw(3) << std::setfill('0')
                   << currentTime() % 1000 << "-" << std::setw(1) << endTime() / 1000 << "."
                   << std::setw(3) << std::setfill('0') << endTime() % 1000;
        }
        HTTP_S.SetHeader("Range", rangeStr.str());
        std::stringstream infoString;
        if (selectedTracks.size()){
          for (std::set<unsigned long>::iterator it = selectedTracks.begin();
               it != selectedTracks.end(); ++it){
            if (!infoString.str().empty()){infoString << ",";}
            infoString << sdpState.tracks[*it].rtpInfo(myMeta.tracks[*it],
                                                       source + "/" + streamName, currentTime());
          }
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
          pausepoint = 1000 * (int)atof(range.c_str());
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
      WARN_MSG("Unhandled command %s:\n%s", HTTP_R.method.c_str(), HTTP_R.BuildRequest().c_str());
      HTTP_R.Clean();
    }
  }

  /// Disconnects the user
  bool OutRTSP::onFinish(){
    if (myConn){myConn.close();}
    return false;
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
    uint16_t len = ntohs(*(short *)(tcpHead.data() + 2));
    if (!myConn.Received().available(len + 4)){
      return false;
    }// a TCP RTP packet, but not complete yet
    // remove whole packet from buffer, including 4 byte header
    std::string tcpPacket = myConn.Received().remove(len + 4);
    uint32_t trackNo = sdpState.getTrackNoForChannel(tcpHead.data()[1]);
    if (trackNo && isPushing()){
      RTP::Packet pkt(tcpPacket.data() + 4, len);
      sdpState.tracks[trackNo].rtpSeq = pkt.getSequence();
      sdpState.handleIncomingRTP(trackNo, pkt);
    }
    // attempt to read more packets
    return handleTCP();
  }

  /// Reads and handles RTP packets over UDP, if needed
  void OutRTSP::handleUDP(){
    if (!isPushing()){return;}
    for (std::map<uint32_t, SDP::Track>::iterator it = sdpState.tracks.begin();
         it != sdpState.tracks.end(); ++it){
      Socket::UDPConnection &s = it->second.data;
      while (s.Receive()){
        if (s.getDestPort() != it->second.cPortA && checkPort){
          // wrong sending port, ignore packet
          continue;
        }
        lastRecv = Util::epoch(); // prevent disconnect of idle TCP connection when using UDP
        myConn.addDown(s.data_len);
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
          return;
        }
        // packet is in order
        if (it->second.rtpSeq == pack.getSequence()){
          sdpState.handleIncomingRTP(it->first, pack);
          ++(it->second.rtpSeq);
          ++(it->second.packTotal);
          ++(it->second.packCurrent);
          if (!it->second.theirSSRC){
            it->second.theirSSRC = pack.getSSRC();
          }
        }
      }
      if (Util::epoch() / 5 != it->second.rtcpSent){
        it->second.rtcpSent = Util::epoch() / 5;
        it->second.pack.sendRTCP_RR(connectedAt, it->second, it->first, myMeta, sendUDP);
      }
    }
  }
}

