#include "output_hls.h"
#include <mist/hls_support.h>
// #include <mist/langcodes.h> /*LTS*/
// #include <mist/stream.h>
// #include <mist/url.h>
// #include <unistd.h>

int64_t bootMsOffset; // boot time in ms
uint64_t systemBoot;  // time since boot in ms
const std::string hlsMediaFormat = ".ts";

namespace Mist{
  bool OutHLS::isReadyForPlay(){
    if (!isInitialized){return false;}
    meta.reloadReplacedPagesIfNeeded();
    if (!M.getValidTracks().size()){return false;}
    uint32_t mainTrack = M.mainTrack();
    if (mainTrack == INVALID_TRACK_ID){return false;}
    DTSC::Fragments fragments(M.fragments(mainTrack));
    return fragments.getValidCount() > 6;
  }
  
  bool OutHLS::listenMode(){return !(config->getString("ip").size());}

  OutHLS::OutHLS(Socket::Connection &conn) : TSOutput(conn){
    // load from global config
    systemBoot = Util::getGlobalConfig("systemBoot").asInt();
    // fall back to local calculation if loading from global config fails
    if (!systemBoot){systemBoot = (Util::unixMS() - Util::bootMS());}
    uaDelay = 0;
    realTime = 0;
    targetTime = 0xFFFFFFFFFFFFFFFFull;
    // If this connection is a socket and not already connected to stdio, connect it to stdio.
    if (myConn.getPureSocket() != -1 && myConn.getSocket() != STDIN_FILENO && myConn.getSocket() != STDOUT_FILENO){
      std::string host = getConnectedHost();
      dup2(myConn.getSocket(), STDIN_FILENO);
      dup2(myConn.getSocket(), STDOUT_FILENO);
      myConn.open(STDOUT_FILENO, STDIN_FILENO);
      myConn.setHost(host);
    }
  }

  OutHLS::~OutHLS(){}

  void OutHLS::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa.removeMember("deps");
    capa["optdeps"] = "HTTP";
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
    capa["codecs"][0u][7u].append("+subtitle");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/application/vnd.apple.mpegurl";
    capa["methods"][0u]["hrn"] = "HLS (TS)";
    capa["methods"][0u]["priority"] = 9;
    // MP3 only works on Edge/Apple
    capa["exceptions"]["codec:MP3"] = JSON::fromString(
        "[[\"blacklist\",[\"Mozilla/"
        "\"]],[\"whitelist\",[\"iPad\",\"iPhone\",\"iPod\",\"MacIntel\",\"Edge\"]]]");
    capa["exceptions"]["codec:HEVC"] = JSON::fromString("[[\"blacklist\"]]");

    cfg->addOption(
        "listlimit",
        JSON::fromString(
            "{\"arg\":\"integer\",\"default\":0,\"short\":\"y\",\"long\":\"list-limit\","
            "\"help\":\"Maximum number of segments in live playlists (0 = infinite).\"}"));
    capa["optional"]["listlimit"]["name"] = "Live playlist limit";
    capa["optional"]["listlimit"]["help"] =
        "Maximum number of parts in live playlists. (0 = infinite)";
    capa["optional"]["listlimit"]["default"] = 0;
    capa["optional"]["listlimit"]["type"] = "uint";
    capa["optional"]["listlimit"]["option"] = "--list-limit";
    capa["optional"]["listlimit"]["short"] = "y";

    cfg->addOption("nonchunked",
                   JSON::fromString("{\"short\":\"C\",\"long\":\"nonchunked\",\"help\":\"Do not "
                                    "send chunked, but buffer whole segments.\"}"));
    capa["optional"]["nonchunked"]["name"] = "Send whole segments";
    capa["optional"]["nonchunked"]["help"] =
        "Disables chunked transfer encoding, forcing per-segment buffering. Reduces performance "
        "significantly, but increases compatibility somewhat.";
    capa["optional"]["nonchunked"]["option"] = "--nonchunked";
    capa["optional"]["nonchunked"]["short"] = "C";
    capa["optional"]["nonchunked"]["default"] = false;

    cfg->addOption("mergesessions",
                   JSON::fromString("{\"short\":\"M\",\"long\":\"mergesessions\",\"help\":\"Merge "
                                    "together sessions from one user into a single session.\"}"));
    capa["optional"]["mergesessions"]["name"] = "Merge sessions";
    capa["optional"]["mergesessions"]["help"] =
        "If enabled, merges together all views from a single user into a single combined session. "
        "If disabled, each view (main playlist request) is a separate session.";
    capa["optional"]["mergesessions"]["option"] = "--mergesessions";
    capa["optional"]["mergesessions"]["short"] = "M";
    capa["optional"]["mergesessions"]["default"] = false;

    cfg->addOption("chunkpath",
                   JSON::fromString("{\"arg\":\"string\",\"default\":\"\",\"short\":\"e\",\"long\":"
                                    "\"chunkpath\",\"help\":\"Alternate URL path to "
                                    "prepend to chunk paths, for serving through e.g. a CDN\"}"));
    capa["optional"]["chunkpath"]["name"] = "Prepend path for chunks";
    capa["optional"]["chunkpath"]["help"] =
        "Chunks will be served from this path. This also disables sessions IDs for chunks.";
    capa["optional"]["chunkpath"]["default"] = "";
    capa["optional"]["chunkpath"]["type"] = "str";
    capa["optional"]["chunkpath"]["option"] = "--chunkpath";
    capa["optional"]["chunkpath"]["short"] = "e";
    capa["optional"]["chunkpath"]["default"] = "";
    cfg->addConnectorOptions(8081, capa);
  }

  /******************************/
  /* HLS Manifest Generation */
  /******************************/

  /// \brief Builds master playlist for (LL)HLS.
  ///\return The master playlist file for (LL)HLS.
  void OutHLS::sendHlsMasterManifest(){
    selectDefaultTracks();
    // check for forced "no low latency" parameter
    bool noLLHLS = H.GetVar("llhls").size() ? H.GetVar("llhls") == "0" : false;

    // Populate the struct that will help generate the master playlist
    const HLS::MasterData masterData ={
        false,//hasSessionIDs, unused
        noLLHLS,
        hlsMediaFormat == ".ts",
        getMainSelectedTrack(),
        H.GetHeader("User-Agent"),
        (Comms::tknMode & 0x04)?tkn:"",
        systemBoot,
        bootMsOffset,
    };

    std::stringstream result;
    HLS::addMasterManifest(result, M, userSelect, masterData);

    H.SetBody(result.str());
    H.SendResponse("200", "OK", myConn);
  }

  /// \brief Builds media playlist to (LL)HLS
  ///\return The media playlist file to (LL)HLS
  void OutHLS::sendHlsMediaManifest(const size_t requestTid){
    const HLS::HlsSpecData hlsSpec ={H.GetVar("_HLS_skip"), H.GetVar("_HLS_msn"),
                                      H.GetVar("_HLS_part")};

    size_t timingTid = HLS::getTimingTrackId(M, H.GetVar("mTrack"), getMainSelectedTrack());

    // Chunkpath & Session ID logic
    std::string urlPrefix = "";
    if (config->getString("chunkpath").size()){
      urlPrefix = HTTP::URL(config->getString("chunkpath")).link("./" + H.url).link("./").getUrl();
    }

    // check for forced "no low latency" parameter
    bool noLLHLS = H.GetVar("llhls").size() ? H.GetVar("llhls") == "0" : false;
    // override if valid header forces "no low latency"
    noLLHLS = H.GetHeader("X-Mist-LLHLS").size() ? H.GetHeader("X-Mist-LLHLS") == "0" : noLLHLS;

    const HLS::TrackData trackData ={
        M.getLive(),
        M.getType(requestTid) == "video",
        noLLHLS,
        hlsMediaFormat,
        M.getEncryption(requestTid),
        (Comms::tknMode & 0x04)?tkn:"",
        timingTid,
        requestTid,
        M.biggestFragment(timingTid) / 1000,
        (uint64_t)atol(H.GetVar("iMsn").c_str()),
        (uint64_t)(M.getLive() ? config->getInteger("listlimit") : 0),
        urlPrefix,
        systemBoot,
        bootMsOffset,
    };

    // Fragment & Key handlers
    DTSC::Fragments fragments(M.fragments(trackData.timingTrackId));
    DTSC::Keys keys(M.keys(trackData.timingTrackId));

    uint32_t bprErrCode = HLS::blockPlaylistReload(M, userSelect, trackData, hlsSpec, fragments, keys);
    if (bprErrCode == 400){
      H.SendResponse("400", "Bad Request: Invalid LLHLS parameter", myConn);
      return;
    }else if (bprErrCode == 503){
      H.SendResponse("503", "Service Unavailable", myConn);
      return;
    }

    HLS::FragmentData fragData;
    HLS::populateFragmentData(M, userSelect, fragData, trackData, fragments, keys);

    std::stringstream result;
    HLS::addStartingMetaTags(result, fragData, trackData, hlsSpec);
    HLS::addMediaFragments(result, M, fragData, trackData, fragments, keys);
    HLS::addEndingTags(result, M, userSelect, fragData, trackData);

    H.SetBody(result.str());
    H.SendResponse("200", "OK", myConn);
  }

  void OutHLS::sendHlsManifest(const std::string url){
    H.setCORSHeaders();
    H.SetHeader("Content-Type", "application/vnd.apple.mpegurl"); // for .m3u8
    H.SetHeader("Cache-Control", "no-store");
    if (H.method == "OPTIONS" || H.method == "HEAD"){
      H.SetBody("");
      H.SendResponse("200", "OK", myConn);
      return;
    }

    if (url.find("/") == std::string::npos){
      sendHlsMasterManifest();
    }else{
      if (!M.getValidTracks().count(atoll(url.c_str()))){
        H.SendResponse("400", "Bad Request: Invalid track requested", myConn);
        return;
      }
      sendHlsMediaManifest(atoll(url.c_str()));
    }
  }

  void OutHLS::onHTTP(){
    initialize();
    bootMsOffset = 0;
    if (M.getLive()){bootMsOffset = M.getBootMsOffset();}

    if (tkn.size()){
      if (Comms::tknMode & 0x08){
        const std::string koekjes = H.GetHeader("Cookie");
        std::stringstream cookieHeader;
        cookieHeader << "tkn=" << tkn << "; Max-Age=" << SESS_TIMEOUT;
        H.SetHeader("Set-Cookie", cookieHeader.str()); 
      }
    }

    if (H.url == "/crossdomain.xml"){
      H.SetHeader("Content-Type", "text/xml");
      H.SetHeader("Server", APPIDENT);
      H.setCORSHeaders();
      if (H.method == "OPTIONS" || H.method == "HEAD"){
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
        H.SetHeader("Cache-Control",
                    "public, max-age=" +
                        JSON::Value(M.getDuration(getMainSelectedTrack()) / 1000).asString() +
                        ", immutable");
        H.SetHeader("Pragma", "");
        H.SetHeader("Expires", "");
      }else{
        H.SetHeader("Cache-Control", "no-store");
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
      std::string url = H.url.substr(H.url.find('/', 5) + 1); // Strip /hls/<streamname>/ from url
      const uint64_t msn = atoll(H.GetVar("msn").c_str());
      const uint64_t dur = atoll(H.GetVar("dur").c_str());
      const uint64_t mTrack = atoll(H.GetVar("mTrack").c_str());
      size_t idx = atoll(url.c_str());

      if (url.find(hlsMediaFormat) == std::string::npos){
        H.SendResponse("404", "File not found", myConn);
        return;
      }
      if (!M.getValidTracks().count(idx)){
        H.SendResponse("404", "Track not found", myConn);
        return;
      }

      uint64_t fragmentIndex;
      uint64_t startTime;
      uint32_t part;

      // set targetTime
      if (sscanf(url.c_str(), "%*d/chunk_%" PRIu64 ".%" PRIu32 ".*", &startTime, &part) ==
          2){
        // Logic: calculate targetTime for partial segments
        targetTime = HLS::getPartTargetTime(M, idx, mTrack, startTime, msn, part);
        if (!targetTime){
          H.SendResponse("404", "Partial fragment does not exist", myConn);
          responded = true;
          return;
        }
        startTime += part * HLS::partDurationMaxMs;
        fragmentIndex = M.getFragmentIndexForTime(mTrack, startTime);
        DEBUG_MSG(5, "partial segment requested: %s st %" PRIu64 " et %" PRIu64, url.c_str(),
                  startTime, targetTime);
      }else if (sscanf(url.c_str(), "%*d_%*d/chunk_%" PRIu64 ".%" PRIu32 ".*", &startTime, &part) == 2){
        // Logic: calculate targetTime for partial segments for TS based media with 1 audio track
        targetTime = HLS::getPartTargetTime(M, idx, mTrack, startTime, msn, part);
        if (!targetTime){
          H.SendResponse("404", "Partial fragment does not exist", myConn);
          return;
        }
        startTime += part * HLS::partDurationMaxMs;
        fragmentIndex = M.getFragmentIndexForTime(mTrack, startTime);
        DEBUG_MSG(5, "partial segment requested: %s st %" PRIu64 " et %" PRIu64, url.c_str(),
                  startTime, targetTime);
      }else if (sscanf(url.c_str(), "%*d/chunk_%" PRIu64 ".*", &startTime) == 1){
        // Logic: calculate targetTime for full segments
        if (M.getVod()){startTime += M.getFirstms(idx);}
        fragmentIndex = M.getFragmentIndexForTime(mTrack, startTime);
        targetTime = dur ? startTime + dur : M.getTimeForFragmentIndex(mTrack, fragmentIndex + 1);
        DEBUG_MSG(5, "full segment requested: %s st %" PRIu64 " et %" PRIu64 " asd", url.c_str(),
                  startTime, targetTime);
      }else if (sscanf(url.c_str(), "%*d_%*d/chunk_%" PRIu64 ".*", &startTime) == 1){
        // Logic: calculate targetTime for full segments
        if (M.getVod()){startTime += M.getFirstms(idx);}
        fragmentIndex = M.getFragmentIndexForTime(mTrack, startTime);
        targetTime = dur ? startTime + dur : M.getTimeForFragmentIndex(mTrack, fragmentIndex + 1);
        DEBUG_MSG(5, "full segment requested: %s st %" PRIu64 " et %" PRIu64 " asd", url.c_str(),
                  startTime, targetTime);
      }else{
        WARN_MSG("Undetected media request. Please report to MistServer.");
      }

      std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin();
      std::set<size_t> aTracks;
      for (; it != userSelect.end(); it++){
        if (M.getType(it->first) == "audio"){aTracks.insert(it->first);}
      }
      // Select the right track
      if (aTracks.size() == 1){
        userSelect.clear();
        userSelect[idx].reload(streamName, idx);
        userSelect[*aTracks.begin()].reload(streamName, *aTracks.begin());
      }else{
        userSelect.clear();
        userSelect[idx].reload(streamName, idx);
      }

      std::set<size_t> validTracks = getSupportedTracks();
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
        if (M.getCodec(*it) == "ID3"){userSelect[*it].reload(streamName, *it);}
      }

      if (M.getLive() && startTime < M.getFirstms(idx)){
        H.setCORSHeaders();
        H.SetBody("The requested fragment is no longer kept in memory on the server and cannot be "
                  "served.\n");
        myConn.SendNow(H.BuildResponse("404", "Fragment out of range"));
        WARN_MSG("Fragment @ %" PRIu64 " too old", startTime);
        responded = true;
        return;
      }

      H.SetHeader("Content-Type", "video/mp2t");
      H.setCORSHeaders();
      if (hasSessionIDs() && !config->getOption("chunkpath")){
        H.SetHeader("Cache-Control", "no-store");
      }else{
        H.SetHeader("Cache-Control",
                    "public, max-age=" +
                        JSON::Value(M.getDuration(getMainSelectedTrack()) / 1000).asString() +
                        ", immutable");
        H.SetHeader("Pragma", "");
        H.SetHeader("Expires", "");
      }
      if (H.method == "OPTIONS" || H.method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        responded = true;
        return;
      }

      H.StartResponse(H, myConn, VLCworkaround || config->getBool("nonchunked"));
      responded = true;
      // we assume whole fragments - but timestamps may be altered at will
      contPAT = fragmentIndex; // PAT continuity counter
      contPMT = fragmentIndex; // PMT continuity counter
      contSDT = fragmentIndex; // SDT continuity counter
      packCounter = 0;
      parseData = true;
      wantRequest = false;
      seek(startTime);
      ts_from = startTime;
    }else{
      initialize();

      H.setCORSHeaders();
      H.SetHeader("Content-Type", "application/vnd.apple.mpegurl");
      if (!M.getValidTracks().size()){
        H.SendResponse("404", "Not online or found", myConn);
        return;
      }
      if (H.method == "OPTIONS" || H.method == "HEAD"){
        H.SendResponse("200", "OK", myConn);
        return;
      }

      // Strip /hls/<streamname>/ from url
      std::string url = H.url.substr(H.url.find('/', 5) + 1);
      sendHlsManifest(url);
      responded = true;
    }
  }

  void OutHLS::sendNext(){
    // First check if we need to stop.
    if (thisPacket.getTime() >= targetTime){
      stop();
      wantRequest = true;

      // Ensure alignment of contCounters, to prevent discontinuities.
      for (std::map<size_t, uint16_t>::iterator it = contCounters.begin(); it != contCounters.end();
           it++){
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
