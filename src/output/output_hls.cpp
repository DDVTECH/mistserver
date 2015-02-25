#include "output_hls.h"
#include <mist/stream.h>
#include <unistd.h>

namespace Mist {
  ///\brief Builds an index file for HTTP Live streaming.
  ///\return The index file for HTTP Live Streaming.
  std::string OutHLS::liveIndex(){
    std::stringstream result;
    result << "#EXTM3U\r\n";
    int audioId = -1;
    std::string audioName;
    for (std::map<unsigned int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      if (it->second.codec == "AAC"){
        audioId = it->first;
        audioName = it->second.getIdentifier();
        break;
      }
    }
    for (std::map<unsigned int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      if (it->second.codec == "H264"){
        int bWidth = it->second.bps * 2;
        if (audioId != -1){
          bWidth += myMeta.tracks[audioId].bps * 2;
        }
        result << "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" << bWidth * 10 << "\r\n";
        result << it->first;
        if (audioId != -1){
          result << "_" << audioId;
        }
        result << "/index.m3u8\r\n";
      }
    }
    DEBUG_MSG(DLVL_HIGH, "Sending this index: %s", result.str().c_str());
    return result.str();
  }

  std::string OutHLS::liveIndex(int tid){
    updateMeta();
    std::stringstream result;
    //parse single track
    int longestFragment = 0;
    if (!myMeta.tracks[tid].fragments.size()){
      DEBUG_MSG(DLVL_FAIL, "liveIndex called with track %d, which has no fragments!", tid);
      return "";
    }
    for (std::deque<DTSC::Fragment>::iterator it = myMeta.tracks[tid].fragments.begin(); (it + 1) != myMeta.tracks[tid].fragments.end(); it++){
      if (it->getDuration() > longestFragment){
        longestFragment = it->getDuration();
      }
    }
    result << "#EXTM3U\r\n#EXT-X-TARGETDURATION:" << (longestFragment / 1000) + 1 << "\r\n";
        
    std::deque<std::string> lines;
    for (std::deque<DTSC::Fragment>::iterator it = myMeta.tracks[tid].fragments.begin(); it != myMeta.tracks[tid].fragments.end(); it++){
      long long int starttime = myMeta.tracks[tid].getKey(it->getNumber()).getTime();
      std::stringstream line;
      long long duration = it->getDuration();
      if (duration < 0){
        duration = myMeta.tracks[tid].lastms - starttime;
      }
      line << "#EXTINF:" << ((duration + 500) / 1000) << ", no desc\r\n" << starttime << "_" << duration + starttime << ".ts\r\n";
      lines.push_back(line.str());
    }
    
    //skip the first fragment if live and there are more than 2 fragments.
    unsigned int skippedLines = 0;
    if (myMeta.live){
      if (lines.size() > 2){
        lines.pop_front();
        skippedLines++;
      }
      //only print the last segment when VoD
      lines.pop_back();
    }
    
    result << "#EXT-X-MEDIA-SEQUENCE:" << myMeta.tracks[tid].missedFrags + skippedLines << "\r\n";
    
    while (lines.size()){
      result << lines.front();
      lines.pop_front();
    }
    if ( !myMeta.live){
      result << "#EXT-X-ENDLIST\r\n";
    }
    DEBUG_MSG(DLVL_HIGH, "Sending this index: %s", result.str().c_str());
    return result.str();
  } //liveIndex
  
  
  OutHLS::OutHLS(Socket::Connection & conn) : HTTPOutput(conn) {
    haveAvcc = false;
    realTime = 0;
    setBlocking(true);
  }
  
  OutHLS::~OutHLS() {}
  
  void OutHLS::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "HLS";
    capa["desc"] = "Enables HTTP protocol Apple-specific streaming (also known as HLS).";
    capa["url_rel"] = "/hls/$/index.m3u8";
    capa["url_prefix"] = "/hls/$/";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/application/vnd.apple.mpegurl";
    capa["methods"][0u]["priority"] = 9ll;
  }

  void OutHLS::fillPacket(bool & first, const char * data, size_t dataLen, char & ContCounter){
    static std::map<int, int> contCounter;
    if (!PackData.BytesFree()){
      if (PacketNumber % 42 == 0){
        TS::Packet tmpPack;
        tmpPack.FromPointer(TS::PAT);
        tmpPack.continuityCounter(++contCounter[tmpPack.PID()]);
        H.Chunkify(tmpPack.ToString(), 188, myConn);
        tmpPack.FromPointer(TS::createPMT(selectedTracks, myMeta).c_str());
        tmpPack.continuityCounter(++contCounter[tmpPack.PID()]);
        H.Chunkify(tmpPack.ToString(), 188, myConn);
        PacketNumber += 2;
      }
      H.Chunkify(PackData.ToString(), 188, myConn);
      PacketNumber ++;
      PackData.Clear();
    }
    
    if (!dataLen){return;}
    
    if (PackData.BytesFree() == 184){
      PackData.PID(0x100 - 1 + currentPacket.getTrackId());
      PackData.continuityCounter(ContCounter++);
      if (first){
        PackData.unitStart(1);
        if (myMeta.tracks[currentPacket.getTrackId()].type == "video"){
          if (currentPacket.getInt("keyframe")){
            PackData.randomAccess(1);
          }
          PackData.PCR(currentPacket.getTime() * 27000);
        }
        first = false;
      }

    }
    
    int tmp = PackData.FillFree(data, dataLen);
    if (tmp != dataLen){
      fillPacket(first, data+tmp, dataLen-tmp, ContCounter);
    }
  }
  
  void OutHLS::sendNext(){
    bool first = true;
    char * dataPointer = 0;
    unsigned int dataLen = 0;
    currentPacket.getString("data", dataPointer, dataLen); //data
    
    if (currentPacket.getTime() >= until){
      stop();
      wantRequest = true;
      parseData = false;
      H.Chunkify("", 0, myConn);
      H.Clean();
      return;
    }

    std::string bs;
    //prepare bufferstring
    if (myMeta.tracks[currentPacket.getTrackId()].type == "video"){
      bs = TS::Packet::getPESVideoLeadIn(0ul, currentPacket.getTime() * 90, currentPacket.getInt("offset") * 90);
      fillPacket(first, bs.data(), bs.size(), VideoCounter);
      if (myMeta.tracks[currentPacket.getTrackId()].codec == "H264"){
        //End of previous nal unit, somehow needed for h264
        fillPacket(first, "\000\000\000\001\011\360", 6, VideoCounter);
      }
      
      if (currentPacket.getInt("keyframe")){
        if (!haveAvcc){
          avccbox.setPayload(myMeta.tracks[currentPacket.getTrackId()].init);
          haveAvcc = true;
          bs = avccbox.asAnnexB();
          fillPacket(first, bs.data(), bs.size(), VideoCounter);
        }
      }
      
      unsigned int i = 0;
      while (i + 4 < (unsigned int)dataLen){
        unsigned int ThisNaluSize = (dataPointer[i] << 24) + (dataPointer[i+1] << 16) + (dataPointer[i+2] << 8) + dataPointer[i+3];
        if (ThisNaluSize + i + 4 > (unsigned int)dataLen){
          DEBUG_MSG(DLVL_WARN, "Too big NALU detected (%u > %d) - skipping!", ThisNaluSize + i + 4, dataLen);
          break;
        }
        fillPacket(first, "\000\000\000\001",4, VideoCounter);
        fillPacket(first, dataPointer+i+4,ThisNaluSize, VideoCounter);      
        i += ThisNaluSize+4;
      }
      if (PackData.BytesFree() < 184){
        PackData.AddStuffing();
        fillPacket(first, 0, 0, VideoCounter);
      }
    }else if (myMeta.tracks[currentPacket.getTrackId()].type == "audio"){
      long long unsigned int tempTime;
      if (AppleCompat){
        tempTime = lastVid;
      }else{
        tempTime = currentPacket.getTime() * 90;
      }
      long unsigned int tempLen = dataLen;
      if ( myMeta.tracks[currentPacket.getTrackId()].codec == "AAC"){
        tempLen += 7;
      }
      bs = TS::Packet::getPESAudioLeadIn(tempLen, tempTime);
      fillPacket(first, bs.data(), bs.size(), AudioCounter);
      if (myMeta.tracks[currentPacket.getTrackId()].codec == "AAC"){
        bs = TS::GetAudioHeader(dataLen, myMeta.tracks[currentPacket.getTrackId()].init);      
        fillPacket(first, bs.data(), bs.size(), AudioCounter);
      }
      fillPacket(first, dataPointer,dataLen, AudioCounter);
      if (PackData.BytesFree() < 184){
        PackData.AddStuffing();
        fillPacket(first, 0, 0, AudioCounter);
      }
    }
  }

  int OutHLS::canSeekms(unsigned int ms){
    //no tracks? Frame too new by definition.
    if ( !myMeta.tracks.size()){
      return 1;
    }
    //loop trough all the tracks
    for (std::map<unsigned int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      //return "too late" if one track is past this point
      if (ms < it->second.firstms){
        return -1;
      }
      //return "too early" if one track is not yet at this point
      if (ms > it->second.lastms){
        return 1;
      }
    }
    return 0;
  }

  void OutHLS::onHTTP(){
    AppleCompat = (H.GetHeader("User-Agent").find("Apple") != std::string::npos);
    VLCworkaround = false;
    if (H.GetHeader("User-Agent").substr(0, 3) == "VLC"){
      std::string vlcver = H.GetHeader("User-Agent").substr(4);
      if (vlcver[0] == '0' || vlcver[0] == '1' || (vlcver[0] == '2' && vlcver[2] < '2')){
        DEBUG_MSG(DLVL_INFO, "Enabling VLC version < 2.2.0 bug workaround.");
        VLCworkaround = true;
      }
    }
    initialize();
    if (H.url.find(".m3u") == std::string::npos){
      std::string tmpStr = H.getUrl().substr(5+streamName.size());
      long long unsigned int from;
      if (sscanf(tmpStr.c_str(), "/%u_%u/%llu_%llu.ts", &vidTrack, &audTrack, &from, &until) != 4){
        if (sscanf(tmpStr.c_str(), "/%u/%llu_%llu.ts", &vidTrack, &from, &until) != 3){
          DEBUG_MSG(DLVL_MEDIUM, "Could not parse URL: %s", H.getUrl().c_str());
          H.Clean();
          H.SetBody("The HLS URL wasn't understood - what did you want, exactly?\n");
          myConn.SendNow(H.BuildResponse("404", "URL mismatch"));
          H.Clean(); //clean for any possible next requests
          return;
        }else{
          selectedTracks.clear();
          selectedTracks.insert(vidTrack);
        }
      }else{
        selectedTracks.clear();
        selectedTracks.insert(vidTrack);
        selectedTracks.insert(audTrack);
      }
      
      if (myMeta.live){
        unsigned int timeout = 0;
        int seekable;
        do {
          seekable = canSeekms(from);
          /// \todo Detection of out-of-range parts.
          if (seekable > 0){
            //time out after 21 seconds
            if (++timeout > 42){
              myConn.close();
              break;
            }
            Util::sleep(500);
            updateMeta();
          }
        }while (myConn && seekable > 0);
        if (seekable < 0){
          H.Clean();
          H.SetBody("The requested fragment is no longer kept in memory on the server and cannot be served.\n");
          myConn.SendNow(H.BuildResponse("412", "Fragment out of range"));
          H.Clean(); //clean for any possible next requests
          DEBUG_MSG(DLVL_WARN, "Fragment @ %llu too old", from);
          return;
        }
      }
      
      seek(from);
      lastVid = from * 90;
      
      H.SetHeader("Content-Type", "video/mp2t");
      H.StartResponse(H, myConn, VLCworkaround);
      PacketNumber = 0;
      parseData = true;
      wantRequest = false;
    }else{
      initialize();
      std::string request = H.url.substr(H.url.find("/", 5) + 1);
      H.Clean();
      if (H.url.find(".m3u8") != std::string::npos){
        H.SetHeader("Content-Type", "audio/x-mpegurl");
      }else{
        H.SetHeader("Content-Type", "audio/mpegurl");
      }
      H.SetHeader("Cache-Control", "no-cache");
      std::string manifest;
      if (request.find("/") == std::string::npos){
        manifest = liveIndex();
      }else{
        int selectId = atoi(request.substr(0,request.find("/")).c_str());
        manifest = liveIndex(selectId);
      }
      H.SetBody(manifest);
      H.SendResponse("200", "OK", myConn);
    }
  }
}
