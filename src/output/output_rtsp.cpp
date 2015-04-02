#include <mist/defines.h>
#include <mist/auth.h>
#include <mist/base64.h>
#include "output_rtsp.h"

namespace Mist {
  OutRTSP::OutRTSP(Socket::Connection & myConn) : Output(myConn){
    connectedAt = Util::epoch() + 2208988800ll;
    seekpoint = 0;
    pausepoint = 0;
    setBlocking(false);
    maxSkipAhead = 0;
    minSkipAhead = 0;
  }
  
  /// Function used to send RTP packets over UDP
  ///\param socket A UDP Connection pointer, sent as a void*, to keep portability.
  ///\param data The RTP Packet that needs to be sent
  ///\param len The size of data
  ///\param channel Not used here, but is kept for compatibility with sendTCP
  void sendUDP(void * socket, char * data, unsigned int len, unsigned int channel) {
    ((Socket::UDPConnection *) socket)->SendNow(data, len);
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

    cfg->addConnectorOptions(554, capa);
    config = cfg;
  }

  void OutRTSP::sendNext(){
    char * dataPointer = 0;
    unsigned int dataLen = 0;
    thisPacket.getString("data", dataPointer, dataLen);
    unsigned int tid = thisPacket.getTrackId();
    unsigned int timestamp = thisPacket.getTime();
    
    //update where we are now.
    seekpoint = timestamp;
    //if we're past the pausing point, seek to it, and pause immediately
    if (pausepoint && seekpoint > pausepoint){
      seekpoint = pausepoint;
      pausepoint = 0;
      stop();
      return;
    }
    
    void * socket = 0;
    void (*callBack)(void *, char *, unsigned int, unsigned int) = 0;
    
    if (tracks[tid].UDP){
      socket = &tracks[tid].data;
      callBack = sendUDP;
      if (Util::epoch()/5 != tracks[tid].rtcpSent){
        tracks[tid].rtcpSent = Util::epoch()/5;
        tracks[tid].rtpPacket.sendRTCP(connectedAt, &tracks[tid].rtcp, tid, myMeta, sendUDP);
      }
    }else{
      socket = &myConn;
      callBack = sendTCP;
    }
    
    if(myMeta.tracks[tid].codec == "MP3"){
      tracks[tid].rtpPacket.setTimestamp(timestamp * 90);
      tracks[tid].rtpPacket.sendData(socket, callBack, dataPointer, dataLen, tracks[tid].channel, "MP3");
      return;
    }

    if( myMeta.tracks[tid].codec == "AC3" || myMeta.tracks[tid].codec == "AAC"){
      tracks[tid].rtpPacket.setTimestamp(timestamp * ((double) myMeta.tracks[tid].rate / 1000.0));
      tracks[tid].rtpPacket.sendData(socket, callBack, dataPointer, dataLen, tracks[tid].channel,myMeta.tracks[tid].codec);
      return;
    }

    if(myMeta.tracks[tid].codec == "H264"){
      long long offset = thisPacket.getInt("offset");
      tracks[tid].rtpPacket.setTimestamp(90 * (timestamp + offset));
      if (tracks[tid].initSent && thisPacket.getFlag("keyframe")) {
        MP4::AVCC avccbox;
        avccbox.setPayload(myMeta.tracks[tid].init);
        tracks[tid].rtpPacket.sendH264(socket, callBack, avccbox.getSPS(), avccbox.getSPSLen(), tracks[tid].channel);
        tracks[tid].rtpPacket.sendH264(socket, callBack, avccbox.getPPS(), avccbox.getPPSLen(), tracks[tid].channel);
        tracks[tid].initSent = true;
      }
      unsigned long sent = 0;
      while (sent < dataLen) {
        unsigned long nalSize = ntohl(*((unsigned long *)(dataPointer + sent)));
        tracks[tid].rtpPacket.sendH264(socket, callBack, dataPointer + sent + 4, nalSize, tracks[tid].channel);
        sent += nalSize + 4;
      }
      return;
    }
    
  }

  void OutRTSP::onRequest(){
    while (HTTP_R.Read(myConn)){
      HTTP_S.Clean();
      HTTP_S.protocol = "RTSP/1.0";
      
      //set the streamname and session
      size_t found = HTTP_R.url.find('/', 7);
      streamName = HTTP_R.url.substr(found + 1, HTTP_R.url.substr(found + 1).find('/'));
      if (streamName != ""){
        HTTP_S.SetHeader("Session", Secure::md5(HTTP_S.GetHeader("User-Agent") + myConn.getHost()) + "_" + streamName);
      }
      
      //set the date
      time_t timer;
      time(&timer);
      struct tm * timeNow = gmtime(&timer);
      char dString[42];
      strftime(dString, 42, "%a, %d %h %Y, %X GMT", timeNow);
      HTTP_S.SetHeader("Date", dString);
      
      //set the sequence number to match the received sequence number
      HTTP_S.SetHeader("CSeq", HTTP_R.GetHeader("CSeq"));
      
      //handle the request
      DEBUG_MSG(DLVL_VERYHIGH, "Received %s:\n%s", HTTP_R.method.c_str(), HTTP_R.BuildRequest().c_str());
      bool handled = false;
      if (HTTP_R.method == "OPTIONS"){
        HTTP_S.SetHeader("Public", "SETUP, TEARDOWN, PLAY, PAUSE, DESCRIBE, GET_PARAMETER");
        HTTP_S.SendResponse("200", "OK", myConn);
        handled = true;
      }
      if (HTTP_R.method == "GET_PARAMETER"){
        HTTP_S.SendResponse("200", "OK", myConn);
        handled = true;
      }
      if (HTTP_R.method == "DESCRIBE"){
        handleDescribe();
        handled = true;
      }
      if (HTTP_R.method == "SETUP"){
        handleSetup();
        handled = true;
      }
      if (HTTP_R.method == "PLAY"){
        handlePlay();
        handled = true;
      }
      if (HTTP_R.method == "PAUSE"){
        handlePause();
        handled = true;
      }
      if (HTTP_R.method == "TEARDOWN"){
        myConn.close();
        stop();
        handled = true;
      }
      if (!handled){
        DEBUG_MSG(DLVL_WARN, "Unhandled command %s:\n%s", HTTP_R.method.c_str(), HTTP_R.BuildRequest().c_str());
      }
      HTTP_R.Clean();
    }
  }

  void OutRTSP::handleDescribe(){
    //initialize the header, clear out any automatically selected tracks
    initialize();
    selectedTracks.clear();
    
    //calculate begin/end of stream
    unsigned int firstms = myMeta.tracks.begin()->second.firstms;
    unsigned int lastms = myMeta.tracks.begin()->second.lastms;
    for (std::map<unsigned int, DTSC::Track>::iterator objIt = myMeta.tracks.begin(); objIt != myMeta.tracks.end(); objIt ++) {
      if (objIt->second.firstms < firstms){
        firstms = objIt->second.firstms;
      }
      if (objIt->second.lastms > lastms){
        lastms = objIt->second.lastms;
      }
    }
    
    HTTP_S.SetHeader("Content-Base", HTTP_R.url);
    HTTP_S.SetHeader("Content-Type", "application/sdp");
    std::stringstream transportString;
    transportString << "v=0\r\n"//version
    "o=- "//owner
    << Util::getMS()//id
    << " 1 IN IP4 127.0.0.1"//or IPv6
    "\r\ns=" << streamName << "\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "i=Mistserver stream " << streamName << "\r\n"
    "u=" << HTTP_R.url.substr(0, HTTP_R.url.rfind('/')) << "/" << streamName << "\r\n"
    "t=0 0\r\n"//timing
    "a=tool:MistServer\r\n"//
    "a=type:broadcast\r\n"//
    "a=control:*\r\n"//
    "a=range:npt=" << ((double)firstms) / 1000.0 << "-" << ((double)lastms) / 1000.0 << "\r\n";
    
    //loop over all tracks, add them to the SDP.
    /// \todo Make sure this works correctly for multibitrate streams.
    for (std::map<unsigned int, DTSC::Track>::iterator objIt = myMeta.tracks.begin(); objIt != myMeta.tracks.end(); objIt ++) {
      INFO_MSG("Codec: %s", objIt->second.codec.c_str());
      if (objIt->second.codec == "H264") {
        MP4::AVCC avccbox;
        avccbox.setPayload(objIt->second.init);
        transportString << "m=" << objIt->second.type << " 0 RTP/AVP 97\r\n"
        "a=rtpmap:97 H264/90000\r\n"
        "a=cliprect:0,0," << objIt->second.height << "," << objIt->second.width << "\r\n"
        "a=framesize:97 " << objIt->second.width << '-' << objIt->second.height << "\r\n"
        "a=fmtp:97 packetization-mode=1;profile-level-id="
        << std::hex << std::setw(2) << std::setfill('0') << (int)objIt->second.init.data()[1] << std::dec << "E0"
        << std::hex << std::setw(2) << std::setfill('0') << (int)objIt->second.init.data()[3] << std::dec << ";"
        "sprop-parameter-sets="
        << Base64::encode(std::string(avccbox.getSPS(), avccbox.getSPSLen()))
        << ","
        << Base64::encode(std::string(avccbox.getPPS(), avccbox.getPPSLen()))
        << "\r\n"
        "a=framerate:" << ((double)objIt->second.fpks)/1000.0 << "\r\n"
        "a=control:track" << objIt->second.trackID << "\r\n";
      } else if (objIt->second.codec == "AAC") {
        transportString << "m=" << objIt->second.type << " 0 RTP/AVP 96" << "\r\n"
        "a=rtpmap:96 mpeg4-generic/" << objIt->second.rate << "/" << objIt->second.channels << "\r\n"
        "a=fmtp:96 streamtype=5; profile-level-id=15; config=";
        for (unsigned int i = 0; i < objIt->second.init.size(); i++) {
          transportString << std::hex << std::setw(2) << std::setfill('0') << (int)objIt->second.init[i] << std::dec;
        }
        //these values are described in RFC 3640
        transportString << "; mode=AAC-hbr; SizeLength=13; IndexLength=3; IndexDeltaLength=3;\r\n"
        "a=control:track" << objIt->second.trackID << "\r\n";
      }else if (objIt->second.codec == "MP3") {
        transportString << "m=" << objIt->second.type << " 0 RTP/AVP 14" << "\r\n"
        "a=rtpmap:14 MPA/90000/" << objIt->second.channels << "\r\n"
        "a=control:track" << objIt->second.trackID << "\r\n";
      }else if ( objIt->second.codec == "AC3") {
        transportString << "m=" << objIt->second.type << " 0 RTP/AVP 100" << "\r\n"
        "a=rtpmap:100 AC3/" << objIt->second.rate << "/" << objIt->second.channels << "\r\n"
        "a=control:track" << objIt->second.trackID << "\r\n";
      }
    }//for tracks iterator
    transportString << "\r\n";
    HTTP_S.SetBody(transportString.str());
    HTTP_S.SendResponse("200", "OK", myConn);
  }
  
  void OutRTSP::handleSetup(){
    std::stringstream transportString;
    unsigned int trId = atol(HTTP_R.url.substr(HTTP_R.url.rfind("/track") + 6).c_str());
    selectedTracks.insert(trId);
    unsigned int SSrc = rand();
    if (myMeta.tracks[trId].codec == "H264") {
      tracks[trId].rtpPacket = RTP::Packet(97, 1, 0, SSrc);
    }else if(myMeta.tracks[trId].codec == "AAC"){
      tracks[trId].rtpPacket = RTP::Packet(96, 1, 0, SSrc);
    }else if(myMeta.tracks[trId].codec == "AC3"){
      tracks[trId].rtpPacket = RTP::Packet(100, 1, 0, SSrc);
    }else if(myMeta.tracks[trId].codec == "MP3"){
      tracks[trId].rtpPacket = RTP::Packet(14, 1, 0, SSrc);
    }else{
      DEBUG_MSG(DLVL_FAIL,"Unsupported codec for RTSP: %s",myMeta.tracks[trId].codec.c_str());
    }
    
    //read client ports
    std::string transport = HTTP_R.GetHeader("Transport");
    unsigned long cPort;
    if (transport.find("TCP") != std::string::npos) {
      /// \todo This needs error checking.
      tracks[trId].UDP = false;
      std::string chanE =  transport.substr(transport.find("interleaved=") + 12, (transport.size() - transport.rfind('-') - 1)); //extract channel ID
      tracks[trId].channel = atol(chanE.c_str());
      tracks[trId].rtcpSent = 0;
      transportString << transport;
    } else {
      tracks[trId].UDP = true;
      size_t port_loc = transport.rfind("client_port=") + 12;
      cPort = atol(transport.substr(port_loc, transport.rfind('-') - port_loc).c_str());
      //find available ports locally;
      int sendbuff = 4*1024*1024;
      tracks[trId].data.SetDestination(myConn.getHost(), cPort);
      tracks[trId].data.bind(2000 + trId * 2);
      setsockopt(tracks[trId].data.getSock(), SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));
      tracks[trId].rtcp.SetDestination(myConn.getHost(), cPort + 1);
      tracks[trId].rtcp.bind(2000 + trId * 2 + 1);
      setsockopt(tracks[trId].rtcp.getSock(), SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));
      std::string source = HTTP_R.url.substr(7);
      unsigned int loc = std::min(source.find(':'),source.find('/'));
      source = source.substr(0,loc);
      transportString << "RTP/AVP/UDP;unicast;client_port=" << cPort << '-' << cPort + 1 << ";source="<< source <<";server_port=" << (2000 + trId * 2) << "-" << (2000 + trId * 2 + 1) << ";ssrc=" << std::hex << SSrc << std::dec;
    }
    /// \todo We should probably not allocate UDP sockets when using TCP.
    HTTP_S.SetHeader("Expires", HTTP_S.GetHeader("Date"));
    HTTP_S.SetHeader("Transport", transportString.str());
    HTTP_S.SetHeader("Cache-Control", "no-cache");
    HTTP_S.SendResponse("200", "OK", myConn);
  }

  void OutRTSP::handlePause(){
    HTTP_S.SendResponse("200", "OK", myConn);
    std::string range = HTTP_R.GetHeader("Range");
    if (range.empty()){
      stop();
      return;
    }
    range = range.substr(range.find("npt=")+4);
    if (range.empty()) {
      stop();
      return;
    }
    pausepoint = 1000 * (int) atof(range.c_str());
    if (pausepoint > seekpoint){
      seekpoint = pausepoint;
      pausepoint = 0;
      stop();
    }
  }
  
  void OutRTSP::handlePlay(){
    /// \todo Add support for queuing multiple play ranges
    //calculate first and last possible timestamps
    unsigned int firstms = myMeta.tracks.begin()->second.firstms;
    unsigned int lastms = myMeta.tracks.begin()->second.lastms;
    for (std::map<unsigned int, DTSC::Track>::iterator objIt = myMeta.tracks.begin(); objIt != myMeta.tracks.end(); objIt ++) {
      if (objIt->second.firstms < firstms){
        firstms = objIt->second.firstms;
      }
      if (objIt->second.lastms > lastms){
        lastms = objIt->second.lastms;
      }
    }
    
    std::stringstream transportString;
    std::string range = HTTP_R.GetHeader("Range");
    if (range != ""){
      DEBUG_MSG(DLVL_DEVEL, "Play: %s", range.c_str());
      range = range.substr(range.find("npt=")+4);
      if (range.empty()) {
        seekpoint = 0;
      } else {
        range = range.substr(0, range.find('-'));
        seekpoint = 1000 * (int) atof(range.c_str());
      }
      //snap seekpoint to closest keyframe
      for (std::map<int, trackmeta>::iterator it = tracks.begin(); it != tracks.end(); it++) {
        it->second.rtcpSent =0;
        if (myMeta.tracks[it->first].type == "video") {
          unsigned int newPoint = seekpoint;
          for (unsigned int iy = 0; iy < myMeta.tracks[it->first].keys.size(); iy++) {
            if (myMeta.tracks[it->first].keys[iy].getTime() > seekpoint && iy > 0) {
              iy--;
              break;
            }
            newPoint = myMeta.tracks[it->first].keys[iy].getTime();
          }
          seekpoint = newPoint;
          break;
        }
      }
    }
    seek(seekpoint);
    
    unsigned int counter = 0;
    std::map<int, long long int> timeMap; //Keeps track of temporary timestamp data for the upcoming seek.
    for (std::map<int, trackmeta>::iterator it = tracks.begin(); it != tracks.end(); it++) {
      timeMap[it->first] = myMeta.tracks[it->first].firstms;
      for (unsigned int iy = 0; iy < myMeta.tracks[it->first].parts.size(); iy++) {
        if (timeMap[it->first] > seekpoint) {
          iy--;
          break;
        }
        timeMap[it->first] += myMeta.tracks[it->first].parts[iy].getDuration();//door parts van keyframes
      }
      if (myMeta.tracks[it->first].codec == "H264") {
        timeMap[it->first] = 90 * timeMap[it->first];
      } else if (myMeta.tracks[it->first].codec == "AAC" || myMeta.tracks[it->first].codec == "MP3" || myMeta.tracks[it->first].codec == "AC3") {
        timeMap[it->first] = timeMap[it->first] * ((double)myMeta.tracks[it->first].rate / 1000.0);
      }
      transportString << "url=" << HTTP_R.url.substr(0, HTTP_R.url.rfind('/')) << "/" << streamName << "/track" << it->first << ";"; //get the current url, not localhost
      transportString << "sequence=" << tracks[it->first].rtpPacket.getSequence() << ";rtptime=" << timeMap[it->first];
      if (counter < tracks.size()) {
        transportString << ",";
      }
      counter++;
    }
    std::stringstream rangeStr;
    rangeStr << "npt=" << seekpoint/1000 << "." << std::setw(3) << std::setfill('0') << seekpoint %1000 << "-" << std::setw(1) << lastms/1000 << "." << std::setw(3) << std::setfill('0') << lastms%1000;
    HTTP_S.SetHeader("Range", rangeStr.str());
    HTTP_S.SetHeader("RTP-Info", transportString.str());
    HTTP_S.SendResponse("200", "OK", myConn);
    parseData = true; 
  }

}
