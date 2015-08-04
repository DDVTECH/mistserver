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
    unsigned int vidTracks = 0;
    for (std::map<unsigned int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      if (it->second.codec == "H264"){
        vidTracks++;
        int bWidth = it->second.bps * 2;
        if (bWidth < 5){
          bWidth = 5;
        }
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
    if (!vidTracks && audioId){
      result << "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" << myMeta.tracks[audioId].bps * 20 << "\r\n";
      result << audioId << "/index.m3u8\r\n";
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
    if ((myMeta.tracks[tid].lastms - myMeta.tracks[tid].firstms) / myMeta.tracks[tid].fragments.size() > longestFragment){
      longestFragment = (myMeta.tracks[tid].lastms - myMeta.tracks[tid].firstms) / myMeta.tracks[tid].fragments.size();
    }
    result << "#EXTM3U\r\n#EXT-X-TARGETDURATION:" << (longestFragment / 1000) + 1 << "\r\n";
        
    std::deque<std::string> lines;
    for (std::deque<DTSC::Fragment>::iterator it = myMeta.tracks[tid].fragments.begin(); it != myMeta.tracks[tid].fragments.end(); it++){
      long long int starttime = myMeta.tracks[tid].getKey(it->getNumber()).getTime();
      std::stringstream line;
      long long duration = it->getDuration();
      if (duration <= 0){
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
  
  
  OutHLS::OutHLS(Socket::Connection & conn) : TSOutput(conn){
    realTime = 0;
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
    if (H.url == "/crossdomain.xml"){
      H.Clean();
      H.SetHeader("Content-Type", "text/xml");
      H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
      H.setCORSHeaders();
      H.SetBody("<?xml version=\"1.0\"?><!DOCTYPE cross-domain-policy SYSTEM \"http://www.adobe.com/xml/dtds/cross-domain-policy.dtd\"><cross-domain-policy><allow-access-from domain=\"*\" /><site-control permitted-cross-domain-policies=\"all\"/></cross-domain-policy>");
      H.SendResponse("200", "OK", myConn);
      H.Clean(); //clean for any possible next requests
      return;
    } //crossdomain.xml
    
    if (H.method == "OPTIONS"){
      H.Clean();
      H.SetHeader("Content-Type", "application/octet-stream");
      H.SetHeader("Cache-Control", "no-cache");
      H.setCORSHeaders();
      H.SetBody("");
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    
    if (H.url.find("hls") == std::string::npos){
      myConn.close();
      return;
    }
    
    appleCompat = (H.GetHeader("User-Agent").find("iPad") != std::string::npos) || (H.GetHeader("User-Agent").find("iPod") != std::string::npos)|| (H.GetHeader("User-Agent").find("iPhone") != std::string::npos);
    bool VLCworkaround = false;
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
      ts_from = from;
      lastVid = from * 90;
      
      H.SetHeader("Content-Type", "video/mp2t");
      H.setCORSHeaders();
      H.StartResponse(H, myConn, VLCworkaround);

      unsigned int fragCounter = myMeta.tracks[vidTrack].missedFrags;
      for (std::deque<DTSC::Fragment>::iterator it = myMeta.tracks[vidTrack].fragments.begin(); it != myMeta.tracks[vidTrack].fragments.end(); it++){
        long long int starttime = myMeta.tracks[vidTrack].getKey(it->getNumber()).getTime();        
        if (starttime <= from && starttime + it->getDuration() > from){
          EXTREME_MSG("setting continuity counter for PAT/PMT to %d",fragCounter);
          contCounters[0]=fragCounter;     //PAT continuity counter
          contCounters[4096]=fragCounter;  //PMT continuity counter
          break;
        }
        ++fragCounter;
      }
      packCounter = 0;
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
      H.setCORSHeaders();
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


  void OutHLS::sendTS(const char * tsData, unsigned int len){    
    H.Chunkify(tsData, len, myConn);
  }
}
