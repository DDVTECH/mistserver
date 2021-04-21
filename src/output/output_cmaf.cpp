#include "output_cmaf.h"
#include <iomanip>
#include <mist/bitfields.h>
#include <mist/checksum.h>
#include <mist/cmaf.h>
#include <mist/defines.h>
#include <mist/encode.h>
#include <mist/langcodes.h> /*LTS*/
#include <mist/mp4.h>
#include <mist/mp4_dash.h>
#include <mist/mp4_encryption.h>
#include <mist/mp4_generic.h>
#include <mist/stream.h>
#include <mist/timing.h>

uint64_t bootMsOffset;

namespace Mist{

  OutCMAF::OutCMAF(Socket::Connection &conn) : HTTPOutput(conn){
    uaDelay = 0;
    realTime = 0;
  }

  OutCMAF::~OutCMAF(){}

  void OutCMAF::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
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

    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "dash/video/mp4";
    capa["methods"][0u]["url_rel"] = "/cmaf/$/index.mpd";
    capa["methods"][0u]["priority"] = 8;

    capa["methods"][1u]["handler"] = "http";
    capa["methods"][1u]["type"] = "html5/application/vnd.apple.mpegurl;version=7";
    capa["methods"][1u]["url_rel"] = "/cmaf/$/index.m3u8";
    capa["methods"][1u]["priority"] = 8;

    capa["methods"][2u]["handler"] = "http";
    capa["methods"][2u]["type"] = "html5/application/vnd.ms-sstr+xml";
    capa["methods"][2u]["url_rel"] = "/cmaf/$/Manifest";
    capa["methods"][2u]["priority"] = 8;

    // MP3 does not work in browsers
    capa["exceptions"]["codec:MP3"] = JSON::fromString("[[\"blacklist\",[\"Mozilla/\"]]]");

    cfg->addOption("nonchunked",
                   JSON::fromString("{\"short\":\"C\",\"long\":\"nonchunked\",\"help\":\"Do not "
                                    "send chunked, but buffer whole segments.\"}"));
    capa["optional"]["nonchunked"]["name"] = "Send whole segments";
    capa["optional"]["nonchunked"]["help"] =
        "Disables chunked transfer encoding, forcing per-segment buffering. Reduces performance "
        "significantly, but increases compatibility somewhat.";
    capa["optional"]["nonchunked"]["option"] = "--nonchunked";
  }

  void OutCMAF::onHTTP(){
    initialize();
    bootMsOffset = 0;
    if (M.getLive()){bootMsOffset = M.getBootMsOffset();}

    if (H.url.size() < streamName.length() + 7){
      H.Clean();
      H.SendResponse("404", "Stream not found", myConn);
      H.Clean();
      return;
    }

    std::string method = H.method;
    std::string url = H.url.substr(streamName.length() + 7); // Strip /cmaf/<streamname>/ from url

    // Send a dash manifest for any URL with .mpd in the path
    if (url.find(".mpd") != std::string::npos){
      sendDashManifest();
      return;
    }

    // Send a hls manifest for any URL with index.m3u8 in the path
    if (url.find("index.m3u8") != std::string::npos){
      size_t loc = url.find("index.m3u8");
      if (loc == 0){
        sendHlsManifest();
        return;
      }
      size_t idx = atoll(url.c_str());
      if (url.find("?") == std::string::npos){
        sendHlsManifest(idx);
        return;
      }
      return;
    }

    // Send a smooth manifest for any URL with .mpd in the path
    if (url.find("Manifest") != std::string::npos){
      sendSmoothManifest();
      return;
    }

    H.Clean();
    H.SetHeader("Content-Type", "video/mp4");
    H.SetHeader("Cache-Control", "no-cache");
    H.setCORSHeaders();
    if (method == "OPTIONS" || method == "HEAD"){
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }

    size_t idx = atoll(url.c_str());
    if (url.find("Q(") != std::string::npos){
      idx = atoll(url.c_str() + url.find("Q(") + 2) % 100;
    }
    if (!M.getValidTracks().count(idx)){
      H.Clean();
      H.SendResponse("404", "Track not found", myConn);
      H.Clean();
      return;
    }

    if (url.find(".m4s") == std::string::npos){
      H.Clean();
      H.SendResponse("404", "File not found", myConn);
      H.Clean();
      return;
    }

    // Select the right track
    userSelect.clear();
    userSelect[idx].reload(streamName, idx);

    H.StartResponse(H, myConn, config->getBool("nonchunked"));

    if (url.find("init.m4s") != std::string::npos){
      std::string headerData = CMAF::trackHeader(M, idx);
      H.Chunkify(headerData.c_str(), headerData.size(), myConn);
      H.Chunkify("", 0, myConn);
      H.Clean();
      return;
    }

    uint64_t startTime = atoll(url.c_str() + url.find("/chunk_") + 7);
    if (M.getVod()){startTime += M.getFirstms(idx);}
    uint64_t fragmentIndex = M.getFragmentIndexForTime(idx, startTime);
    targetTime = M.getTimeForFragmentIndex(idx, fragmentIndex + 1);

    std::string headerData = CMAF::fragmentHeader(M, idx, fragmentIndex);
    H.Chunkify(headerData.c_str(), headerData.size(), myConn);

    uint64_t mdatSize = 8 + CMAF::payloadSize(M, idx, fragmentIndex);
    char mdatHeader[] ={0x00, 0x00, 0x00, 0x00, 'm', 'd', 'a', 't'};
    Bit::htobl(mdatHeader, mdatSize);

    H.Chunkify(mdatHeader, 8, myConn);

    seek(startTime);

    wantRequest = false;
    parseData = true;
  }

  void OutCMAF::sendNext(){
    if (thisPacket.getTime() >= targetTime){
      HIGH_MSG("Finished playback to %" PRIu64, targetTime);
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

  bool OutCMAF::tracksAligned(const std::set<size_t> &trackList){
    if (trackList.size() <= 1){return true;}

    size_t baseTrack = *trackList.begin();
    for (std::set<size_t>::iterator it = trackList.begin(); it != trackList.end(); ++it){
      if (*it == baseTrack){continue;}
      if (!M.tracksAlign(*it, baseTrack)){return false;}
    }
    return true;
  }

  void OutCMAF::generateSegmentlist(size_t idx, std::stringstream &s,
                                    void callBack(uint64_t, uint64_t, std::stringstream &, bool)){
    DTSC::Fragments fragments(M.fragments(idx));
    uint32_t firstFragment = fragments.getFirstValid();
    uint32_t endFragment = fragments.getEndValid();
    bool first = true;
    // skip the first two fragments if live
    if (M.getLive() && (endFragment - firstFragment) > 6){firstFragment += 2;}

    if (M.getType(idx) == "audio"){
      uint32_t mainTrack = M.mainTrack();
      if (mainTrack == INVALID_TRACK_ID){return;}
      DTSC::Fragments f(M.fragments(mainTrack));
      uint64_t firstVidTime = M.getTimeForFragmentIndex(mainTrack, f.getFirstValid());
      firstFragment = M.getFragmentIndexForTime(idx, firstVidTime);
    }

    DTSC::Keys keys(M.keys(idx));
    for (; firstFragment < endFragment; ++firstFragment){
      uint32_t duration = fragments.getDuration(firstFragment);
      uint64_t starttime = keys.getTime(fragments.getFirstKey(firstFragment));
      if (!duration){
        if (M.getLive()){continue;}// skip last fragment when live
        duration = M.getLastms(idx) - starttime;
      }
      if (M.getVod()){starttime -= M.getFirstms(idx);}
      callBack(starttime, duration, s, first);
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
    H.SetHeader("Cache-Control", "no-cache");
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
    r << "segmentAlignment=\"true\" id=\"" << idx
      << "\" startWithSAP=\"1\" subsegmentAlignment=\"true\" subsegmentStartsWithSAP=\"1\">" << std::endl;
  }

  void OutCMAF::dashRepresentation(size_t id, size_t idx, std::stringstream &r){
    std::string codec = M.getCodec(idx);
    std::string type = M.getType(idx);
    r << "<Representation id=\"" << idx << "\" bandwidth=\"" << M.getBps(idx) * 8 << "\" codecs=\"";
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
         "\" media=\"$RepresentationID$/chunk_$Time$.m4s\" "
         "initialization=\"$RepresentationID$/init.m4s\"><SegmentTimeline>"
      << std::endl;
  }

  void OutCMAF::dashAdaptation(size_t id, std::set<size_t> tracks, bool aligned, std::stringstream &r){
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
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      if (M.getType(it->first) == "video"){vTracks.insert(it->first);}
      if (M.getType(it->first) == "audio"){aTracks.insert(it->first);}
      if (M.getType(it->first) == "subtitle"){sTracks.insert(it->first);}
    }

    if (!vTracks.size() && !aTracks.size()){return "";}

    bool videoAligned = checkAlignment && tracksAligned(vTracks);
    bool audioAligned = checkAlignment && tracksAligned(aTracks);

    std::stringstream r;
    r << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << std::endl;
    r << "<MPD ";
    size_t mainTrack = getMainSelectedTrack();
    size_t mainDuration = M.getDuration(mainTrack);
    if (M.getVod()){
      r << "type=\"static\" mediaPresentationDuration=\"" << dashTime(mainDuration) << "\" minBufferTime=\"PT1.5S\" ";
    }else{
      r << "type=\"dynamic\" minimumUpdatePeriod=\"PT2.0S\" availabilityStartTime=\""
        << Util::getUTCString(Util::epoch() - M.getLastms(mainTrack) / 1000)
        << "\" timeShiftBufferDepth=\"" << dashTime(mainDuration)
        << "\" suggestedPresentationDelay=\"PT5.0S\" minBufferTime=\"PT2.0S\" publishTime=\""
        << Util::getUTCString(Util::epoch()) << "\" ";
    }
    r << "profiles=\"urn:mpeg:dash:profile:isoff-live:2011\" "
         "xmlns=\"urn:mpeg:dash:schema:mpd:2011\" >"
      << std::endl;
    r << "<ProgramInformation><Title>" << streamName << "</Title></ProgramInformation>" << std::endl;
    r << "<Period " << (M.getLive() ? "start=\"0\"" : "") << ">" << std::endl;

    dashAdaptation(1, vTracks, videoAligned, r);
    dashAdaptation(2, aTracks, audioAligned, r);

    if (sTracks.size()){
      for (std::set<size_t>::iterator it = sTracks.begin(); it != sTracks.end(); it++){
        std::string lang = (M.getLang(*it) == "" ? "unknown" : M.getLang(*it));
        r << "<AdaptationSet id=\"" << *it << "\" group=\"3\" mimeType=\"text/vtt\" lang=\"" << lang
          << "\"><Representation id=\"" << *it << "\" bandwidth=\"256\"><BaseURL>../../" << streamName
          << ".vtt?track=" << *it << "</BaseURL></Representation></AdaptationSet>" << std::endl;
      }
    }

    r << "</Period></MPD>" << std::endl;

    return r.str();
  }

  /******************************/
  /* HLS v7 Manifest Generation */
  /******************************/

  void OutCMAF::sendHlsManifest(size_t idx, const std::string &sessId){
    std::string method = H.method;
    H.Clean();
    //    H.SetHeader("Content-Type", "application/vnd.apple.mpegurl");
    H.SetHeader("Content-Type", "audio/mpegurl");
    H.SetHeader("Cache-Control", "no-cache");
    H.setCORSHeaders();
    if (method == "OPTIONS" || method == "HEAD"){
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    if (idx == INVALID_TRACK_ID){
      H.SetBody(hlsManifest());
    }else{
      H.SetBody(hlsManifest(idx, sessId));
    }
    H.SendResponse("200", "OK", myConn);
    H.Clean();
  }

  void hlsSegment(uint64_t start, uint64_t duration, std::stringstream &s, bool first){
    if (bootMsOffset){
      uint64_t unixMs = start + bootMsOffset + (Util::unixMS() - Util::bootMS());
      time_t uSecs = unixMs/1000;
      struct tm * tVal = gmtime(&uSecs);
      s << "#EXT-X-PROGRAM-DATE-TIME: " << (tVal->tm_year+1900) << "-" << std::setw(2) << std::setfill('0') << (tVal->tm_mon+1) << "-" << std::setw(2) << std::setfill('0') << tVal->tm_mday << "T" << std::setw(2) << std::setfill('0') << tVal->tm_hour << ":" << std::setw(2) << std::setfill('0') << tVal->tm_min << ":" << std::setw(2) << std::setfill('0') << tVal->tm_sec << "." << std::setw(3) << std::setfill('0') << (unixMs%1000) << "Z" << std::endl;
    }
    s << "#EXTINF:" << (((double)duration) / 1000) << ",\r\nchunk_" << start << ".m4s" << std::endl;
  }

  ///\brief Builds an index file for HTTP Live streaming.
  ///\return The index file for HTTP Live Streaming.
  std::string OutCMAF::hlsManifest(){
    std::stringstream result;
    result << "#EXTM3U\r\n#EXT-X-VERSION:7\r\n#EXT-X-INDEPENDENT-SEGMENTS\r\n";

    selectDefaultTracks();
    std::set<size_t> vTracks;
    std::set<size_t> aTracks;
    std::set<size_t> sTracks;
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      if (M.getType(it->first) == "video"){vTracks.insert(it->first);}
      if (M.getType(it->first) == "audio"){aTracks.insert(it->first);}
      if (M.getType(it->first) == "subtitle"){sTracks.insert(it->first);}
    }
    for (std::set<size_t>::iterator it = vTracks.begin(); it != vTracks.end(); it++){
      std::string codec = M.getCodec(*it);
      if (codec == "H264" || codec == "HEVC" || codec == "MPEG2"){
        int bWidth = M.getBps(*it);
        if (bWidth < 5){bWidth = 5;}
        if (aTracks.size()){bWidth += M.getBps(*aTracks.begin());}
        result << "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" << (bWidth * 8)
               << ",RESOLUTION=" << M.getWidth(*it) << "x" << M.getHeight(*it);
        if (M.getFpks(*it)){result << ",FRAME-RATE=" << (float)M.getFpks(*it) / 1000;}
        if (aTracks.size()){result << ",AUDIO=\"aud1\"";}
        if (sTracks.size()){result << ",SUBTITLES=\"sub1\"";}
        if (codec == "H264" || codec == "HEVC"){
          result << ",CODECS=\"";
          result << Util::codecString(M.getCodec(*it), M.getInit(*it));
          result << "\"";
        }
        result << "\r\n" << *it;
        if (hasSessionIDs()){
          result << "/index.m3u8?sessId=" << getpid() << "\r\n";
        }else{
          result << "/index.m3u8\r\n";
        }
      }else if (codec == "subtitle"){

        if (M.getLang(*it).empty()){meta.setLang(*it, "und");}

        result << "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"sub1\",LANGUAGE=\"" << M.getLang(*it)
               << "\",NAME=\"" << Encodings::ISO639::decode(M.getLang(*it))
               << "\",AUTOSELECT=NO,DEFAULT=NO,FORCED=NO,URI=\"" << *it << "/index.m3u8\""
               << "\r\n";
      }
    }
    for (std::set<size_t>::iterator it = aTracks.begin(); it != aTracks.end(); it++){
      if (M.getLang(*it).empty()){meta.setLang(*it, "und");}

      result << "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud1\",LANGUAGE=\"" << M.getLang(*it)
             << "\",NAME=\"" << Encodings::ISO639::decode(M.getLang(*it))
             << "\",AUTOSELECT=YES,DEFAULT=YES,URI=\"" << *it << "/index.m3u8\""
             << "\r\n";
    }
    for (std::set<size_t>::iterator it = sTracks.begin(); it != sTracks.end(); it++){
      if (M.getLang(*it).empty()){meta.setLang(*it, "und");}

      result << "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"sub1\",LANGUAGE=\"" << M.getLang(*it)
             << "\",NAME=\"" << Encodings::ISO639::decode(M.getLang(*it))
             << "\",AUTOSELECT=NO,DEFAULT=NO,FORCED=NO,URI=\"" << *it << "/index.m3u8\""
             << "\r\n";
    }
    if (aTracks.size() && !vTracks.size()){
      std::string codec = M.getCodec(*aTracks.begin());
      result << "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" << M.getBps(*aTracks.begin()) * 8;
      result << ",CODECS=\""
             << Util::codecString(M.getCodec(*aTracks.begin()), M.getInit(*aTracks.begin())) << "\"\r\n";
      result << *aTracks.begin() << "/index.m3u8\r\n";
    }
    HIGH_MSG("Sending this index: %s", result.str().c_str());
    return result.str();
  }

  std::string OutCMAF::hlsManifest(size_t idx, const std::string &sessId){
    std::stringstream result;
    // parse single track
    uint32_t targetDuration = (M.biggestFragment(idx) / 1000) + 1;

    DTSC::Fragments fragments(M.fragments(idx));
    uint32_t firstFragment = fragments.getFirstValid();
    uint32_t endFragment = fragments.getEndValid();
    // skip the first two fragments if live
    if (M.getLive() && (endFragment - firstFragment) > 6){firstFragment += 2;}
    if (M.getType(idx) == "audio"){
      uint32_t mainTrack = M.mainTrack();
      if (mainTrack == INVALID_TRACK_ID){return "";}
      DTSC::Fragments f(M.fragments(mainTrack));
      uint64_t firstVidTime = M.getTimeForFragmentIndex(mainTrack, f.getFirstValid());
      firstFragment = M.getFragmentIndexForTime(idx, firstVidTime);
    }

    result << "#EXTM3U\r\n"
              "#EXT-X-VERSION:7\r\n"
              "#EXT-X-TARGETDURATION:"
           << targetDuration << "\r\n";
    if (M.getLive()){result << "#EXT-X-MEDIA-SEQUENCE:" << firstFragment << "\r\n";}
    result << "#EXT-X-MAP:URI=\"init.m4s"
           << "\"\r\n";

    generateSegmentlist(idx, result, hlsSegment);

    if (M.getVod()){result << "#EXT-X-ENDLIST\r\n";}
    return result.str();
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

  /// Converts bytes per second and track ID into a single bits per second value, where the last two
  /// digits are the track ID. Breaks for track IDs > 99. But really, this is MS-SS, so who cares..?
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
    H.SetHeader("Cache-Control", "no-cache");
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

  void OutCMAF::smoothAdaptation(const std::string &type, std::set<size_t> tracks, std::stringstream &r){
    if (!tracks.size()){return;}
    DTSC::Keys keys(M.keys(*tracks.begin()));
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
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      if (M.getType(it->first) == "video"){vTracks.insert(it->first);}
      if (M.getType(it->first) == "audio"){aTracks.insert(it->first);}
    }

    if (!aTracks.size() && !vTracks.size()){
      FAIL_MSG("No valid tracks found");
      return "";
    }

    if (M.getVod()){
      r << "Duration=\"" << M.getLastms(vTracks.size() ? *vTracks.begin() : *aTracks.begin()) << "\">\n";
    }else{
      r << "Duration=\"0\" IsLive=\"TRUE\" LookAheadFragmentCount=\"2\" DVRWindowLength=\""
        << M.getBufferWindow() << "\" CanSeek=\"TRUE\" CanPause=\"TRUE\">\n";
    }

    smoothAdaptation("audio", aTracks, r);
    smoothAdaptation("video", vTracks, r);
    r << "</SmoothStreamingMedia>\n";

    return toUTF16(r.str());
  }// namespace Mist

}// namespace Mist
