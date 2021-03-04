#include "output_hls.h"
#include <mist/langcodes.h> /*LTS*/
#include <mist/stream.h>
#include <mist/url.h>
#include <unistd.h>

namespace Mist{
  bool OutHLS::isReadyForPlay(){
    if (!isInitialized){return false;}
    meta.reloadReplacedPagesIfNeeded();
    if (!M.getValidTracks().size()){return false;}
    uint32_t mainTrack = M.mainTrack();
    if (mainTrack == INVALID_TRACK_ID){return false;}
    DTSC::Fragments fragments(M.fragments(mainTrack));
    return fragments.getValidCount() > 4;
  }

  ///\brief Builds an index file for HTTP Live streaming.
  ///\return The index file for HTTP Live Streaming.
  std::string OutHLS::liveIndex(){
    std::stringstream result;
    selectDefaultTracks();
    result << "#EXTM3U\r\n";
    size_t audioId = INVALID_TRACK_ID;
    size_t vidTracks = 0;
    bool hasSubs = false;
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); ++it){
      if (audioId == INVALID_TRACK_ID && M.getType(it->first) == "audio"){audioId = it->first;}
      if (!hasSubs && M.getCodec(it->first) == "subtitle"){hasSubs = true;}
    }
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); ++it){
      if (M.getType(it->first) == "video"){
        ++vidTracks;
        int bWidth = M.getBps(it->first);
        if (bWidth < 5){bWidth = 5;}
        if (audioId != INVALID_TRACK_ID){bWidth += M.getBps(audioId);}
        result << "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" << (bWidth * 8);
        result << ",RESOLUTION=" << M.getWidth(it->first) << "x" << M.getHeight(it->first);
        if (M.getFpks(it->first)){
          result << ",FRAME-RATE=" << (float)M.getFpks(it->first) / 1000;
        }
        if (hasSubs){result << ",SUBTITLES=\"sub1\"";}
        result << ",CODECS=\"";
        result << Util::codecString(M.getCodec(it->first), M.getInit(it->first));
        if (audioId != INVALID_TRACK_ID){
          result << "," << Util::codecString(M.getCodec(audioId), M.getInit(audioId));
        }
        result << "\"\r\n" << it->first;
        if (audioId != INVALID_TRACK_ID){result << "_" << audioId;}
        if (hasSessionIDs()){
          result << "/index.m3u8?sessId=" << sid;
          if (origin.size()){result << "&origin=" << origin;}
        }else{
          result << "/index.m3u8";
          if (origin.size()){result << "?origin=" << origin;}
        }
        result << "\r\n";
      }else if (M.getCodec(it->first) == "subtitle"){

        if (M.getLang(it->first).empty()){meta.setLang(it->first, "und");}

        result << "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"sub1\",LANGUAGE=\"" << M.getLang(it->first)
               << "\",NAME=\"" << Encodings::ISO639::decode(M.getLang(it->first))
               << "\",AUTOSELECT=NO,DEFAULT=NO,FORCED=NO,URI=\"" << it->first << "/index.m3u8\""
               << "\r\n";
      }
    }
    if (!vidTracks && audioId != INVALID_TRACK_ID){
      result << "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" << (M.getBps(audioId) * 8);
      result << ",CODECS=\"" << Util::codecString(M.getCodec(audioId), M.getInit(audioId)) << "\"";
      result << "\r\n";
      result << audioId << "/index.m3u8";
      if (origin.size()){result << "?origin=" << origin;}
      result << "\r\n";
    }
    return result.str();
  }

  std::string OutHLS::liveIndex(size_t tid, const std::string &sessId, const std::string &urlPrefix){
    //Timing track is current track, unless non-video, then time by video track
    size_t timingTid = tid;
    if (M.getType(timingTid) != "video"){timingTid = M.mainTrack();}
    if (timingTid == INVALID_TRACK_ID){timingTid = tid;}

    std::stringstream result;
    // parse single track
    uint32_t targetDuration = (M.biggestFragment(timingTid) / 1000) + 1;
    result << "#EXTM3U\r\n#EXT-X-VERSION:";

    result << (M.getEncryption(tid) == "" ? "3" : "5");

    result << "\r\n#EXT-X-TARGETDURATION:" << targetDuration << "\r\n";

    if (M.getEncryption(tid) != ""){
      result << "#EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"";
      result << "urlHere";
      result << "\",KEYFORMAT=\"com.apple.streamingkeydelivery" << std::endl;
    }

    std::deque<std::string> lines;
    std::deque<uint16_t> durations;
    uint32_t totalDuration = 0;
    DTSC::Keys keys(M.keys(timingTid));
    DTSC::Fragments fragments(M.fragments(timingTid));
    uint32_t firstFragment = fragments.getFirstValid();
    uint32_t endFragment = fragments.getEndValid();
    for (int i = firstFragment; i < endFragment; i++){
      uint64_t duration = fragments.getDuration(i);
      size_t keyNumber = fragments.getFirstKey(i);
      uint64_t startTime = keys.getTime(keyNumber);
      if (!duration){duration = M.getLastms(timingTid) - startTime;}
      double floatDur = (double)duration / 1000;
      char lineBuf[400];

      if (M.getCodec(tid) == "subtitle"){
        snprintf(lineBuf, 400, "#EXTINF:%f,\r\n../../../%s.webvtt?track=%zu&from=%" PRIu64 "&to=%" PRIu64 "\r\n",
                 (double)duration / 1000, streamName.c_str(), tid, startTime, startTime + duration);
      }else{
        if (origin.size()){
          if (sessId.size()){
            snprintf(lineBuf, 400, "#EXTINF:%f,\r\n%s%" PRIu64 "_%" PRIu64 ".ts?sessId=%s&origin=%s\r\n",
                     floatDur, urlPrefix.c_str(), startTime, startTime + duration, sessId.c_str(), origin.c_str());
          }else{
            snprintf(lineBuf, 400, "#EXTINF:%f,\r\n%s%" PRIu64 "_%" PRIu64 ".ts?origin=%s\r\n", floatDur, urlPrefix.c_str(),
                     startTime, startTime + duration, origin.c_str());
          }
        }else{
          if (sessId.size()){
            snprintf(lineBuf, 400, "#EXTINF:%f,\r\n%s%" PRIu64 "_%" PRIu64 ".ts?sessId=%s\r\n",
                     floatDur, urlPrefix.c_str(), startTime, startTime + duration, sessId.c_str());
          }else{
            snprintf(lineBuf, 400, "#EXTINF:%f,\r\n%s%" PRIu64 "_%" PRIu64 ".ts\r\n", floatDur, urlPrefix.c_str(),
                     startTime, startTime + duration);
          }
        }
      }
      totalDuration += duration;
      durations.push_back(duration);
      lines.push_back(lineBuf);
    }
    size_t skippedLines = 0;
    if (M.getLive() && lines.size()){
      // only print the last segment when non-live
      lines.pop_back();
      totalDuration -= durations.back();
      durations.pop_back();
      // skip the first two segments when live, unless that brings us under 4 target durations
      while ((totalDuration - durations.front()) > (targetDuration * 4000) && skippedLines < 2){
        lines.pop_front();
        totalDuration -= durations.front();
        durations.pop_front();
        ++skippedLines;
      }
      /*LTS-START*/
      // remove lines to reduce size towards listlimit setting - but keep at least 4X target
      // duration available
      uint64_t listlimit = config->getInteger("listlimit");
      if (listlimit){
        while (lines.size() > listlimit && (totalDuration - durations.front()) > (targetDuration * 4000)){
          lines.pop_front();
          totalDuration -= durations.front();
          durations.pop_front();
          ++skippedLines;
        }
      }
      /*LTS-END*/
    }

    result << "#EXT-X-MEDIA-SEQUENCE:" << firstFragment + skippedLines << "\r\n";

    for (std::deque<std::string>::iterator it = lines.begin(); it != lines.end(); it++){
      result << *it;
    }
    if (!M.getLive() || !totalDuration){result << "#EXT-X-ENDLIST\r\n";}
    HIGH_MSG("Sending this index: %s", result.str().c_str());
    return result.str();
  }

  OutHLS::OutHLS(Socket::Connection &conn) : TSOutput(conn){
    uaDelay = 0;
    realTime = 0;
    until = 0xFFFFFFFFFFFFFFFFull;
  }

  OutHLS::~OutHLS(){}

  void OutHLS::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "HLS";
    capa["friendly"] = "Apple segmented over HTTP (HLS)";
    capa["desc"] =
        "Segmented streaming in Apple (TS-based) format over HTTP ( = HTTP Live Streaming)";
    capa["url_rel"] = "/hls/$/index.m3u8";
    capa["url_prefix"] = "/hls/$/";
    capa["codecs"][0u][0u].append("+HEVC");
    capa["codecs"][0u][1u].append("+H264");
    capa["codecs"][0u][2u].append("+MPEG2");
    capa["codecs"][0u][3u].append("+AAC");
    capa["codecs"][0u][4u].append("+MP3");
    capa["codecs"][0u][5u].append("+AC3");
    capa["codecs"][0u][6u].append("+MP2");
    capa["codecs"][0u][6u].append("+subtitle");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/application/vnd.apple.mpegurl";
    capa["methods"][0u]["hrn"] = "HLS (TS)";
    capa["methods"][0u]["priority"] = 9;
    // MP3 only works on Edge/Apple
    capa["exceptions"]["codec:MP3"] = JSON::fromString(
        "[[\"blacklist\",[\"Mozilla/"
        "\"]],[\"whitelist\",[\"iPad\",\"iPhone\",\"iPod\",\"MacIntel\",\"Edge\"]]]");
    capa["exceptions"]["codec:HEVC"] = JSON::fromString("[[\"blacklist\"]]");
    /*LTS-START*/
    cfg->addOption("listlimit",
                   JSON::fromString(
                       "{\"arg\":\"integer\",\"default\":0,\"short\":\"y\",\"long\":\"list-limit\","
                       "\"help\":\"Maximum number of parts in live playlists (0 = infinite).\"}"));
    capa["optional"]["listlimit"]["name"] = "Live playlist limit";
    capa["optional"]["listlimit"]["help"] =
        "Maximum number of parts in live playlists. (0 = infinite)";
    capa["optional"]["listlimit"]["default"] = 0;
    capa["optional"]["listlimit"]["type"] = "uint";
    capa["optional"]["listlimit"]["option"] = "--list-limit";

    cfg->addOption("nonchunked",
                   JSON::fromString("{\"short\":\"C\",\"long\":\"nonchunked\",\"help\":\"Do not "
                                    "send chunked, but buffer whole segments.\"}"));
    capa["optional"]["nonchunked"]["name"] = "Send whole segments";
    capa["optional"]["nonchunked"]["help"] =
        "Disables chunked transfer encoding, forcing per-segment buffering. Reduces performance "
        "significantly, but increases compatibility somewhat.";
    capa["optional"]["nonchunked"]["option"] = "--nonchunked";

    cfg->addOption("mergesessions",
                   JSON::fromString("{\"short\":\"M\",\"long\":\"mergesessions\",\"help\":\"Merge "
                                    "together sessions from one user into a single session.\"}"));
    capa["optional"]["mergesessions"]["name"] = "Merge sessions";
    capa["optional"]["mergesessions"]["help"] =
        "If enabled, merges together all views from a single user into a single combined session. "
        "If disabled, each view (main playlist request) is a separate session.";
    capa["optional"]["mergesessions"]["option"] = "--mergesessions";

    cfg->addOption("chunkpath",
                   JSON::fromString(
                       "{\"arg\":\"string\",\"default\":"",\"short\":\"e\",\"long\":\"chunkpath\","
                       "\"help\":\"Alternate URL path to prepend to chunk paths, for serving through e.g. a CDN\"}"));
    capa["optional"]["chunkpath"]["name"] = "Prepend path for chunks";
    capa["optional"]["chunkpath"]["help"] = "Chunks will be served from this path. This also disables sessions IDs for chunks.";
    capa["optional"]["chunkpath"]["default"] = "";
    capa["optional"]["chunkpath"]["type"] = "str";
    capa["optional"]["chunkpath"]["option"] = "--chunkpath";
    capa["optional"]["chunkpath"]["short"] = "e";
    capa["optional"]["chunkpath"]["default"] = "";
    /*LTS-END*/
  }

  void OutHLS::preHTTP(){
    if (H.GetVar("sessId").size()){
      sid = H.GetVar("sessId");
    }
    HTTPOutput::preHTTP();
  }

  void OutHLS::onHTTP(){
    std::string method = H.method;
    origin = H.GetVar("origin");

    if (H.url == "/crossdomain.xml"){
      H.SetHeader("Content-Type", "text/xml");
      H.SetHeader("Server", APPIDENT);
      H.setCORSHeaders();
      if (method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        responded = true;
        return;
      }
      H.SetBody("<?xml version=\"1.0\"?><!DOCTYPE cross-domain-policy SYSTEM "
                "\"http://www.adobe.com/xml/dtds/"
                "cross-domain-policy.dtd\"><cross-domain-policy><allow-access-from domain=\"*\" "
                "/><site-control permitted-cross-domain-policies=\"all\"/></cross-domain-policy>");
      H.SendResponse("200", "OK", myConn);
      responded = true;
      return;
    }// crossdomain.xml

    if (H.method == "OPTIONS"){
      bool isTS = (HTTP::URL(H.url).getExt().substr(0, 3) != "m3u");
      H.setCORSHeaders();
      if (isTS){
        H.SetHeader("Content-Type", "video/mp2t");
      }else{
        H.SetHeader("Content-Type", "application/vnd.apple.mpegurl");
      }
      if (isTS && (!hasSessionIDs() || config->getOption("chunkpath"))){
        H.SetHeader("Cache-Control", "public, max-age="+JSON::Value(M.getDuration(getMainSelectedTrack())/1000).asString()+", immutable");
        H.SetHeader("Pragma", "");
        H.SetHeader("Expires", "");
      }else{
        H.SetHeader("Cache-Control", "no-cache");
      }
      H.SetBody("");
      H.SendResponse("200", "OK", myConn);
      responded = true;
      return;
    }

    if (H.url.find("hls") == std::string::npos){
      onFail("HLS handler active, but this is not a HLS URL. Eh... What...?");
      return;
    }

    std::string userAgent = H.GetHeader("User-Agent");
    bool VLCworkaround = false;
    if (userAgent.substr(0, 3) == "VLC"){
      std::string vlcver = userAgent.substr(4);
      if (vlcver[0] == '0' || vlcver[0] == '1' || (vlcver[0] == '2' && vlcver[2] < '2')){
        INFO_MSG("Enabling VLC version < 2.2.0 bug workaround.");
        VLCworkaround = true;
      }
    }

    initialize();
    if (!keepGoing()){return;}

    if (HTTP::URL(H.url).getExt().substr(0, 3) != "m3u"){
      std::string tmpStr = H.getUrl().substr(5 + streamName.size());
      uint64_t from;
      if (sscanf(tmpStr.c_str(), "/%zu_%zu/%" PRIu64 "_%" PRIu64 ".ts", &vidTrack, &audTrack, &from, &until) != 4){
        if (sscanf(tmpStr.c_str(), "/%zu/%" PRIu64 "_%" PRIu64 ".ts", &vidTrack, &from, &until) != 3){
          MEDIUM_MSG("Could not parse URL: %s", H.getUrl().c_str());
          H.setCORSHeaders();
          H.SetBody("The HLS URL wasn't understood - what did you want, exactly?\n");
          myConn.SendNow(H.BuildResponse("404", "URL mismatch"));
          responded = true;
          return;
        }
        userSelect.clear();
        userSelect[vidTrack].reload(streamName, vidTrack);
      }else{
        userSelect.clear();
        userSelect[vidTrack].reload(streamName, vidTrack);
        userSelect[audTrack].reload(streamName, audTrack);
      }
      std::set<size_t> validTracks = getSupportedTracks();
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
        if (M.getCodec(*it) == "ID3"){userSelect[*it].reload(streamName, *it);}
      }

      if (M.getLive() && from < M.getFirstms(vidTrack)){
        H.setCORSHeaders();
        H.SetBody("The requested fragment is no longer kept in memory on the server and cannot be "
                  "served.\n");
        myConn.SendNow(H.BuildResponse("404", "Fragment out of range"));
        WARN_MSG("Fragment @ %" PRIu64 " too old", from);
        responded = true;
        return;
      }

      H.SetHeader("Content-Type", "video/mp2t");
      H.setCORSHeaders();
      if (hasSessionIDs() && !config->getOption("chunkpath")){
        H.SetHeader("Cache-Control", "no-cache");
      }else{
        H.SetHeader("Cache-Control", "public, max-age="+JSON::Value(M.getDuration(getMainSelectedTrack())/1000).asString()+", immutable");
        H.SetHeader("Pragma", "");
        H.SetHeader("Expires", "");
      }
      if (method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        responded = true;
        return;
      }

      H.StartResponse(H, myConn, VLCworkaround || config->getBool("nonchunked"));
      responded = true;
      // we assume whole fragments - but timestamps may be altered at will
      uint32_t fragIndice = M.getFragmentIndexForTime(vidTrack, from);
      contPAT = fragIndice; // PAT continuity counter
      contPMT = fragIndice; // PMT continuity counter
      contSDT = fragIndice; // SDT continuity counter
      packCounter = 0;
      parseData = true;
      wantRequest = false;
      seek(from);
      ts_from = from;
    }else{
      initialize();
      const std::string reqUrl = "./"+H.url;
      std::string request = H.url.substr(H.url.find("/", 5) + 1);
      H.setCORSHeaders();
      H.SetHeader("Content-Type", "application/vnd.apple.mpegurl");
      if (!M.getValidTracks().size()){
        H.SendResponse("404", "Not online or found", myConn);
        return;
      }
      if (method == "OPTIONS" || method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        return;
      }
      std::string manifest;
      if (request.find("/") == std::string::npos){
        manifest = liveIndex();
      }else{
        size_t idx = atoi(request.substr(0, request.find("/")).c_str());
        if (!M.getValidTracks().count(idx)){
          H.SendResponse("404", "No corresponding track found", myConn);
          return;
        }
        if (config->getString("chunkpath").size()){
          manifest = liveIndex(idx, "", HTTP::URL(config->getString("chunkpath")).link(reqUrl).link("./").getUrl());
        }else{
          manifest = liveIndex(idx, sid);
        }
      }
      H.SetBody(manifest);
      H.SendResponse("200", "OK", myConn);
      responded = true;
    }
  }

  void OutHLS::sendNext(){
    // First check if we need to stop.
    if (thisPacket.getTime() >= until){
      stop();
      wantRequest = true;

      // Ensure alignment of contCounters, to prevent discontinuities.
      for (std::map<size_t, uint16_t>::iterator it = contCounters.begin(); it != contCounters.end(); it++){
        if (it->second % 16 != 0){
          packData.clear();
          packData.setPID(it->first);
          packData.addStuffing();
          while (it->second % 16 != 0){
            packData.setContinuityCounter(++it->second);
            sendTS(packData.checkAndGetBuffer());
          }
          packData.clear();
        }
      }

      // Signal end of data
      H.Chunkify("", 0, myConn);
      H.Clean();
      return;
    }
    // Invoke the generic TS output sendNext handler
    TSOutput::sendNext();
  }

  void OutHLS::sendTS(const char *tsData, size_t len){H.Chunkify(tsData, len, myConn);}

  void OutHLS::onFail(const std::string &msg, bool critical){
    if (HTTP::URL(H.url).getExt().substr(0, 3) != "m3u"){
      HTTPOutput::onFail(msg, critical);
      return;
    }
    H.Clean(); // make sure no parts of old requests are left in any buffers
    H.SetHeader("Server", APPIDENT);
    H.setCORSHeaders();
    H.SetHeader("Content-Type", "application/vnd.apple.mpegurl");
    H.SetHeader("Cache-Control", "no-cache");
    H.SetBody("#EXTM3U\r\n#EXT-X-ERROR: " + msg + "\r\n#EXT-X-ENDLIST\r\n");
    H.SendResponse("200", "OK", myConn);
    Output::onFail(msg, critical);
  }
}// namespace Mist
