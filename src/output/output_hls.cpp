#include "output_hls.h"
#include <mist/defines.h>
#include <mist/http_parser.h>
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
    for (std::map<int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      if (it->second.codec == "AAC"){
        audioId = it->first;
        audioName = it->second.getIdentifier();
        break;
      }
    }
    for (std::map<int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
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
#if DEBUG >= 8
    std::cerr << "Sending this index:" << std::endl << result.str() << std::endl;
#endif
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
    result << "#EXTM3U\r\n"
        "#EXT-X-TARGETDURATION:" << (longestFragment / 1000) + 1 << "\r\n"
        "#EXT-X-MEDIA-SEQUENCE:" << myMeta.tracks[tid].missedFrags << "\r\n";
    for (std::deque<DTSC::Fragment>::iterator it = myMeta.tracks[tid].fragments.begin(); it != myMeta.tracks[tid].fragments.end(); it++){
      long long int starttime = myMeta.tracks[tid].getKey(it->getNumber()).getTime();
      
      if (it != (myMeta.tracks[tid].fragments.end() - 1)){
        result << "#EXTINF:" << ((it->getDuration() + 500) / 1000) << ", no desc\r\n" << starttime << "_" << it->getDuration() + starttime << ".ts\r\n";
      }
    }
    if ( !myMeta.live){
      result << "#EXT-X-ENDLIST\r\n";
    }
#if DEBUG >= 8
    std::cerr << "Sending this index:" << std::endl << result.str() << std::endl;
#endif
    return result.str();
  } //liveIndex
  
  
  OutHLS::OutHLS(Socket::Connection & conn) : Output(conn) {
    haveAvcc = false;
    myConn.setHost(config->getString("ip"));
    streamName = config->getString("streamname");
  }
  
  OutHLS::~OutHLS() {}

  void OutHLS::onFail(){
    HTTP_S.Clean(); //make sure no parts of old requests are left in any buffers
    HTTP_S.SetBody("Stream not found. Sorry, we tried.");
    HTTP_S.SendResponse("404", "Stream not found", myConn);
    Output::onFail();
  }
  
  void OutHLS::init(Util::Config * cfg){
    Output::init(cfg);
    capa["name"] = "HLS";
    capa["desc"] = "Enables HTTP protocol Apple-specific streaming (also known as HLS).";
    capa["deps"] = "HTTP";
    capa["url_rel"] = "/hls/$/index.m3u8";
    capa["url_prefix"] = "/hls/$/";
    capa["socket"] = "http_hls";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][1u].append("AAC");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/application/vnd.apple.mpegurl";
    capa["methods"][0u]["priority"] = 9ll;
    cfg->addBasicConnectorOptions(capa);
    config = cfg;
  }
  
  void OutHLS::sendNext(){
    Socket::Buffer ToPack;
    char * ContCounter = 0;
    bool IsKeyFrame = false;
    
    char * dataPointer = 0;
    unsigned int dataLen = 0;
    currentPacket.getString("data", dataPointer, dataLen);

    if (currentPacket.getTime() >= until){
      DEBUG_MSG(DLVL_DEVEL, "(%d) Done sending fragment", getpid() );
      stop();
      wantRequest = true;
      HTTP_S.Chunkify("", 0, myConn);
      HTTP_S.Clean();
      return;
    }
    
    //detect packet type, and put converted data into ToPack.
    if (myMeta.tracks[currentPacket.getTrackId()].type == "video"){
      ToPack.append(TS::Packet::getPESVideoLeadIn(0ul, currentPacket.getTime() * 90));
      
      IsKeyFrame = currentPacket.getInt("keyframe");
      if (IsKeyFrame){
        if (!haveAvcc){
          avccbox.setPayload(myMeta.tracks[currentPacket.getTrackId()].init);
          haveAvcc = true;
        }
        ToPack.append(avccbox.asAnnexB());
      }
      unsigned int i = 0;
      while (i + 4 < (unsigned int)dataLen){
        unsigned int ThisNaluSize = (dataPointer[i] << 24) + (dataPointer[i+1] << 16) + (dataPointer[i+2] << 8) + dataPointer[i+3];
        if (ThisNaluSize + i + 4 > (unsigned int)dataLen){
          DEBUG_MSG(DLVL_WARN, "Too big NALU detected (%u > %d) - skipping!", ThisNaluSize + i + 4, dataLen);
          break;
        }
        ToPack.append("\000\000\000\001", 4);
        i += 4;
        ToPack.append(dataPointer + i, ThisNaluSize);
        i += ThisNaluSize;
      }
      ContCounter = &VideoCounter;
    }else if (myMeta.tracks[currentPacket.getTrackId()].type == "audio"){
      if (AppleCompat){
        ToPack.append(TS::Packet::getPESAudioLeadIn(7+dataLen, lastVid));
      }else{
        ToPack.append(TS::Packet::getPESAudioLeadIn(7+dataLen, currentPacket.getTime() * 90));
      }
      ToPack.append(TS::GetAudioHeader(dataLen, myMeta.tracks[currentPacket.getTrackId()].init));
      ToPack.append(dataPointer, dataLen);
      ContCounter = &AudioCounter;
    }
    
    bool first = true;
    //send TS packets
    while (ToPack.size()){
      if (PacketNumber % 42 == 0){
        HTTP_S.Chunkify(TS::PAT, 188, myConn);
        HTTP_S.Chunkify(TS::PMT, 188, myConn);
        PacketNumber += 2;
      }
      PackData.Clear();
      /// \todo Update according to sendHeader()'s generated data.
      //0x100 - 1 + currentPacket.getTrackId()
      if (myMeta.tracks[currentPacket.getTrackId()].type == "video"){
        PackData.PID(0x100);
      }else{
        PackData.PID(0x101);
      }
      PackData.ContinuityCounter((*ContCounter)++);
      if (first){
        PackData.UnitStart(1);
        if (IsKeyFrame){
          PackData.RandomAccess(1);
          PackData.PCR(currentPacket.getTime() * 27000);
        }
        first = false;
      }
      unsigned int toSend = PackData.AddStuffing(ToPack.bytes(184));
      std::string gonnaSend = ToPack.remove(toSend);
      PackData.FillFree(gonnaSend);
      HTTP_S.Chunkify(PackData.ToString(), 188, myConn);
      PacketNumber ++;
    }
  }

  int OutHLS::canSeekms(unsigned int ms){
    //no tracks? Frame too new by definition.
    if ( !myMeta.tracks.size()){
      return 1;
    }
    //loop trough all the tracks
    for (std::map<int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
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

  void OutHLS::onRequest(){
    while (HTTP_R.Read(myConn)){
      DEBUG_MSG(DLVL_DEVEL, "Received request: %s", HTTP_R.getUrl().c_str());
      AppleCompat = (HTTP_R.GetHeader("User-Agent").find("Apple") != std::string::npos);
      initialize();
      if (HTTP_R.url.find(".m3u") == std::string::npos){
        std::string tmpStr = HTTP_R.getUrl();
        std::string fmtStr = "/hls/" + streamName + "/%u_%u/%llu_%llu.ts";
        long long unsigned int from;
        sscanf(tmpStr.c_str(), fmtStr.c_str(), &vidTrack, &audTrack, &from, &until);
        DEBUG_MSG(DLVL_DEVEL, "Vid %u, Aud %u, From %llu, Until %llu", vidTrack, audTrack, from, until);
        selectedTracks.clear();
        selectedTracks.insert(vidTrack);
        selectedTracks.insert(audTrack);
        
        if (myMeta.live){
          /// \todo Detection of out-of-range parts.
          int seekable = canSeekms(from);
          if (seekable < 0){
            HTTP_S.Clean();
            HTTP_S.SetBody("The requested fragment is no longer kept in memory on the server and cannot be served.\n");
            myConn.SendNow(HTTP_S.BuildResponse("412", "Fragment out of range"));
            HTTP_R.Clean(); //clean for any possible next requests
            DEBUG_MSG(DLVL_WARN, "Fragment @ %llu too old", from);
            continue;
          }
          if (seekable > 0){
            HTTP_S.Clean();
            HTTP_S.SetBody("Proxy, re-request this in a second or two.\n");
            myConn.SendNow(HTTP_S.BuildResponse("208", "Ask again later"));
            HTTP_R.Clean(); //clean for any possible next requests
            DEBUG_MSG(DLVL_WARN, "Fragment @ %llu not available yet", from);
            continue;
          }
        }
        
        seek(from);
        lastVid = from * 90;
        
        HTTP_S.Clean();
        HTTP_S.SetHeader("Content-Type", "video/mp2t");
        HTTP_S.StartResponse(HTTP_R, myConn);
        PacketNumber = 0;
        parseData = true;
        wantRequest = false;
      }else{
        initialize();
        std::string request = HTTP_R.url.substr(HTTP_R.url.find("/", 5) + 1);
        HTTP_S.Clean();
        if (HTTP_R.url.find(".m3u8") != std::string::npos){
          HTTP_S.SetHeader("Content-Type", "audio/x-mpegurl");
        }else{
          HTTP_S.SetHeader("Content-Type", "audio/mpegurl");
        }
        HTTP_S.SetHeader("Cache-Control", "no-cache");
        std::string manifest;
        if (request.find("/") == std::string::npos){
          manifest = liveIndex();
        }else{
          int selectId = atoi(request.substr(0,request.find("/")).c_str());
          manifest = liveIndex(selectId);
        }
        HTTP_S.SetBody(manifest);
        HTTP_S.SendResponse("200", "OK", myConn);
      }
      HTTP_R.Clean(); //clean for any possible next requests
    }
  }
}
