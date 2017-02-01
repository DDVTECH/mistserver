#include <mist/defines.h>
#include <mist/auth.h>
#include <mist/encode.h>
#include <mist/stream.h>
#include <mist/bitfields.h>
#include <mist/bitstream.h>
#include <mist/adts.h>
#include <mist/triggers.h>
#include "output_rtsp.h"
#include <sys/stat.h>

namespace Mist {

  Socket::Connection * mainConn = 0;

  OutRTSP::OutRTSP(Socket::Connection & myConn) : Output(myConn){
    connectedAt = Util::epoch() + 2208988800ll;
    pausepoint = 0;
    setBlocking(false);
    maxSkipAhead = 0;
    minSkipAhead = 0;
    expectTCP = false;
    isPushing = false;
    lastTimeSync = 0;
    mainConn = &myConn;
  }
  
  /// Function used to send RTP packets over UDP
  ///\param socket A UDP Connection pointer, sent as a void*, to keep portability.
  ///\param data The RTP Packet that needs to be sent
  ///\param len The size of data
  ///\param channel Not used here, but is kept for compatibility with sendTCP
  void sendUDP(void * socket, char * data, unsigned int len, unsigned int channel) {
    ((Socket::UDPConnection *) socket)->SendNow(data, len);
    if (mainConn){mainConn->addUp(len);}
  }


  /// Function used to send RTP packets over TCP
  ///\param socket A TCP Connection pointer, sent as a void*, to keep portability.
  ///\param data The RTP Packet that needs to be sent
  ///\param len The size of data
  ///\param channel Used to distinguish different data streams when sending RTP over TCP
  void sendTCP(void * socket, char * data, unsigned int len, unsigned int channel) {
    //1 byte '$', 1 byte channel, 2 bytes length
    char buf[] = "$$$$";
    buf[1] = channel;
    ((short *) buf)[1] = htons(len);
    ((Socket::Connection *) socket)->SendNow(buf, 4);
    ((Socket::Connection *) socket)->SendNow(data, len);
  }

  void OutRTSP::init(Util::Config * cfg){
    Output::init(cfg);
    capa["name"] = "RTSP";
    capa["desc"] = "Provides Real Time Streaming Protocol output, supporting both UDP and TCP transports.";
    capa["deps"] = "";
    capa["url_rel"] = "/$";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("AC3");
    
    capa["methods"][0u]["handler"] = "rtsp";
    capa["methods"][0u]["type"] = "rtsp";
    capa["methods"][0u]["priority"] = 2ll;
    
    capa["optional"]["maxsend"]["name"] = "Max RTP packet size";
    capa["optional"]["maxsend"]["help"] = "Maximum size of RTP packets in bytes";
    capa["optional"]["maxsend"]["default"] = (long long)RTP::MAX_SEND;
    capa["optional"]["maxsend"]["type"] = "uint";
    capa["optional"]["maxsend"]["option"] = "--max-packet-size";
    capa["optional"]["maxsend"]["short"] = "m";

    cfg->addConnectorOptions(5554, capa);
    config = cfg;
  }

  bool OutRTSP::isReadyForPlay(){
    if (isPushing){
      return true;
    }
    return Output::isReadyForPlay();
  }

  void OutRTSP::sendNext(){
    char * dataPointer = 0;
    unsigned int dataLen = 0;
    thisPacket.getString("data", dataPointer, dataLen);
    uint32_t tid = thisPacket.getTrackId();
    uint64_t timestamp = thisPacket.getTime();
    
    //if we're past the pausing point, seek to it, and pause immediately
    if (pausepoint && timestamp > pausepoint){
      pausepoint = 0;
      stop();
      return;
    }


    if (myMeta.live && lastTimeSync + 666 < timestamp){
      lastTimeSync = timestamp;
      updateMeta();
      DTSC::Track & mainTrk = myMeta.tracks[getMainSelectedTrack()];
      // The extra 1000ms here is for the metadata sync delay.
      // It can be removed once we get rid of that.
      if (timestamp + 1000 + needsLookAhead < mainTrk.keys.rbegin()->getTime() && mainTrk.lastms - mainTrk.keys.rbegin()->getTime() > needsLookAhead){
        INFO_MSG("Skipping forward %llums (%llu ms LA)", mainTrk.keys.rbegin()->getTime() - thisPacket.getTime(), needsLookAhead);
        seek(mainTrk.keys.rbegin()->getTime());
        return;
      }
    }
    
    void * socket = 0;
    void (*callBack)(void *, char *, unsigned int, unsigned int) = 0;
    
    if (tracks[tid].channel == -1){//UDP connection
      socket = &tracks[tid].data;
      callBack = sendUDP;
      if (Util::epoch()/5 != tracks[tid].rtcpSent){
        tracks[tid].rtcpSent = Util::epoch()/5;
        tracks[tid].pack.sendRTCP(connectedAt, &tracks[tid].rtcp, tid, myMeta, sendUDP);
      }
    }else{
      socket = &myConn;
      callBack = sendTCP;
    }
    
    if(myMeta.tracks[tid].codec == "MP3"){
      tracks[tid].pack.setTimestamp(timestamp * 90);
      tracks[tid].pack.sendData(socket, callBack, dataPointer, dataLen, tracks[tid].channel, "MP3");
      return;
    }

    if( myMeta.tracks[tid].codec == "AC3" || myMeta.tracks[tid].codec == "AAC"){
      tracks[tid].pack.setTimestamp(timestamp * ((double) myMeta.tracks[tid].rate / 1000.0));
      tracks[tid].pack.sendData(socket, callBack, dataPointer, dataLen, tracks[tid].channel,myMeta.tracks[tid].codec);
      return;
    }

    if(myMeta.tracks[tid].codec == "H264"){
      long long offset = thisPacket.getInt("offset");
      tracks[tid].pack.setTimestamp(90 * (timestamp + offset));
      unsigned long sent = 0;
      while (sent < dataLen) {
        unsigned long nalSize = ntohl(*((unsigned long *)(dataPointer + sent)));
        tracks[tid].pack.sendH264(socket, callBack, dataPointer + sent + 4, nalSize, tracks[tid].channel);
        sent += nalSize + 4;
      }
      return;
    }
    
  }

  std::string OutRTSP::getStatsName(){
    if (isPushing){
      return "INPUT";
    }else{
      return Output::getStatsName();
    }
  }

  /// This request handler also checks for UDP packets
  void OutRTSP::requestHandler(){
    if (!expectTCP){
      handleUDP();
    }
    Output::requestHandler();
  }

  void OutRTSP::onRequest(){
    RTP::MAX_SEND = config->getInteger("maxsend");
    //if needed, parse TCP packets, and cancel if it is not safe (yet) to read HTTP/RTSP packets
    while ((!expectTCP || handleTCP()) && HTTP_R.Read(myConn)){
      //cancel broken URLs
      if (HTTP_R.url.size() < 8){
        WARN_MSG("Invalid data found in RTSP input around ~%llub - disconnecting!", myConn.dataDown());
        myConn.close();
        break;
      }
      HTTP_S.Clean();
      HTTP_S.protocol = "RTSP/1.0";
      
      //set the streamname and session
      if (!source.size()){
        std::string source = HTTP_R.url.substr(7);
        unsigned int loc = std::min(source.find(':'),source.find('/'));
        source = source.substr(0,loc);
      }
      size_t found = HTTP_R.url.find('/', 7);
      if (!streamName.size()){
        streamName = HTTP_R.url.substr(found + 1, HTTP_R.url.substr(found + 1).find('/'));
        Util::sanitizeName(streamName);
      }
      if (streamName.size()){
        HTTP_S.SetHeader("Session", Secure::md5(HTTP_S.GetHeader("User-Agent") + getConnectedHost()) + "_" + streamName);
      }
      
      //set the date
      time_t timer;
      time(&timer);
      struct tm * timeNow = gmtime(&timer);
      char dString[42];
      strftime(dString, 42, "%a, %d %h %Y, %X GMT", timeNow);
      HTTP_S.SetHeader("Date", dString);
      
      //set the sequence number to match the received sequence number
      if (HTTP_R.hasHeader("CSeq")){
        HTTP_S.SetHeader("CSeq", HTTP_R.GetHeader("CSeq"));
      }
      if (HTTP_R.hasHeader("Cseq")){
        HTTP_S.SetHeader("CSeq", HTTP_R.GetHeader("Cseq"));
      }
      
      INFO_MSG("Handling %s", HTTP_R.method.c_str());

      //handle the request
      if (HTTP_R.method == "OPTIONS"){
        HTTP_S.SetHeader("Public", "SETUP, TEARDOWN, PLAY, PAUSE, DESCRIBE, GET_PARAMETER, ANNOUNCE, RECORD");
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
        "o=- " << Util::getMS() << " 1 IN IP4 127.0.0.1\r\n"
        "s=" << streamName << "\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "i=" << streamName << "\r\n"
        "u=" << HTTP_R.url.substr(0, HTTP_R.url.rfind('/')) << "/" << streamName << "\r\n"
        "t=0 0\r\n"
        "a=tool:MistServer\r\n"
        "a=type:broadcast\r\n"
        "a=control:*\r\n";
        if (myMeta.live){
          transportString << "a=range:npt=" << ((double)startTime()) / 1000.0 << "-\r\n";
        }else{
          transportString << "a=range:npt=" << ((double)startTime()) / 1000.0 << "-" << ((double)endTime()) / 1000.0 << "\r\n";
        }
        
        for (std::map<unsigned int, DTSC::Track>::iterator objIt = myMeta.tracks.begin(); objIt != myMeta.tracks.end(); ++objIt) {
          transportString << tracks[objIt->first].mediaDescription(objIt->second);
        }
        transportString << "\r\n";
        HIGH_MSG("Reply: %s", transportString.str().c_str());
        HTTP_S.SetHeader("Content-Base", HTTP_R.url.substr(0, HTTP_R.url.rfind('/')) + "/" + streamName);
        HTTP_S.SetHeader("Content-Type", "application/sdp");
        HTTP_S.SetBody(transportString.str());
        HTTP_S.SendResponse("200", "OK", myConn);
        HTTP_R.Clean();
        continue;
      }
      if (HTTP_R.method == "SETUP"){
        size_t trackPos = HTTP_R.url.rfind("/track");
        if (trackPos != std::string::npos){
          unsigned int trId = atol(HTTP_R.url.substr(trackPos + 6).c_str());
          if (myMeta.tracks.count(trId)){
            if (tracks[trId].parseTransport(HTTP_R.GetHeader("Transport"), getConnectedHost(), source, myMeta.tracks[trId])){
              selectedTracks.insert(trId);
              HTTP_S.SetHeader("Expires", HTTP_S.GetHeader("Date"));
              HTTP_S.SetHeader("Transport", tracks[trId].transportString);
              HTTP_S.SetHeader("Cache-Control", "no-cache");
              HTTP_S.SendResponse("200", "OK", myConn);
            }else{
              HTTP_S.SendResponse("404", "Track not available", myConn);
            }
            HTTP_R.Clean();
            continue;
          }
        }
        //might be push setup - check known control points
        if (isPushing && tracks.size()){
          bool setupHandled = false;
          for (std::map<int, RTPTrack>::iterator it = tracks.begin(); it != tracks.end(); ++it){
            if (it->second.control.size() && (HTTP_R.url.find(it->second.control) != std::string::npos || HTTP_R.GetVar("pass").find(it->second.control) != std::string::npos)){
              if (it->second.parseTransport(HTTP_R.GetHeader("Transport"), getConnectedHost(), source, myMeta.tracks[it->first])){
                if (it->second.channel != -1){
                  expectTCP = true;
                }
                HTTP_S.SetHeader("Expires", HTTP_S.GetHeader("Date"));
                HTTP_S.SetHeader("Transport", it->second.transportString);
                HTTP_S.SetHeader("Cache-Control", "no-cache");
                HTTP_S.SendResponse("200", "OK", myConn);
                INFO_MSG("Setup completed for track %llu (%s): %s", it->first, myMeta.tracks[it->first].codec.c_str(), it->second.transportString.c_str());
              }else{
                HTTP_S.SendResponse("404", "Track not known or allowed", myConn);
              }
              setupHandled = true;
              HTTP_R.Clean();
              break;
            }
          }
          if (!setupHandled){
            HTTP_S.SendResponse("404", "Track not known", myConn);
            HTTP_R.Clean();
          }
          continue;
        }
        FAIL_MSG("Could not handle setup: pushing=%s, trackSize=%u", isPushing?"true":"false", tracks.size());
      }
      if (HTTP_R.method == "PLAY"){
        initialSeek();
        std::string range = HTTP_R.GetHeader("Range");
        if (range != ""){
          range = range.substr(range.find("npt=")+4);
          if (!range.empty()) {
            range = range.substr(0, range.find('-'));
            uint64_t targetPos = 1000*atof(range.c_str());
            if (targetPos){seek(targetPos);}
          }
        }
        std::stringstream rangeStr;
        if (myMeta.live){
          rangeStr << "npt=" << currentTime()/1000 << "." << std::setw(3) << std::setfill('0') << currentTime()%1000 << "-";
        }else{
          rangeStr << "npt=" << currentTime()/1000 << "." << std::setw(3) << std::setfill('0') << currentTime()%1000 << "-" << std::setw(1) << endTime()/1000 << "." << std::setw(3) << std::setfill('0') << endTime()%1000;
        }
        HTTP_S.SetHeader("Range", rangeStr.str());
        std::stringstream infoString;
        if (selectedTracks.size()){
          for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); ++it){
            if (!infoString.str().empty()){infoString << ",";}
            infoString << tracks[*it].rtpInfo(myMeta.tracks[*it], source+"/"+streamName, currentTime());
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
        if (!range.empty()){
          range = range.substr(range.find("npt=")+4);
        }
        if (range.empty()){
          stop();
        }else{
          pausepoint = 1000 * (int) atof(range.c_str());
          if (pausepoint > currentTime()){pausepoint = 0; stop();}
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
        std::string smp = streamName.substr(0,(streamName.find_first_of("+ ")));
        IPC::sharedPage serverCfg(SHM_CONF, DEFAULT_CONF_PAGE_SIZE); ///< Contains server configuration and capabilities
        IPC::semaphore configLock(SEM_CONF, O_CREAT | O_RDWR, ACCESSPERMS, 1);
        configLock.wait();
        
        DTSC::Scan streamCfg = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("streams").getMember(smp);
        if (streamCfg){
          if (streamCfg.getMember("source").asString().substr(0, 7) != "push://"){
            FAIL_MSG("Push rejected - stream %s not a push-able stream. (%s != push://*)", streamName.c_str(), streamCfg.getMember("source").asString().c_str());
            onFinish();
          }else{
            std::string source = streamCfg.getMember("source").asString().substr(7);
            std::string IP = source.substr(0, source.find('@'));
            /*LTS-START*/
            std::string password;
            if (source.find('@') != std::string::npos){
              password = source.substr(source.find('@')+1);
              if (password != ""){
                if (password == HTTP_R.GetVar("pass")){
                  INFO_MSG("Password accepted - ignoring IP settings.");
                  IP = "";
                }else{
                  INFO_MSG("Password rejected - checking IP.");
                  if (IP == ""){
                    IP = "deny-all.invalid";
                  }
                }
              }
            }
            if(Triggers::shouldTrigger("STREAM_PUSH", smp)){
              std::string payload = streamName+"\n" + getConnectedHost() +"\n"+capa["name"].asStringRef()+"\n"+reqUrl;
              if (!Triggers::doTrigger("STREAM_PUSH", payload, smp)){
                FAIL_MSG("Push from %s to %s rejected - STREAM_PUSH trigger denied the push", getConnectedHost().c_str(), streamName.c_str());
                onFinish();
                configLock.post();
                configLock.close();
                return;
              }
            }
            /*LTS-END*/
            if (IP != ""){
              if (!myConn.isAddress(IP)){
                FAIL_MSG("Push from %s to %s rejected - source host not whitelisted", getConnectedHost().c_str(), streamName.c_str());
                onFinish();
              }
            }
          }
        }else{
          FAIL_MSG("Push from %s rejected - stream '%s' not configured.", getConnectedHost().c_str(), streamName.c_str());
          onFinish();
        }
        configLock.post();
        configLock.close();
        if (!myConn){return;}//do not initialize if rejected
        isPushing = true;
        initialize();
        INFO_MSG("Pushing to stream %s", streamName.c_str());
        parseSDP(HTTP_R.body);
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
    if (myConn){
      myConn.close();
    }
    return false;
  }

  /// Attempts to parse TCP RTP packets at the beginning of the header.
  /// Returns whether it is safe to attempt to read HTTP/RTSP packets (true) or not (false).
  bool OutRTSP::handleTCP(){
    if (!myConn.Received().size() || !myConn.Received().available(1)){return false;}//no data
    if (myConn.Received().copy(1) != "$"){return true;}//not a TCP RTP packet
    if (!myConn.Received().available(4)){return false;}//a TCP RTP packet, but not complete yet
    //We have a TCP packet! Read it...
    //Format: 1 byte '$', 1 byte channel, 2 bytes len, len bytes binary data
    std::string tcpHead = myConn.Received().copy(4);
    uint16_t len = ntohs(*(short*)(tcpHead.data()+2));
    if (!myConn.Received().available(len+4)){return false;}//a TCP RTP packet, but not complete yet
    //remove whole packet from buffer, including 4 byte header
    std::string tcpPacket = myConn.Received().remove(len+4);
    for (std::map<int, RTPTrack>::iterator it = tracks.begin(); it != tracks.end(); ++it){
      if (tcpHead.data()[1] == it->second.channel){
        RTP::Packet pkt(tcpPacket.data()+4, len);
        it->second.rtpSeq = pkt.getSequence();
        handleIncomingRTP(it->first, pkt);
        break;
      }
    }
    //attempt to read more packets
    return handleTCP();
  }

  /// Reads and handles RTP packets over UDP, if needed
  void OutRTSP::handleUDP(){
    for (std::map<int, RTPTrack>::iterator it = tracks.begin(); it != tracks.end(); ++it){
      Socket::UDPConnection & s = it->second.data;
      while (s.Receive()){
        if (s.getDestPort() != it->second.cPort){
          //wrong sending port, ignore packet
          continue;
        }
        lastRecv = Util::epoch();//prevent disconnect of idle TCP connection when using UDP
        myConn.addDown(s.data_len);
        RTP::Packet pack(s.data, s.data_len);
        if (!it->second.rtpSeq){it->second.rtpSeq = pack.getSequence();}
        //packet is very early - assume dropped after 10 packets
        while ((int16_t)(((uint16_t)it->second.rtpSeq) - ((uint16_t)pack.getSequence())) < -10){
          WARN_MSG("Giving up on packet %u", it->second.rtpSeq);
          ++(it->second.rtpSeq);
          //send any buffered packets we may have
          while (it->second.packBuffer.count(it->second.rtpSeq)){
            handleIncomingRTP(it->first, pack);
            ++(it->second.rtpSeq);
          }
        }
        //send any buffered packets we may have
        while (it->second.packBuffer.count(it->second.rtpSeq)){
          handleIncomingRTP(it->first, pack);
          ++(it->second.rtpSeq);
        }
        //packet is slightly early - buffer it
        if (((int16_t)(((uint16_t)it->second.rtpSeq) - ((uint16_t)pack.getSequence())) < 0)){
          INFO_MSG("Buffering early packet #%u->%u", it->second.rtpSeq, pack.getSequence());
          it->second.packBuffer[pack.getSequence()] = pack;
        }
        //packet is late
        if ((int16_t)(((uint16_t)it->second.rtpSeq) - ((uint16_t)pack.getSequence())) > 0){
          //negative difference?
          WARN_MSG("Dropped a packet that arrived too late! (%d packets difference)", (int16_t)(((uint16_t)it->second.rtpSeq) - ((uint16_t)pack.getSequence())));
          return;
        }
        //packet is in order
        if (it->second.rtpSeq == pack.getSequence()){
          handleIncomingRTP(it->first, pack);
          ++(it->second.rtpSeq);
        }
      }
    }
  }

  ///Helper function to determine if a H264 NAL unit is init data or not
  static inline bool isH264Init(char * data){
    uint8_t nalType = (data[0] & 0x1F);
    // 7 = SPS
    // 8 = PPS
    return (nalType == 7 || nalType == 8);
  }

  ///Helper function to determine if a H264 NAL unit is a keyframe or not
  static inline bool isH264Keyframe(char * data, unsigned long len){
    uint8_t nalType = (data[0] & 0x1F);
    if (nalType == 0x05){return true;}
    if (nalType != 0x01){return false;}
    Utils::bitstream bs;
    for (size_t i = 1; i < 10 && i < len; ++i) {
      if (i + 2 < len && (memcmp(data + i, "\000\000\003", 3) == 0)) { //Emulation prevention bytes
        bs.append(data + i, 2);
        i += 2;
      } else {
        bs.append(data + i, 1);
      }
    }
    bs.getExpGolomb();//Discard first_mb_in_slice
    uint64_t sliceType = bs.getUExpGolomb();
    //Slice types:
    //  0: P - Predictive slice (at most 1 reference)
    //  1: B - Bi-predictive slice (at most 2 references)
    //  2: I - Intra slice (no external references)
    //  3: SP - Switching predictive slice (at most 1 reference)
    //  4: SI - Switching intra slice (no external references)
    //  5-9: 0-4, but all in picture of same type
    if (sliceType == 2 || sliceType == 4 || sliceType == 7 || sliceType == 9){
      return true;
    }
    return false;
  }

  ///Helper function to determine a H264 NAL unit frame number
  ///\returns -1 if there is no frame number
  ///UNFINISHED. Reads all values, but doesn't return any sensible values. Be warned!
  static inline int getH264FrameNum(char * data, unsigned long len, h264::SPSMeta & md){
    char nalType = (data[0] & 0x1F);
    if (nalType != 1 && nalType != 2 && nalType != 5){
      return -1;
    }
    Utils::bitstream bs;
    for (size_t i = 1; i < 20 && i < len; ++i) {
      if (i + 2 < len && (memcmp(data + i, "\000\000\003", 3) == 0)) { //Emulation prevention bytes
        bs.append(data + i, 2);
        i += 2;
      } else {
        bs.append(data + i, 1);
      }
    }
    bs.getUExpGolomb();//first_mb_in_slice
    bs.getUExpGolomb();//slice_type
    bs.getUExpGolomb();//pps_id
    if (md.sep_col_plane){
      bs.get(2);//colour_plane_id
    }
    uint16_t frame_num = bs.get(md.log2_max_frame_num);
    if (!md.mbs_only){
      if (bs.get(1)){bs.get(1);}//field_pic_flag && bottom_field_flag
    }
    if (nalType == 5){
      bs.getUExpGolomb();//idr_pic_id
    }
    ///\todo Implement pic_order_cnt_type value 1
    uint16_t order_cnt = 0;
    if (md.cnt_type == 0){
      order_cnt = bs.get(md.log2_max_order_cnt);
    }
    return -1;
  }

  /// Handles a single H264 packet, checking if others are appended at the end in Annex B format.
  /// If so, splits them up and calls h264Packet for each. If not, calls it only once for the whole payload.
  void OutRTSP::h264MultiParse(uint64_t ts, const uint64_t track, char * buffer, const uint32_t len){
    uint32_t lastStart = 0;
    for (uint32_t i = 0; i < len-4; ++i){
      //search for start code
      if (buffer[i] == 0 && buffer[i+1] == 0 && buffer[i+2] == 0 && buffer[i+3] == 1){
        //if found, handle a packet from the last start code up to this start code
        Bit::htobl(buffer+lastStart, (i-lastStart-1)-4);//size-prepend
        if (!isH264Init(buffer+lastStart+4)){
          h264Packet(ts, track, buffer+lastStart, (i-lastStart-1), isH264Keyframe(buffer+lastStart+4, i-lastStart-5));
        }
        lastStart = i;
      }
    }
    //Last packet (might be first, if no start codes found)
    Bit::htobl(buffer+lastStart, (len-lastStart)-4);//size-prepend
    if (!isH264Init(buffer+lastStart+4)){
      h264Packet(ts, track, buffer+lastStart, (len-lastStart), isH264Keyframe(buffer+lastStart+4, len-lastStart-4));
    }
  }

  void OutRTSP::h264Packet(uint64_t ts, const uint64_t track, const char * buffer, const uint32_t len, bool isKey){
    //Ignore zero-length packets (e.g. only contained init data and nothing else)
    if (!len){return;}
    double fps = h264meta[track].fps;
    uint32_t offset = 0;
    uint64_t newTs = ts;
    if (fps > 1){
      //Assume a steady frame rate, clip the timestamp based on frame number.
      uint64_t frameNo = (ts / (1000.0/fps))+0.5;
      while (frameNo < tracks[track].packCount){
        tracks[track].packCount--;
      }
      //More than 32 frames behind? We probably skipped something, somewhere...
      if ((frameNo-tracks[track].packCount) > 32){
        tracks[track].packCount = frameNo;
      }
      //After some experimentation, we found that the time offset is the difference between the frame number and the packet counter, times the frame rate in ms
      offset = (frameNo-tracks[track].packCount) * (1000.0/fps);
      //... and the timestamp is the packet counter times the frame rate in ms.
      newTs = tracks[track].packCount * (1000.0/fps);
      VERYHIGH_MSG("Packing time %llu = %sframe %llu (%.2f FPS). Expected %llu -> +%llu/%lu", ts, isKey?"key":"i", frameNo, fps, tracks[track].packCount, (frameNo-tracks[track].packCount), offset);
    }else{
      //For non-steady frame rate, assume no offsets are used and the timestamp is already correct
      VERYHIGH_MSG("Packing time %llu = %sframe %llu (variable rate)", ts, isKey?"key":"i", tracks[track].packCount);
    }
    //Fill the new DTSC packet, buffer it.
    DTSC::Packet nextPack;
    nextPack.genericFill(newTs, offset, track, buffer, len, 0, isKey);
    tracks[track].packCount++;
    nProxy.streamName = streamName;
    continueNegotiate(track);
    bufferLivePacket(nextPack);
  }

  /// Handles RTP packets generically, for both TCP and UDP-based connections.
  /// In case of UDP, expects packets to be pre-sorted.
  void OutRTSP::handleIncomingRTP(const uint64_t track, const RTP::Packet & pkt){
    if (!tracks[track].firstTime){
      tracks[track].firstTime = pkt.getTimeStamp() + 1;
    }
    if (myMeta.tracks[track].codec == "AAC"){
      //assume AAC packets are single AU units
      /// \todo Support other input than single AU units
      char * pl = pkt.getPayload();
      unsigned int headLen = (Bit::btohs(pl) >> 3) + 2;//in bits, so /8, plus two for the prepended size
      DTSC::Packet nextPack;
      uint16_t samples = aac::AudSpecConf::samples(myMeta.tracks[track].init);
      uint32_t sampleOffset = 0;
      uint32_t offset = 0;
      uint32_t auSize = 0;
      for (uint32_t i = 2; i < headLen; i += 2){
        auSize = Bit::btohs(pl+i) >> 3;//only the upper 13 bits
        nextPack.genericFill((pkt.getTimeStamp() + sampleOffset - tracks[track].firstTime + 1) / ((double)myMeta.tracks[track].rate / 1000.0), 0, track, pl+headLen+offset, std::min(auSize, pkt.getPayloadSize() - headLen - offset), 0, false);
        offset += auSize;
        sampleOffset += samples;
        nProxy.streamName = streamName;
        continueNegotiate(track);
        bufferLivePacket(nextPack);
      }
      return;
    }
    if (myMeta.tracks[track].codec == "H264"){
      //Handles common H264 packets types, but not all.
      //Generalizes and converts them all to a data format ready for DTSC, then calls h264Packet for that data.
      //Prints a WARN-level message if packet type is unsupported.
      /// \todo Support other H264 packets types?
      char * pl = pkt.getPayload();
      if ((pl[0] & 0x1F) == 0){
        WARN_MSG("H264 packet type null ignored");
        return;
      }
      if ((pl[0] & 0x1F) < 24){
        DONTEVEN_MSG("H264 single packet, type %u", (unsigned int)(pl[0] & 0x1F));
        if (isH264Init(pl)){
          return;
        }
        static char * packBuffer = 0;
        static unsigned long packBufferSize = 0;
        unsigned long len = pkt.getPayloadSize();
        if (packBufferSize < len+4){
          char * tmp = (char*)realloc(packBuffer, len+4);
          if (tmp){
            packBuffer = tmp;
            packBufferSize = len+4;
          }else{
            free(packBuffer);
            packBufferSize = 0;
            packBuffer = 0;
            FAIL_MSG("Failed to allocate memory for H264 packet");
            return;
          }
        }
        Bit::htobl(packBuffer, len);//size-prepend
        memcpy(packBuffer+4, pl, len);
        h264Packet((pkt.getTimeStamp() - tracks[track].firstTime + 1) / 90, track, packBuffer, len+4, isH264Keyframe(packBuffer+4, len));
        return;
      }
      if ((pl[0] & 0x1F) == 24){
        DONTEVEN_MSG("H264 STAP-A packet");
        unsigned int len = 0;
        unsigned int pos = 1;
        while (pos + 1 < pkt.getPayloadSize()){
          unsigned int pLen = Bit::btohs(pl+pos);
          INSANE_MSG("Packet of %ub and type %u", pLen, (unsigned int)(pl[pos+2] & 0x1F));
          pos += 2+pLen;
          len += 4+pLen;
        }
        static char * packBuffer = 0;
        static unsigned long packBufferSize = 0;
        if (packBufferSize < len){
          char * tmp = (char*)realloc(packBuffer, len);
          if (tmp){
            packBuffer = tmp;
            packBufferSize = len;
          }else{
            free(packBuffer);
            packBufferSize = 0;
            packBuffer = 0;
            FAIL_MSG("Failed to allocate memory for H264 STAP-A packet");
            return;
          }
        }
        pos = 1;
        len = 0;
        bool isKey = false;
        while (pos + 1 < pkt.getPayloadSize()){
          unsigned int pLen = Bit::btohs(pl+pos);
          isKey |= isH264Keyframe(pl+pos+2, pLen);
          if (!isH264Init(pl+pos+2)){
            Bit::htobl(packBuffer+len, pLen);//size-prepend
            memcpy(packBuffer+len+4, pl+pos+2, pLen);
            len += 4+pLen;
          }
          pos += 2+pLen;
        }
        h264Packet((pkt.getTimeStamp() - tracks[track].firstTime + 1) / 90, track, packBuffer, len, isKey);
        return;
      }
      if ((pl[0] & 0x1F) == 28){
        DONTEVEN_MSG("H264 FU-A packet");
        static char * fuaBuffer = 0;
        static unsigned long fuaBufferSize = 0;
        static unsigned long fuaCurrLen = 0;

        //No length yet? Check for start bit. Ignore rest.
        if (!fuaCurrLen && (pkt.getPayload()[1] & 0x80) == 0){
          HIGH_MSG("Not start of a new FU-A - throwing away");
          return;
        }
        if (fuaCurrLen && ((pkt.getPayload()[1] & 0x80) || (tracks[track].rtpSeq != pkt.getSequence()))){
          WARN_MSG("Ending unfinished FU-A");
          INSANE_MSG("H264 FU-A packet incompleted: %lu", fuaCurrLen);
          if (isH264Init(fuaBuffer+4)){
            //attempt to detect multiple H264 packets, even though specs disallow it
            h264MultiParse((pkt.getTimeStamp() - tracks[track].firstTime + 1) / 90, track, fuaBuffer, fuaCurrLen);
          }else{
            Bit::htobl(fuaBuffer, fuaCurrLen-4);//size-prepend
            fuaBuffer[4] |= 0x80;//set error bit
            h264Packet((pkt.getTimeStamp() - tracks[track].firstTime + 1) / 90, track, fuaBuffer, fuaCurrLen, isH264Keyframe(fuaBuffer+4, fuaCurrLen-4));
          }
          fuaCurrLen = 0;
          return;
        }

        unsigned long len = pkt.getPayloadSize() - 2;//ignore the two FU-A bytes in front
        if (!fuaCurrLen){len += 5;}//five extra bytes for the first packet


        if (fuaBufferSize < fuaCurrLen + len){
          char * tmp = (char*)realloc(fuaBuffer, fuaCurrLen + len);
          if (tmp){
            fuaBuffer = tmp;
            fuaBufferSize = fuaCurrLen + len;
          }else{
            free(fuaBuffer);
            fuaBufferSize = 0;
            fuaBuffer = 0;
            FAIL_MSG("Failed to allocate memory for H264 FU-A packet");
            return;
          }
        }
  
        if (fuaCurrLen == 0){
          memcpy(fuaBuffer+4, pkt.getPayload()+1, pkt.getPayloadSize()-1);
          //reconstruct first byte
          fuaBuffer[4] = (fuaBuffer[4] & 0x1F) | (pkt.getPayload()[0] & 0xE0);
        }else{
          memcpy(fuaBuffer+fuaCurrLen, pkt.getPayload()+2, pkt.getPayloadSize()-2);
        }
        fuaCurrLen += len;

        if (pkt.getPayload()[1] & 0x40){//last packet
          INSANE_MSG("H264 FU-A packet type %u completed: %lu", (unsigned int)(fuaBuffer[4] & 0x1F), fuaCurrLen);
          if (isH264Init(fuaBuffer+4)){
            //attempt to detect multiple H264 packets, even though specs disallow it
            h264MultiParse((pkt.getTimeStamp() - tracks[track].firstTime + 1) / 90, track, fuaBuffer, fuaCurrLen);
          }else{
            Bit::htobl(fuaBuffer, fuaCurrLen-4);//size-prepend
            h264Packet((pkt.getTimeStamp() - tracks[track].firstTime + 1) / 90, track, fuaBuffer, fuaCurrLen, isH264Keyframe(fuaBuffer+4, fuaCurrLen-4));
          }
          fuaCurrLen = 0;
        }
        return;
      }
      WARN_MSG("H264 packet type %u unsupported", (unsigned int)(pl[0] & 0x1F));
      return;
    }
  }

  void OutRTSP::parseSDP(const std::string & sdp){
    std::stringstream ss(sdp);
    std::string to;
    uint64_t trackNo = 0;
    bool nope = true; //true if we have no valid track to fill
    while(std::getline(ss,to,'\n')){
      if (!to.empty() && *to.rbegin() == '\r'){to.erase(to.size()-1, 1);}

      // All tracks start with a media line
      if (to.substr(0,2) == "m="){
        nope = true;
        ++trackNo;
        std::stringstream words(to.substr(2));
        std::string item;
        if (getline(words, item, ' ') && (item == "audio" || item == "video")){
            myMeta.tracks[trackNo].type = item;
            myMeta.tracks[trackNo].trackID = trackNo;
        }else{
          WARN_MSG("Media type not supported: %s", item.c_str());
          continue;
        }
        getline(words, item, ' ');
        if (!getline(words, item, ' ') || item != "RTP/AVP"){
          WARN_MSG("Media transport not supported: %s", item.c_str());
          continue;
        }
        nope = false;
        continue;
      }
      if (nope){continue;}//ignore lines if we have no valid track
      // RTP mapping
      if (to.substr(0, 8) == "a=rtpmap"){
        std::string mediaType = to.substr(to.find(' ', 8)+1);
        std::string trCodec = mediaType.substr(0, mediaType.find('/'));
        for(unsigned int i=0;i<trCodec.size();++i){
          if(trCodec[i]<=122 && trCodec[i]>=97){trCodec[i]-=32;}
        }
        if (trCodec == "H264"){
          myMeta.tracks[trackNo].codec = "H264";
        }
        if (trCodec == "MPEG4-GENERIC"){
          myMeta.tracks[trackNo].codec = "AAC";
          std::string extraInfo = mediaType.substr(mediaType.find('/')+1);
          if (extraInfo.find('/') != std::string::npos){
            size_t lastSlash = extraInfo.find('/');
            myMeta.tracks[trackNo].rate = atoll(extraInfo.substr(0, lastSlash).c_str());
            myMeta.tracks[trackNo].channels = atoll(extraInfo.substr(lastSlash+1).c_str());
          }else{
            myMeta.tracks[trackNo].rate = atoll(extraInfo.c_str());
            myMeta.tracks[trackNo].channels = 1;
          }
        }
        INFO_MSG("Incoming track %s", myMeta.tracks[trackNo].getIdentifier().c_str());
        continue;
      }
      if (to.substr(0, 10) == "a=control:"){
        tracks[trackNo].control = to.substr(10);
        continue;
      }
      if (to.substr(0, 7) == "a=fmtp:"){
        tracks[trackNo].fmtp = to.substr(7);
        if (myMeta.tracks[trackNo].codec == "AAC"){
          if (tracks[trackNo].getParamString("mode") != "AAC-hbr"){
            //a=fmtp:97 profile-level-id=1;mode=AAC-hbr;sizelength=13;indexlength=3;indexdeltalength=3; config=120856E500
            FAIL_MSG("AAC transport mode not supported: %s", tracks[trackNo].getParamString("mode").c_str());
            nope = true;
            myMeta.tracks.erase(trackNo);
            tracks.erase(trackNo);
            continue;
          }
          myMeta.tracks[trackNo].init = Encodings::Hex::decode(tracks[trackNo].getParamString("config"));
          //myMeta.tracks[trackNo].rate = aac::AudSpecConf::rate(myMeta.tracks[trackNo].init);

        }
        if (myMeta.tracks[trackNo].codec == "H264"){
          //a=fmtp:96 packetization-mode=1; sprop-parameter-sets=Z0LAHtkA2D3m//AUABqxAAADAAEAAAMAMg8WLkg=,aMuDyyA=; profile-level-id=42C01E
          std::string sprop = tracks[trackNo].getParamString("sprop-parameter-sets");
          size_t comma = sprop.find(',');
          std::string spsInfo = Encodings::Base64::decode(sprop.substr(0,comma));
          std::string ppsInfo = Encodings::Base64::decode(sprop.substr(comma+1));
          h264::sequenceParameterSet sps(spsInfo.data(), spsInfo.size());
          h264meta[trackNo] = sps.getCharacteristics();
          myMeta.tracks[trackNo].width = h264meta[trackNo].width;
          myMeta.tracks[trackNo].height = h264meta[trackNo].height;
          myMeta.tracks[trackNo].fpks = h264meta[trackNo].fps * 1000;
          MP4::AVCC avccBox;
          avccBox.setVersion(1);
          avccBox.setProfile(spsInfo[1]);
          avccBox.setCompatibleProfiles(spsInfo[2]);
          avccBox.setLevel(spsInfo[3]);
          avccBox.setSPSNumber(1);
          avccBox.setSPS(spsInfo);
          avccBox.setPPSNumber(1);
          avccBox.setPPS(ppsInfo);
          myMeta.tracks[trackNo].init = std::string(avccBox.payload(), avccBox.payloadSize());
        }
        continue;
      }
      // We ignore bandwidth lines
      if (to.substr(0,2) == "b="){
        continue;
      }
      //we ignore everything before the first media line.
      if (!trackNo){
        continue;
      }
      //at this point, the data is definitely for a track
      INFO_MSG("Unhandled SDP line for track %llu: %s", trackNo, to.c_str());
    }

  }

}

