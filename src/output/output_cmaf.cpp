#include "output_cmaf.h"

#include <mist/bitfields.h>
#include <mist/checksum.h>
#include <mist/cmaf.h>
#include <mist/hls_support.h>
#include <mist/mp4_generic.h>

uint64_t systemBoot; // time since boot in ms

uint64_t cmafBoot = Util::bootSecs();
uint64_t dataUp = 0;
uint64_t dataDown = 0;

namespace Mist{
  void CMAFPushTrack::connect(std::string debugParam){
    D.setHeader("Transfer-Encoding", "chunked");
    D.prepareRequest(url, "POST", D.getSocket());

    HTTP::Parser &http = D.getHTTP();
    http.sendingChunks = true;
    http.SendRequest(D.getSocket());

    if (debugParam.length()){
      if (debugParam[debugParam.length() - 1] != '/'){debugParam += '/';}
      debug = true;
      std::string filename = url.getUrl();
      filename.erase(0, filename.rfind("/") + 1);
      snprintf(debugName, 500, "%s%s-%" PRIu64, debugParam.c_str(), filename.c_str(),
               Util::bootMS());
      INFO_MSG("CMAF DEBUG FILE: %s", debugName);
      debugFile = fopen(debugName, "wb");
    }
  }

  void CMAFPushTrack::disconnect(){
    Socket::Connection &sock = D.getSocket();

    MP4::MFRA mfraBox;
    send(mfraBox.asBox(), mfraBox.boxedSize());
    send("");
    sock.close();

    if (debugFile){
      fclose(debugFile);
      debugFile = 0;
    }
  }

  void CMAFPushTrack::send(const char *data, size_t len){
    uint64_t preUp = D.getSocket().dataUp();
    uint64_t preDown = D.getSocket().dataDown();
    D.getHTTP().Chunkify(data, len, D.getSocket());
    if (debug && debugFile){fwrite(data, 1, len, debugFile);}
    dataUp += D.getSocket().dataUp() - preUp;
    dataDown += D.getSocket().dataDown() - preDown;
  }

  void CMAFPushTrack::send(const std::string &data){send(data.data(), data.size());}

  bool OutCMAF::isReadyForPlay(){
    if (!isInitialized){initialize();}
    meta.reloadReplacedPagesIfNeeded();
    if (!M.getValidTracks().size()){return false;}
    uint32_t mainTrack = M.mainTrack();
    if (mainTrack == INVALID_TRACK_ID){return false;}
    DTSC::Fragments fragments(M.fragments(mainTrack));
    return fragments.getValidCount() > 2;
  }

  OutCMAF::OutCMAF(Socket::Connection & conn, Util::Config & _cfg, JSON::Value & _capa)
    : HTTPOutput(conn, _cfg, _capa) {
    // load from global config
    systemBoot = Util::getGlobalConfig("systemBoot").asInt();
    // fall back to local calculation if loading from global config fails
    if (!systemBoot){systemBoot = (Util::unixMS() - Util::bootMS());}

    uaDelay = 0;
    realTime = 0;
    if (config->getString("target").size()){
      needsLookAhead = 5000;

      streamName = config->getString("streamname");
      std::string target = config->getString("target");
      target.replace(0, 4, "http"); // Translate to http for cmaf:// or https for cmafs://
      pushUrl = HTTP::URL(target);

      INFO_MSG("About to push stream %s out. Host: %s, port: %" PRIu32 ", location: %s",
               streamName.c_str(), pushUrl.host.c_str(), pushUrl.getPort(), pushUrl.path.c_str());
      myConn.setHost(pushUrl.host);
      initialize();
      initialSeek();
      startPushOut();
    }else{
      realTime = 0;
    }
  }

  void OutCMAF::connStats(uint64_t now, Comms::Connections &statComm){
    // For non-push usage, call usual function.
    if (!isRecording()){
      Output::connStats(now, statComm);
      return;
    }
    // For push output, this data is not coming from the usual place as we have multiple
    // connections to worry about.
    statComm.setUp(dataUp);
    statComm.setDown(dataDown);
    statComm.setTime(now - cmafBoot);
  }

  // Properly end all tracks on shutdown.
  OutCMAF::~OutCMAF(){
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end();
         it++){
      onTrackEnd(it->first);
    }
  }

  void OutCMAF::init(Util::Config *cfg, JSON::Value & capa) {
    HTTPOutput::init(cfg, capa);
    capa["name"] = "CMAF";
    capa["friendly"] = "CMAF (fMP4) over HTTP (DASH, HLS7, HSS)";
    capa["desc"] = "Segmented streaming in CMAF (fMP4) format over HTTP";
    capa["url_rel"] = "/cmaf/$/";
    capa["url_prefix"] = "/cmaf/$/";
    capa["socket"] = "http_dash_mp4";
    capa["codecs"][0u][0u].append("+H264");
    capa["codecs"][0u][1u].append("+HEVC");
    capa["codecs"][0u][2u].append("+AAC");
    capa["codecs"][0u][3u].append("+AC3");
    capa["codecs"][0u][4u].append("+MP3");
    capa["codecs"][0u][5u].append("+subtitle");
    capa["encryption"].append("CTR128");

    { // DASH playback method
      JSON::Value & M = capa["methods"].append();
      M["hrn"] = "DASH";
      M["handler"] = "http";
      M["type"] = "dash/video/mp4";
      M["ttff"] = "segs";
      M["ttff_segs"] = 3;
      M["latency"] = "frag*1.5";
      M["bw"].fromString("[0, 0, 12, false, 100]");
      M["control"] = 10;
      M["stability"] = 4;
      M["cpu_server"] = 4;
      M["permissibility"] = 10;
      M["url_rel"] = "/cmaf/$/index.mpd";
      M["abr"] = true;
    }
    { // HLS CMAF playback method
      JSON::Value & M = capa["methods"].append();
      M["hrn"] = "HLS (CMAF)";
      M["handler"] = "http";
      M["type"] = "html5/application/vnd.apple.mpegurl;version=7";
      M["ttff"] = "segs";
      M["ttff_segs"] = 3;
      M["latency"] = "frag*1.5";
      M["bw"].fromString("[0, 0, 12, false, 100]");
      M["control"] = 10;
      M["stability"] = 8;
      M["cpu_server"] = 4;
      M["permissibility"] = 10;
      M["url_rel"] = "/cmaf/$/index.m3u8";
      M["abr"] = true;
    }
    { // MSS playback method
      JSON::Value & M = capa["methods"].append();
      M["hrn"] = "MS Smooth Streaming";
      M["handler"] = "http";
      M["type"] = "html5/application/vnd.ms-sstr+xml";
      M["ttff"] = "segs";
      M["ttff_segs"] = 3;
      M["latency"] = "frag*1.5";
      M["bw"].fromString("[0, 0, 12, false, 100]");
      M["control"] = 1;
      M["stability"] = 3;
      M["cpu_server"] = 4;
      M["permissibility"] = 10;
      M["url_rel"] = "/cmaf/$/Manifest";
      M["abr"] = true;
    }

    // MP3 does not work in browsers
    capa["exceptions"]["codec:MP3"] = JSON::fromString("[[\"blacklist\",[\"Mozilla/\"]]]");

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

    cfg->addStandardPushCapabilities(capa);
    capa["push_urls"].append("cmaf://*");
    capa["push_urls"].append("cmafs://*");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target CMAF URL to push out towards.";
    cfg->addOption("target", opt);
  }

  void OutCMAF::respondHTTP(const HTTP::Parser & request, bool headersOnly) {
    // Set global defaults, first
    HTTPOutput::respondHTTP(request, headersOnly);

    if (request.url.find('/', 6) == std::string::npos) {
      H.SendResponse("404", "Stream not found", myConn);
      return;
    }

    // Strip /cmaf/<streamname>/ from url
    std::string url = request.url.substr(request.url.find('/', 6) + 1);
    HTTP::URL req(reqUrl);

    // Send a dash manifest for any URL with .mpd in the path
    if (req.getExt() == "mpd"){
      sendDashManifest();
      responded = true;
      return;
    }

    // Send a hls manifest for any URL with m3u8/m3u extension
    if (req.getExt() == "m3u8" || req.getExt() == "m3u") {
      H.SetHeader("Content-Type", "application/vnd.apple.mpegurl"); // for .m3u8
      H.SetHeader("Cache-Control", "no-store");
      if (headersOnly) {
        H.SendResponse("200", "OK", myConn);
        responded = true;
        return;
      }

      if (url.find("/") == std::string::npos) {
        initialSeek(true);
        HLS::Generator hlsGen;
        if (tkn.size() && (Comms::tknMode & 0x04)) { hlsGen.setParam("tkn", tkn); }
        if (targetParams.count("start")) { hlsGen.setParam("start", targetParams["start"]); }
        if (targetParams.count("stop")) { hlsGen.setParam("stop", targetParams["stop"]); }
        if (targetParams.count("video")) { hlsGen.setParam("video", targetParams["video"]); }
        if (targetParams.count("audio")) { hlsGen.setParam("audio", targetParams["audio"]); }
        if (targetParams.count("subtitle")) { hlsGen.setParam("subtitle", targetParams["subtitle"]); }
        H.StartResponse(request, myConn);
        H.Chunkify(hlsGen.masterPlaylist(M, userSelect, getMainSelectedTrack()), myConn);
        H.Chunkify("", 0, myConn);
        responded = true;
      } else {
        size_t mainTrack = getMainSelectedTrack();
        targetParams["audio"] = "none";
        targetParams["video"] = "none";
        targetParams["subtitle"] = "none";
        while (url.find('/') != std::string::npos) {
          std::string selector = url.substr(0, url.find('/'));
          if (selector.size()) {
            if (selector[0] == 'a') { targetParams["audio"] = selector.substr(1); }
            if (selector[0] == 'v') { targetParams["video"] = selector.substr(1); }
            if (selector[0] == 's') { targetParams["subtitle"] = selector.substr(1); }
          }
          url = url.substr(url.find('/') + 1);
        }
        selectDefaultTracks();
        initialSeek(true);
        HLS::Generator hlsGen;
        hlsGen.setExt("m4s");
        hlsGen.setListLimit(config->getInteger("listlimit"));
        if (tkn.size() && (Comms::tknMode & 0x04)) { hlsGen.setParam("tkn", tkn); }
        H.StartResponse(request, myConn);
        // If there is a start time in the future, apply a limiter that makes an empty playlist
        if (targetParams.count("start")) {
          uint64_t startTime = JSON::Value(targetParams["start"]).asInt();
          if (startTime > M.getLastms(mainTrack)) { meta.applyLimiter(startTime, startTime); }
        }
        H.Chunkify(hlsGen.subPlaylist(M, userSelect, mainTrack), myConn);
        H.Chunkify("", 0, myConn);
        responded = true;
      }
      return;
    }

    // Send a smooth manifest for any URL with .mpd in the path
    if (url.find("Manifest") != std::string::npos){
      sendSmoothManifest();
      responded = true;
      return;
    }

    if (tkn.size() && (Comms::tknMode & 0x04)) {
      H.SetHeader("Cache-Control", "no-store");
    } else {
      H.SetHeader("Cache-Control",
                  "public, max-age=" +
                      JSON::Value(M.getDuration(getMainSelectedTrack()) / 1000).asString() +
                      ", immutable");
      H.SetHeader("Pragma", "");
      H.SetHeader("Expires", "");
    }
    H.setCORSHeaders();
    if (headersOnly) {
      H.SendResponse("200", "OK", myConn);
      responded = true;
      return;
    }

    meta.removeLimiter();
    selectDefaultTracks();
    size_t mainTrack = getMainSelectedTrack();
    targetParams["audio"] = "none";
    targetParams["video"] = "none";
    targetParams["subtitle"] = "none";
    while (url.find('/') != std::string::npos) {
      std::string selector = url.substr(0, url.find('/'));
      if (selector.size()) {
        if (selector[0] == 'a') { targetParams["audio"] = selector.substr(1); }
        if (selector[0] == 'v') { targetParams["video"] = selector.substr(1); }
        if (selector[0] == 's') { targetParams["subtitle"] = selector.substr(1); }
      }
      url = url.substr(url.find('/') + 1);
    }
    selectDefaultTracks();

    if (url.find("init") != std::string::npos) {
      H.StartResponse(request, myConn);
      Util::ResizeablePointer headerData;
      CMAF::header(headerData, M, userSelect);
      H.Chunkify(headerData, headerData.size(), myConn);
      H.Chunkify("", 0, myConn);
      responded = true;
      return;
    }

    uint64_t startTime;
    targetTime = 0;
    if (sscanf(url.c_str(), "%" PRIu64 "_%" PRIu64 ".*", &startTime, &targetTime) != 2) {
      if (sscanf(url.c_str(), "%" PRIu64 ".*", &startTime) != 1) {
        H.SendResponse("400", "Bad Request: Could not parse the url", myConn);
        return;
      }
    }
    size_t fragmentIndex = M.getFragmentIndexForTime(mainTrack, startTime);
    if (!targetTime) {
      DTSC::Fragments fragments(M.fragments(mainTrack));
      targetTime = startTime + fragments.getDuration(fragmentIndex);
    }
    meta.applyLimiter(startTime, targetTime);
    seek(startTime);

    Util::ResizeablePointer headerData;
    CMAF::fragmentHeader(headerData, M, userSelect, fragmentIndex);
    H.StartResponse(request, myConn, config->getBool("nonchunked"));
    H.Chunkify(headerData, headerData.size(), myConn);

    wantRequest = false;
    parseData = true;
    realTime = 0;
    responded = true;
  }

  bool OutCMAF::onFinish() {
    if (!wantRequest) {
      INFO_MSG("Finished playback to %" PRIu64, targetTime);
      wantRequest = true;
      parseData = false;
      H.Chunkify("", 0, myConn);
      H.Clean();
    }
    return true;
  }

  void OutCMAF::sendNext(){
    if (isRecording()){
      pushNext();
      return;
    }
    if (thisPacket.getTime() >= targetTime){
      INFO_MSG("Finished playback to %" PRIu64, targetTime);
      wantRequest = true;
      parseData = false;
      H.Chunkify("", 0, myConn);
      H.Clean();
      return;
    }
    char *data;
    size_t dataLen;
    thisPacket.getString("data", data, dataLen);
    H.Chunkify(data, dataLen, myConn);
  }

  /***************************************************************************************************/
  /* Utility */
  /***************************************************************************************************/

  void OutCMAF::generateSegmentlist(size_t idx, std::stringstream &s,
                                    void dashSegmentCallBack(uint64_t, uint64_t,
                                                             std::stringstream &, bool)){
    // NOTE: Weirdly making the 0th track as the reference track fixed everything.
    // Looks like a nomenclature issue.
    // TODO: Investigate with spec and refactor stuff appropriately.

    size_t mainTrack = M.mainTrack();

    if (mainTrack == INVALID_TRACK_ID){return;}
    DTSC::Fragments fragments(M.fragments(mainTrack));
    uint32_t firstFragment = fragments.getFirstValid();
    uint32_t lastFragment = fragments.getEndValid();
    bool first = true;
    // skip the first two fragments if live
    if (M.getLive() && (lastFragment - firstFragment) > 6){firstFragment += 2;}

    DTSC::Keys keys(M.getKeys(mainTrack));
    for (; firstFragment < lastFragment; ++firstFragment){
      uint32_t duration = fragments.getDuration(firstFragment);
      uint64_t starttime = keys.getTime(fragments.getFirstKey(firstFragment));
      if (!duration){
        if (M.getLive()){continue;}// skip last fragment when live
        duration = M.getLastms(mainTrack) - starttime;
      }
      if (M.getVod()){starttime -= M.getFirstms(mainTrack);}
      dashSegmentCallBack(starttime, duration, s, first);
      first = false;
    }

    /*LTS-START
    // remove lines to reduce size towards listlimit setting - but keep at least 4X target
    // duration available
    uint64_t listlimit = config->getInteger("listlimit");
    if (listlimit){
      while (lines.size() > listlimit &&
             (totalDuration - durations.front()) > (targetDuration * 4000)){
        lines.pop_front();
        totalDuration -= durations.front();
        durations.pop_front();
        ++skippedLines;
      }
    }
    LTS-END*/
  }

  std::string OutCMAF::buildNalUnit(size_t len, const char *data){
    char *res = (char *)malloc(len + 4);
    Bit::htobl(res, len);
    memcpy(res + 4, data, len);
    return std::string(res, len + 4);
  }

  std::string OutCMAF::h264init(const std::string &initData){
    char res[7];
    snprintf(res, 7, "%.2X%.2X%.2X", initData[1], initData[2], initData[3]);
    return res;
  }

  std::string OutCMAF::h265init(const std::string &initData){
    char res[17];
    snprintf(res, 17, "%.2X%.2X%.2X%.2X%.2X%.2X%.2X%.2X", initData[1], initData[6], initData[7],
             initData[8], initData[9], initData[10], initData[11], initData[12]);
    return res;
  }

  /*********************************/
  /* MPEG-DASH Manifest Generation */
  /*********************************/

  void OutCMAF::sendDashManifest(){
    std::string method = H.method;
    H.Clean();
    H.SetHeader("Content-Type", "application/dash+xml");
    H.SetHeader("Cache-Control", "no-store");
    H.setCORSHeaders();
    if (method == "OPTIONS" || method == "HEAD"){
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    H.SetBody(dashManifest());
    H.SendResponse("200", "OK", myConn);
    H.Clean();
  }

  void dashSegment(uint64_t start, uint64_t duration, std::stringstream &s, bool first){
    s << "<S ";
    if (first){s << "t=\"" << start << "\" ";}
    s << "d=\"" << duration << "\" />" << std::endl;
  }

  std::string OutCMAF::dashTime(uint64_t time){
    std::stringstream r;
    r << "PT";
    if (time >= 3600000){r << (time / 3600000) << "H";}
    if (time >= 60000){r << (time / 60000) % 60 << "M";}
    r << (time / 1000) % 60 << "." << std::setfill('0') << std::setw(3) << (time % 1000) << "S";
    return r.str();
  }

  void OutCMAF::dashAdaptationSet(size_t id, size_t idx, std::stringstream &r){
    std::string type = M.getType(idx);
    r << "<AdaptationSet group=\"" << id << "\" mimeType=\"" << type << "/mp4\" ";
    if (type == "video"){
      r << "width=\"" << M.getWidth(idx) << "\" height=\"" << M.getHeight(idx) << "\" frameRate=\""
        << M.getFpks(idx) / 1000 << "\" ";
    }
    r << "segmentAlignment=\"true\" id=\"" << type[0] << idx
      << "\" startWithSAP=\"1\" subsegmentAlignment=\"true\" subsegmentStartsWithSAP=\"1\">" << std::endl;
  }

  void OutCMAF::dashRepresentation(size_t id, size_t idx, std::stringstream &r){
    std::string codec = M.getCodec(idx);
    std::string type = M.getType(idx);
    r << "<Representation id=\"" << type[0] << idx << "\" bandwidth=\"" << M.getBps(idx) * 8 << "\" codecs=\"";
    r << Util::codecString(M.getCodec(idx), M.getInit(idx));
    r << "\" ";
    if (type == "audio"){
      r << "audioSamplingRate=\"" << M.getRate(idx)
        << "\"> <AudioChannelConfiguration "
           "schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\""
        << M.getChannels(idx) << "\" /></Representation>" << std::endl;
    }else{
      r << "/>";
    }
  }

  void OutCMAF::dashSegmentTemplate(std::stringstream &r){
    r << "<SegmentTemplate timescale=\"1000"
         "\" media=\"$RepresentationID$/$Time$.m4s\" "
         "initialization=\"$RepresentationID$/init.m4s\"><SegmentTimeline>"
      << std::endl;
  }

  void OutCMAF::dashAdaptation(size_t id, std::set<size_t> tracks, bool aligned,
                               std::stringstream &r){
    if (!tracks.size()){return;}
    if (aligned){
      size_t firstTrack = *tracks.begin();
      dashAdaptationSet(id, *tracks.begin(), r);
      dashSegmentTemplate(r);
      generateSegmentlist(firstTrack, r, dashSegment);
      r << "</SegmentTimeline></SegmentTemplate>" << std::endl;
      for (std::set<size_t>::iterator it = tracks.begin(); it != tracks.end(); it++){
        dashRepresentation(id, *it, r);
      }
      r << "</AdaptationSet>" << std::endl;
      return;
    }
    for (std::set<size_t>::iterator it = tracks.begin(); it != tracks.end(); it++){
      std::string codec = M.getCodec(*it);
      std::string type = M.getType(*it);
      dashAdaptationSet(id, *tracks.begin(), r);
      dashSegmentTemplate(r);
      generateSegmentlist(*it, r, dashSegment);
      r << "</SegmentTimeline></SegmentTemplate>" << std::endl;
      dashRepresentation(id, *it, r);
      r << "</AdaptationSet>" << std::endl;
    }
  }

  /// Returns a string with the full XML DASH manifest MPD file.
  std::string OutCMAF::dashManifest(bool checkAlignment){
    initialize();
    selectDefaultTracks();
    std::set<size_t> vTracks;
    std::set<size_t> aTracks;
    std::set<size_t> sTracks;
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end();
         it++){
      if (M.getType(it->first) == "video"){vTracks.insert(it->first);}
      if (M.getType(it->first) == "audio"){aTracks.insert(it->first);}
      if (M.getCodec(it->first) == "subtitle"){sTracks.insert(it->first);}
    }

    if (!vTracks.size() && !aTracks.size()){return "";}

    bool videoAligned = !checkAlignment;
    bool audioAligned = !checkAlignment;

    std::stringstream r;
    r << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << std::endl;
    r << "<MPD ";
    size_t mainTrack = getMainSelectedTrack();
    size_t mainDuration = M.getDuration(mainTrack);
    if (M.getVod()){
      r << "type=\"static\" mediaPresentationDuration=\"" << dashTime(mainDuration)
        << "\" minBufferTime=\"PT1.5S\" ";
    }else{
      r << "type=\"dynamic\" minimumUpdatePeriod=\"PT2.0S\" availabilityStartTime=\""
        << Util::getUTCString(Util::epoch() - M.getLastms(mainTrack) / 1000)
        << "\" timeShiftBufferDepth=\"" << dashTime(mainDuration)
        << "\" suggestedPresentationDelay=\"PT5.0S\" minBufferTime=\"PT2.0S\" publishTime=\""
        << Util::getUTCString(Util::epoch()) << "\" ";
    }

    r << "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" ";
    r << "xmlns:xlink=\"http://www.w3.org/1999/xlink\" ";
    r << "xsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 "
         "http://standards.iso.org/ittf/PubliclyAvailableStandards/MPEG-DASH_schema_files/"
         "DASH-MPD.xsd\" ";
    r << "profiles=\"urn:mpeg:dash:profile:isoff-live:2011\" "
         "xmlns=\"urn:mpeg:dash:schema:mpd:2011\" >"
      << std::endl;
    r << "<ProgramInformation><Title>" << streamName << "</Title></ProgramInformation>"
      << std::endl;
    r << "<Period " << (M.getLive() ? "start=\"PT0.0S\"" : "") << ">" << std::endl;

    dashAdaptation(1, vTracks, videoAligned, r);
    dashAdaptation(2, aTracks, audioAligned, r);

    if (sTracks.size()){
      for (std::set<size_t>::iterator it = sTracks.begin(); it != sTracks.end(); it++){
        std::string lang = (M.getLang(*it) == "" ? "unknown" : M.getLang(*it));
        r << "<AdaptationSet id=\"" << *it << "\" group=\"3\" mimeType=\"text/vtt\" lang=\"" << lang
          << "\"><Representation id=\"" << *it << "\" bandwidth=\"256\"><BaseURL>../../"
          << streamName << ".vtt?track=" << *it << "</BaseURL></Representation></AdaptationSet>"
          << std::endl;
      }
    }

    r << "</Period></MPD>" << std::endl;

    return r.str();
  }

  /****************************************/
  /* Smooth Streaming Manifest Generation */
  /****************************************/

  std::string toUTF16(const std::string &original){
    std::string result;
    result.append("\377\376", 2);
    for (std::string::const_iterator it = original.begin(); it != original.end(); it++){
      result += (*it);
      result.append("\000", 1);
    }
    return result;
  }

  /// Converts bytes per second and track ID into a single bits per second value, where the last
  /// two digits are the track ID. Breaks for track IDs > 99. But really, this is MS-SS, so who
  /// cares..?
  uint64_t bpsAndIdToBitrate(uint32_t bps, uint64_t tid){
    return ((uint64_t)((bps * 8) / 100)) * 100 + tid;
  }

  void smoothSegment(uint64_t start, uint64_t duration, std::stringstream &s, bool first){
    s << "<c ";
    if (first){s << "t=\"" << start << "\" ";}
    s << "d=\"" << duration << "\" />" << std::endl;
  }

  void OutCMAF::sendSmoothManifest(){
    std::string method = H.method;
    H.Clean();
    H.SetHeader("Content-Type", "application/dash+xml");
    H.SetHeader("Cache-Control", "no-store");
    H.setCORSHeaders();
    if (method == "OPTIONS" || method == "HEAD"){
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    H.SetBody(smoothManifest());
    H.SendResponse("200", "OK", myConn);
    H.Clean();
  }

  void OutCMAF::smoothAdaptation(const std::string &type, std::set<size_t> tracks,
                                 std::stringstream &r){
    if (!tracks.size()){return;}
    DTSC::Keys keys(M.getKeys(*tracks.begin()));
    r << "<StreamIndex Type=\"" << type << "\" QualityLevels=\"" << tracks.size() << "\" Name=\""
      << type << "\" Chunks=\"" << keys.getValidCount() << "\" Url=\"Q({bitrate})/"
      << "chunk_{start_time}.m4s\" ";
    if (type == "video"){
      size_t maxWidth = 0;
      size_t maxHeight = 0;

      for (std::set<size_t>::iterator it = tracks.begin(); it != tracks.end(); it++){
        size_t width = M.getWidth(*it);
        size_t height = M.getHeight(*it);
        if (width > maxWidth){maxWidth = width;}
        if (height > maxHeight){maxHeight = height;}
      }
      r << "MaxWidth=\"" << maxWidth << "\" MaxHeight=\"" << maxHeight << "\" DisplayWidth=\""
        << maxWidth << "\" DisplayHeight=\"" << maxHeight << "\"";
    }
    r << ">\n";
    size_t index = 0;
    for (std::set<size_t>::iterator it = tracks.begin(); it != tracks.end(); it++){
      r << "<QualityLevel Index=\"" << index++ << "\" Bitrate=\""
        << bpsAndIdToBitrate(M.getBps(*it) * 8, *it) << "\" CodecPrivateData=\"" << std::hex;
      if (type == "audio"){
        std::string init = M.getInit(*it);
        for (unsigned int i = 0; i < init.size(); i++){
          r << std::setfill('0') << std::setw(2) << std::right << (int)init[i];
        }
        r << std::dec << "\" SamplingRate=\"" << M.getRate(*it)
          << "\" Channels=\"2\" BitsPerSample=\"16\" PacketSize=\"4\" AudioTag=\"255\" "
             "FourCC=\"AACL\" />\n";
      }
      if (type == "video"){
        MP4::AVCC avccbox;
        avccbox.setPayload(M.getInit(*it));
        std::string tmpString = avccbox.asAnnexB();
        for (size_t i = 0; i < tmpString.size(); i++){
          r << std::setfill('0') << std::setw(2) << std::right << (int)tmpString[i];
        }
        r << std::dec << "\" MaxWidth=\"" << M.getWidth(*it) << "\" MaxHeight=\""
          << M.getHeight(*it) << "\" FourCC=\"AVC1\" />\n";
      }
    }
    generateSegmentlist(*tracks.begin(), r, smoothSegment);
    r << "</StreamIndex>\n";
  }

  /// Returns a string with the full XML DASH manifest MPD file.
  std::string OutCMAF::smoothManifest(bool checkAlignment){
    initialize();

    std::stringstream r;
    r << "<?xml version=\"1.0\" encoding=\"utf-16\"?>\n"
         "<SmoothStreamingMedia MajorVersion=\"2\" MinorVersion=\"0\" TimeScale=\"1000\" ";

    selectDefaultTracks();
    std::set<size_t> vTracks;
    std::set<size_t> aTracks;
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end();
         it++){
      if (M.getType(it->first) == "video"){vTracks.insert(it->first);}
      if (M.getType(it->first) == "audio"){aTracks.insert(it->first);}
    }

    if (!aTracks.size() && !vTracks.size()){
      FAIL_MSG("No valid tracks found");
      return "";
    }

    if (M.getVod()){
      r << "Duration=\"" << M.getLastms(vTracks.size() ? *vTracks.begin() : *aTracks.begin())
        << "\">\n";
    }else{
      r << "Duration=\"0\" IsLive=\"TRUE\" LookAheadFragmentCount=\"2\" DVRWindowLength=\""
        << M.getBufferWindow() << "\" CanSeek=\"TRUE\" CanPause=\"TRUE\">\n";
    }

    smoothAdaptation("audio", aTracks, r);
    smoothAdaptation("video", vTracks, r);
    r << "</SmoothStreamingMedia>\n";

    return toUTF16(r.str());
  }

  /**********************************/
  /* CMAF Push Output functionality */
  /**********************************/

  // When we disconnect a track, or when we're done pushing out, send an empty 'mfra' box to
  // indicate track end.
  void OutCMAF::onTrackEnd(size_t idx){
    if (!isRecording()){return;}
    if (!pushTracks.count(idx) || !pushTracks.at(idx).D.getSocket()){return;}
    INFO_MSG("Disconnecting track %zu", idx);
    pushTracks[idx].disconnect();
    pushTracks.erase(idx);
  }

  // Create the connections and post request needed to start pushing out a track.
  void OutCMAF::setupTrackObject(size_t idx){
    CMAFPushTrack &track = pushTracks[idx];
    track.url = pushUrl;
    if (targetParams.count("usp") && targetParams["usp"] == "1"){
      std::string usp_path = "Streams(" + M.getTrackIdentifier(idx) + ")";
      track.url = track.url.link(usp_path);
    }else{
      track.url.path += "/";
      track.url = track.url.link(M.getTrackIdentifier(idx));
    }

    track.connect(targetParams["debug"]);

    std::string header = CMAF::trackHeader(M, idx, true);
    track.send(header);
  }

  /// Function that waits at most `maxWait` ms (in steps of 100ms) for the next keyframe to become
  /// available. Uses thisIdx and thisPacket to determine track and current timestamp
  /// respectively.
  bool OutCMAF::waitForNextKey(uint64_t maxWait){
    uint64_t mTrk = getMainSelectedTrack();
    size_t currentKey = M.getKeyIndexForTime(mTrk, thisTime);
    uint64_t startTime = Util::bootMS();
    DTSC::Keys keys(M.getKeys(mTrk));
    while (startTime + maxWait > Util::bootMS() && keepGoing()){
      if (keys.getEndValid() > currentKey + 1 &&
          M.getLastms(thisIdx) >= M.getTimeForKeyIndex(mTrk, currentKey + 1)){
        return true;
      }
      Util::sleep(20);
      meta.reloadReplacedPagesIfNeeded();
    }
    INFO_MSG("Timed out waiting for next key (track %" PRIu64
             ", %zu+1, last is %zu, time is %" PRIu64 ")",
             mTrk, currentKey, keys.getEndValid() - 1,
             M.getTimeForKeyIndex(getMainSelectedTrack(), currentKey + 1));
    return (keys.getEndValid() > currentKey + 1 &&
            M.getLastms(thisIdx) >= M.getTimeForKeyIndex(mTrk, currentKey + 1));
  }

  // Set up an empty connection to the target to make sure we can push data towards it.
  void OutCMAF::startPushOut(){
    myConn.close();
    myConn.Received().clear();
    myConn.open(pushUrl.host, pushUrl.getPort(), true);
    wantRequest = false;
    parseData = true;
  }

  // CMAF Push output uses keyframe boundaries instead of fragment boundaries, to allow for lower
  // latency
  void OutCMAF::pushNext(){
    size_t mTrk = getMainSelectedTrack();
    // Set up a new connection if this is a new track, or if we have been disconnected.
    if (!pushTracks.count(thisIdx) || !pushTracks.at(thisIdx).D.getSocket()){
      if (pushTracks.count(thisIdx)){
        INFO_MSG("Reconnecting existing track: socket was disconnected");
      }
      CMAFPushTrack &track = pushTracks[thisIdx];
      size_t keyIndex = M.getKeyIndexForTime(mTrk, thisPacket.getTime());
      track.headerFrom = M.getTimeForKeyIndex(mTrk, keyIndex);
      if (track.headerFrom < thisPacket.getTime()){
        track.headerFrom = M.getTimeForKeyIndex(mTrk, keyIndex + 1);
      }

      INFO_MSG("Starting track %zu at %" PRIu64 "ms into the stream, current packet at %" PRIu64
               "ms",
               thisIdx, track.headerFrom, thisPacket.getTime());

      setupTrackObject(thisIdx);
      track.headerUntil = 0;
    }
    CMAFPushTrack &track = pushTracks[thisIdx];
    if (thisPacket.getTime() < track.headerFrom){return;}
    if (thisPacket.getTime() >= track.headerUntil){
      size_t keyIndex = M.getKeyIndexForTime(mTrk, thisTime);
      uint64_t keyTime = M.getTimeForKeyIndex(mTrk, keyIndex);
      if (keyTime > thisTime){
        realTime = 1000;
        if (!liveSeek()){
          WARN_MSG("Corruption probably occurred, initiating reconnect. Key %zu is time %" PRIu64
                   ", but packet is time %" PRIu64,
                   keyIndex, keyTime, thisTime);
          onTrackEnd(thisIdx);
          track.headerFrom = M.getTimeForKeyIndex(mTrk, keyIndex + 1);
          track.headerUntil = 0;
          pushNext();
        }
        realTime = 0;
        return;
      }
      track.headerFrom = keyTime;
      if (!waitForNextKey()){
        onTrackEnd(thisIdx);
        dropTrack(thisIdx, "No next keyframe available");
        return;
      }
      track.headerUntil = M.getTimeForKeyIndex(mTrk, keyIndex + 1);
      std::string keyHeader = CMAF::keyHeader(M, thisIdx, track.headerFrom, track.headerUntil,
                                              keyIndex + 1, true, true);
      uint64_t mdatSize = 8 + CMAF::payloadSize(M, thisIdx, track.headerFrom, track.headerUntil);
      char mdatHeader[] ={0x00, 0x00, 0x00, 0x00, 'm', 'd', 'a', 't'};
      Bit::htobl(mdatHeader, mdatSize);

      track.send(keyHeader);
      track.send(mdatHeader, 8);
    }
    char *data;
    size_t dataLen;
    thisPacket.getString("data", data, dataLen);

    track.send(data, dataLen);
  }

}// namespace Mist
