#include "output_hls.h"
#include <mist/stream.h>
#include <unistd.h>

namespace Mist {
  ///\brief Builds an index file for HTTP Live streaming.
  ///\return The index file for HTTP Live Streaming.
  std::string OutHLS::liveIndex() {
    std::stringstream result;
    result << "#EXTM3U\r\n";
    int audioId = -1;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      if (it->second.codec == "AAC" || it->second.codec == "MP3" || it->second.codec == "AC3") {
        audioId = it->first;
        break;
      }
    }
    unsigned int vidTracks = 0;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      if (it->second.codec == "H264" || it->second.codec == "HEVC") {
        vidTracks++;
        int bWidth = it->second.bps;
        if (bWidth < 5) {
          bWidth = 5;
        }
        if (audioId != -1) {
          bWidth += myMeta.tracks[audioId].bps;
        }
        result << "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" << (bWidth * 8) << "\r\n";
        result << it->first;
        if (audioId != -1) {
          result << "_" << audioId;
        }
        result << "/index.m3u8\r\n";
      }
    }
    if (!vidTracks && audioId) {
      result << "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" << (myMeta.tracks[audioId].bps * 8) << "\r\n";
      result << audioId << "/index.m3u8\r\n";
    }
    DEBUG_MSG(DLVL_HIGH, "Sending this index: %s", result.str().c_str());
    return result.str();
  }

  std::string OutHLS::pushLiveIndex(){
    std::stringstream result;
    result << "#EXTM3U\r\n";
    std::set<unsigned int> audioTracks;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      if (it->second.codec == "AAC" || it->second.codec == "MP3" || it->second.codec == "AC3") {
        audioTracks.insert(it->first);
      }
    }
    if (!audioTracks.size()){
      audioTracks.insert(-1);
    }
    unsigned int vidTracks = 0;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      if (it->second.codec == "H264" || it->second.codec == "HEVC") {
        for (std::set<unsigned int>::iterator audIt = audioTracks.begin(); audIt != audioTracks.end(); audIt++){
          vidTracks++;
          int bWidth = it->second.bps;
          if (bWidth < 5) {
            bWidth = 5;
          }
          if (*audIt != -1) {
            bWidth += myMeta.tracks[*audIt].bps;
          }
          result << "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" << (bWidth * 8) << "\r\n";
          result << it->first;
          if (*audIt != -1) {
            result << "_" << *audIt;
          }
          result << "/index.m3u8\r\n";
        }
      }
    }
    if (!vidTracks && audioTracks.size()) {
      result << "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" << (myMeta.tracks[*audioTracks.begin()].bps * 8) << "\r\n";
      result << *audioTracks.begin() << "/index.m3u8\r\n";
    }
    DEBUG_MSG(DLVL_HIGH, "Sending this index: %s", result.str().c_str());
    return result.str();
  }

  std::string OutHLS::pushLiveIndex(int tid, unsigned long bTime, unsigned long eTime){
    updateMeta();
    if (!myMeta.tracks[tid].fragments.size()) {
      DEBUG_MSG(DLVL_FAIL, "liveIndex called with track %d, which has no fragments!", tid);
      return "";
    }
    std::stringstream result;
    //parse single track
    int longestFragment = 0;
    for (std::deque<DTSC::Fragment>::iterator it = myMeta.tracks[tid].fragments.begin(); (it + 1) != myMeta.tracks[tid].fragments.end(); it++) {
      if (it->getDuration() > longestFragment) {
        longestFragment = it->getDuration();
      }
    }
    if ((myMeta.tracks[tid].lastms - myMeta.tracks[tid].firstms) / myMeta.tracks[tid].fragments.size() > longestFragment) {
      longestFragment = (myMeta.tracks[tid].lastms - myMeta.tracks[tid].firstms) / myMeta.tracks[tid].fragments.size();
    }
    result << "#EXTM3U\r\n#EXT-X-TARGETDURATION:" << (longestFragment / 1000) + 1 << "\r\n";

    std::deque<std::string> lines;
    unsigned int skippedLines = 0;
    for (std::deque<DTSC::Fragment>::iterator it = myMeta.tracks[tid].fragments.begin(); it != myMeta.tracks[tid].fragments.end(); it++) {
      long long int starttime = myMeta.tracks[tid].getKey(it->getNumber()).getTime();
      long long duration = it->getDuration();
      if (duration <= 0) {
        duration = myMeta.tracks[tid].lastms - starttime;
      }
      if (starttime < bTime){
        skippedLines++;
      }
      if (starttime >= bTime && (starttime + duration) <= eTime){
        char lineBuf[400];
        snprintf(lineBuf, 400, "#EXTINF:%lld, no desc\r\n%lld_%lld.ts\r\n", ((duration + 500) / 1000), starttime, starttime + duration);
        lines.push_back(lineBuf);
      }
    }

    result << "#EXT-X-MEDIA-SEQUENCE:" << myMeta.tracks[tid].missedFrags + skippedLines << "\r\n";

    while (lines.size()) {
      result << lines.front();
      lines.pop_front();
    }
    if (!myMeta.live && eTime >= myMeta.tracks[tid].lastms) {
      result << "#EXT-X-ENDLIST\r\n";
    }
    DEBUG_MSG(DLVL_HIGH, "Sending this index: %s", result.str().c_str());
    return result.str();
  }


  std::string OutHLS::liveIndex(int tid) {
    updateMeta();
    std::stringstream result;
    //parse single track
    int longestFragment = 0;
    if (!myMeta.tracks[tid].fragments.size()) {
      DEBUG_MSG(DLVL_FAIL, "liveIndex called with track %d, which has no fragments!", tid);
      return "";
    }
    for (std::deque<DTSC::Fragment>::iterator it = myMeta.tracks[tid].fragments.begin(); (it + 1) != myMeta.tracks[tid].fragments.end(); it++) {
      if (it->getDuration() > longestFragment) {
        longestFragment = it->getDuration();
      }
    }
    if ((myMeta.tracks[tid].lastms - myMeta.tracks[tid].firstms) / myMeta.tracks[tid].fragments.size() > longestFragment) {
      longestFragment = (myMeta.tracks[tid].lastms - myMeta.tracks[tid].firstms) / myMeta.tracks[tid].fragments.size();
    }
    result << "#EXTM3U\r\n#EXT-X-TARGETDURATION:" << (longestFragment / 1000) + 1 << "\r\n";

    std::deque<std::string> lines;
    for (std::deque<DTSC::Fragment>::iterator it = myMeta.tracks[tid].fragments.begin(); it != myMeta.tracks[tid].fragments.end(); it++) {
      long long int starttime = myMeta.tracks[tid].getKey(it->getNumber()).getTime();
      long long duration = it->getDuration();
      if (duration <= 0) {
        duration = myMeta.tracks[tid].lastms - starttime;
      }
      char lineBuf[400];
      snprintf(lineBuf, 400, "#EXTINF:%lld, no desc\r\n%lld_%lld,ts\r\n", ((duration + 500) / 1000), starttime, starttime + duration);
      lines.push_back(lineBuf);
    }
    unsigned int skippedLines = 0;
    if (myMeta.live) {
      //only print the last segment when VoD
      lines.pop_back();
      /*LTS-START*/
      unsigned int skip = (( myMeta.tracks[tid].fragments.size()-1) * config->getInteger("startpos")) / 1000u;
      while (skippedLines < skip && lines.size() >= 3){
        lines.pop_front();
        skippedLines++;
      }
      if (config->getInteger("listlimit")) {
        unsigned long listlimit = config->getInteger("listlimit");
        while (lines.size() > listlimit) {
          lines.pop_front();
          skippedLines++;
        }
      }
      /*LTS-END*/
    }

    result << "#EXT-X-MEDIA-SEQUENCE:" << myMeta.tracks[tid].missedFrags + skippedLines << "\r\n";

    while (lines.size()) {
      result << lines.front();
      lines.pop_front();
    }
    if (!myMeta.live) {
      result << "#EXT-X-ENDLIST\r\n";
    }
    DEBUG_MSG(DLVL_HIGH, "Sending this index: %s", result.str().c_str());
    return result.str();
  } //liveIndex

  std::string OutHLS::generatePushList() {
    updateMeta();
    std::set<unsigned int> videoTracks;
    std::set<unsigned int> audioTracks;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      if (it->second.codec == "AAC" || it->second.codec == "MP3" || it->second.codec == "AC3") {
        audioTracks.insert(it->first);
      }
      if (it->second.codec == "H264" || it->second.codec == "H265"){
        videoTracks.insert(it->first);
      }
    }
    JSON::Value result;
    for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++) {
      std::stringstream tid;
      tid << it->second.trackID;
      result["tracks"][tid.str()] = it->second.toJSON(true);
    }
    for(std::set<unsigned int>::iterator it = videoTracks.begin(); it != videoTracks.end(); it++){
      for(std::set<unsigned int>::iterator it2 = audioTracks.begin(); it2 != audioTracks.end(); it2++){
        JSON::Value quality;
        std::stringstream identifier;
        identifier << "/" << *it << "_" << *it2;
        quality["index"] = "/push" + identifier.str() + "/index_\%llu_\%llu.m3u8"; 
        quality["segment"] = identifier.str() + "/\%llu_\%llu.ts"; 
        quality["video"] = *it;
        quality["audio"] = *it2;
        quality["id"] = identifier.str();
        std::deque<DTSC::Fragment>::iterator it3 = myMeta.tracks[*it].fragments.begin();
        for (int i = 0; i < 2; i++){
          if (it3 != myMeta.tracks[*it].fragments.end()){
            ++it3;
          }
        }
        for (; it3 != myMeta.tracks[*it].fragments.end(); it3++) {
          if (myMeta.live && it3 == (myMeta.tracks[*it].fragments.end() - 1)){
            //Skip the current last fragment if we are live
            continue;
          }
          long long int starttime = myMeta.tracks[*it].getKey(it3->getNumber()).getTime();
          std::stringstream line;
          long long duration = it3->getDuration();
          if (duration <= 0) {
            duration = myMeta.tracks[*it].lastms - starttime;
          }
          std::stringstream segmenturl;
          segmenturl << identifier.str() << "/" << starttime << "_" << duration + starttime << ".ts";
          JSON::Value segment;
          //segment["url"] = segmenturl.str();
          segment["time"] = starttime;
          segment["duration"] = duration;
          segment["number"] = (unsigned int)it3->getNumber();
          quality["segments"].append(segment);
        }
        result["qualities"].append(quality);
      }
    }
    return result.toString();;
  }




  OutHLS::OutHLS(Socket::Connection & conn) : TSOutput(conn) {
    realTime = 0;
  }

  OutHLS::~OutHLS() {}

  void OutHLS::init(Util::Config * cfg) {
    HTTPOutput::init(cfg);
    capa["name"] = "HLS";
    capa["desc"] = "Enables HTTP protocol Apple-specific streaming (also known as HLS).";
    capa["url_rel"] = "/hls/$/index.m3u8";
    capa["url_prefix"] = "/hls/$/";
    capa["url_pushlist"] = "/hls/$/push/list";
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("AC3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/application/vnd.apple.mpegurl";
    capa["methods"][0u]["priority"] = 9ll;
    /*LTS-START*/
    cfg->addOption("listlimit", JSON::fromString("{\"arg\":\"integer\",\"default\":0,\"short\":\"y\",\"long\":\"list-limit\",\"help\":\"Maximum number of parts in live playlists (0 = infinite).\"}"));
    capa["optional"]["listlimit"]["name"] = "Live playlist limit";
    capa["optional"]["listlimit"]["help"] = "Maximum number of parts in live playlists. (0 = infinite)";
    capa["optional"]["listlimit"]["default"] = 0ll;
    capa["optional"]["listlimit"]["type"] = "uint";
    capa["optional"]["listlimit"]["option"] = "--list-limit";
    /*LTS-END*/
  }

  int OutHLS::canSeekms(unsigned int ms) {
    //no tracks? Frame too new by definition.
    if (!myMeta.tracks.size()) {
      return 1;
    }
    //check main track
    DTSC::Track & mainTrack = myMeta.tracks[*selectedTracks.begin()];
    //return "too late" if one track is past this point
    if (ms < mainTrack.firstms){
      return -1;
    }
    //return "too early" if one track is not yet at this point
    if (ms > mainTrack.lastms){
      return 1;
    }
    return 0;
  }

  void OutHLS::onHTTP() {
    std::string method = H.method;

    if (H.url == "/crossdomain.xml") {
      H.Clean();
      H.SetHeader("Content-Type", "text/xml");
      H.SetHeader("Server", "MistServer/" PACKAGE_VERSION);
      H.setCORSHeaders();
      if(method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }
      H.SetBody("<?xml version=\"1.0\"?><!DOCTYPE cross-domain-policy SYSTEM \"http://www.adobe.com/xml/dtds/cross-domain-policy.dtd\"><cross-domain-policy><allow-access-from domain=\"*\" /><site-control permitted-cross-domain-policies=\"all\"/></cross-domain-policy>");
      H.SendResponse("200", "OK", myConn);
      H.Clean(); //clean for any possible next requests
      return;
    } //crossdomain.xml

    if (H.method == "OPTIONS") {
      H.Clean();
      H.SetHeader("Content-Type", "application/octet-stream");
      H.SetHeader("Cache-Control", "no-cache");
      H.setCORSHeaders();
      H.SetBody("");
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }

    if (H.url.find("hls") == std::string::npos) {
      myConn.close();
      return;
    }


    appleCompat = (H.GetHeader("User-Agent").find("iPad") != std::string::npos) || (H.GetHeader("User-Agent").find("iPod") != std::string::npos) || (H.GetHeader("User-Agent").find("iPhone") != std::string::npos);
    bool VLCworkaround = false;
    if (H.GetHeader("User-Agent").substr(0, 3) == "VLC") {
      std::string vlcver = H.GetHeader("User-Agent").substr(4);
      if (vlcver[0] == '0' || vlcver[0] == '1' || (vlcver[0] == '2' && vlcver[2] < '2')) {
        DEBUG_MSG(DLVL_INFO, "Enabling VLC version < 2.2.0 bug workaround.");
        VLCworkaround = true;
      }
    }
    initialize();
    if (H.url.substr(5 + streamName.size(), 5) == "/push"){
      std::string relPushUrl = H.url.substr(10 + streamName.size());
      H.Clean();
      if (relPushUrl == "/list"){
        H.SetBody(generatePushList());
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }
      if (relPushUrl.find(".m3u8") != std::string::npos) {
        H.SetHeader("Content-Type", "audio/x-mpegurl");
      } else {
        H.SetHeader("Content-Type", "audio/mpegurl");
      }
      if (relPushUrl == "/index.m3u8"){
        H.SetHeader("Cache-Control", "no-cache");
        H.setCORSHeaders();
        H.SetBody(pushLiveIndex());
        H.SendResponse("200", "OK", myConn);
        H.Clean(); //clean for any possible next requests
        return;
      }else {
        unsigned int vTrack;
        unsigned int aTrack;
        unsigned long long bTime;
        unsigned long long eTime;
        if (sscanf(relPushUrl.c_str(), "/%u_%u/index_%llu_%llu.m3u", &vTrack, &aTrack, &bTime, &eTime) == 4) {
          if (eTime < bTime){
            eTime = bTime;
          }
          H.SetHeader("Cache-Control", "no-cache");
          H.setCORSHeaders();
          H.SetBody(pushLiveIndex(vTrack, bTime, eTime));
          H.SendResponse("200", "OK", myConn);
          H.Clean(); //clean for any possible next requests
          return;
        }
      }
      H.SetBody("The HLS URL wasn't understood - what did you want, exactly?\n");
      myConn.SendNow(H.BuildResponse("404", "URL mismatch"));
      H.Clean(); //clean for any possible next requests
      return;
    }else if (H.url.find(".m3u") == std::string::npos) {
      std::string tmpStr = H.getUrl().substr(5 + streamName.size());
      long long unsigned int from;
      if (sscanf(tmpStr.c_str(), "/%u_%u/%llu_%llu.ts", &vidTrack, &audTrack, &from, &until) != 4) {
        if (sscanf(tmpStr.c_str(), "/%u/%llu_%llu.ts", &vidTrack, &from, &until) != 3) {
          DEBUG_MSG(DLVL_MEDIUM, "Could not parse URL: %s", H.getUrl().c_str());
          H.Clean();
          H.setCORSHeaders();
          H.SetBody("The HLS URL wasn't understood - what did you want, exactly?\n");
          myConn.SendNow(H.BuildResponse("404", "URL mismatch"));
          H.Clean(); //clean for any possible next requests
          return;
        } else {
          selectedTracks.clear();
          selectedTracks.insert(vidTrack);
        }
      } else {
        selectedTracks.clear();
        selectedTracks.insert(vidTrack);
        selectedTracks.insert(audTrack);
      }
      for (std::map<unsigned int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        if (it->second.codec == "ID3"){
          selectedTracks.insert(it->first);
        }
      }

      if (myMeta.live) {
        unsigned int timeout = 0;
        int seekable;
        do {
          seekable = canSeekms(from);
          /// \todo Detection of out-of-range parts.
          if (seekable > 0) {
            //time out after 21 seconds
            if (++timeout > 42) {
              myConn.close();
              break;
            }
            Util::sleep(500);
            updateMeta();
          }
        } while (myConn && seekable > 0);
        if (seekable < 0) {
          H.Clean();
          H.setCORSHeaders();
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
      
      H.Clean();
      H.SetHeader("Content-Type", "video/mp2t");
      H.setCORSHeaders();
      if(method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }
      H.StartResponse(H, myConn, VLCworkaround);

      unsigned int fragCounter = myMeta.tracks[vidTrack].missedFrags;
      for (std::deque<DTSC::Fragment>::iterator it = myMeta.tracks[vidTrack].fragments.begin(); it != myMeta.tracks[vidTrack].fragments.end(); it++) {
        long long int starttime = myMeta.tracks[vidTrack].getKey(it->getNumber()).getTime();
        if (starttime <= from && starttime + it->getDuration() > from) {
          EXTREME_MSG("setting continuity counter for PAT/PMT to %d", fragCounter);
          contCounters[0] = fragCounter;   //PAT continuity counter
          contCounters[4096] = fragCounter; //PMT continuity counter
          break;
        }
        ++fragCounter;
      }
      packCounter = 0;
      parseData = true;
      wantRequest = false;
    } else {
      initialize();
      std::string request = H.url.substr(H.url.find("/", 5) + 1);
      H.Clean();
      if (H.url.find(".m3u8") != std::string::npos) {
        H.SetHeader("Content-Type", "audio/x-mpegurl");
      } else {
        H.SetHeader("Content-Type", "audio/mpegurl");
      }
      H.SetHeader("Cache-Control", "no-cache");
      H.setCORSHeaders();
      if(method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        H.Clean();
        return;
      }
      std::string manifest;
      if (request.find("/") == std::string::npos) {
        manifest = liveIndex();
      } else {
        int selectId = atoi(request.substr(0, request.find("/")).c_str());
        manifest = liveIndex(selectId);
      }
      H.SetBody(manifest);
      H.SendResponse("200", "OK", myConn);
    }
  }


  void OutHLS::sendTS(const char * tsData, unsigned int len) {
    H.Chunkify(tsData, len, myConn);
  }
}
