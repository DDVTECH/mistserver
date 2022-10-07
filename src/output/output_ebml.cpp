#include "output_ebml.h"
#include <mist/ebml_socketglue.h>
#include <mist/opus.h>
#include <mist/riff.h>

namespace Mist{
  OutEBML::OutEBML(Socket::Connection &conn) : HTTPOutput(conn){
    currentClusterTime = 0;
    newClusterTime = 0;
    segmentSize = 0xFFFFFFFFFFFFFFFFull;
    tracksSize = 0;
    infoSize = 0;
    cuesSize = 0;
    seekheadSize = 0;
    seekSize = 0;
    doctype = "matroska";
    if (config->getString("target").size()){
      if (config->getString("target").substr(0, 9) == "mkv-exec:"){
        std::string input = config->getString("target").substr(9);
        char *args[128];
        uint8_t argCnt = 0;
        char *startCh = 0;
        for (char *i = (char *)input.c_str(); i <= input.data() + input.size(); ++i){
          if (!*i){
            if (startCh){args[argCnt++] = startCh;}
            break;
          }
          if (*i == ' '){
            if (startCh){
              args[argCnt++] = startCh;
              startCh = 0;
              *i = 0;
            }
          }else{
            if (!startCh){startCh = i;}
          }
        }
        args[argCnt] = 0;

        int fin = -1;
        Util::Procs::StartPiped(args, &fin, 0, 0);
        myConn.open(fin, -1);

        wantRequest = false;
        parseData = true;
        return;
      }
      if (config->getString("target").find(".webm") != std::string::npos){doctype = "webm";}
      initialize();
      if (!M.getLive()){calcVodSizes();}
      if (!streamName.size()){
        WARN_MSG("Recording unconnected EBML output to file! Cancelled.");
        conn.close();
        return;
      }
      if (config->getString("target") == "-"){
        parseData = true;
        wantRequest = false;
        INFO_MSG("Outputting %s to stdout in EBML format", streamName.c_str());
        return;
      }
      if (!M.getValidTracks().size()){
        INFO_MSG("Stream not available - aborting");
        conn.close();
        return;
      }
      if (genericWriter(config->getString("target"))){
        parseData = true;
        wantRequest = false;
        INFO_MSG("Recording %s to %s in EBML format", streamName.c_str(),
                 config->getString("target").c_str());
        return;
      }
      conn.close();
    }
  }

  void OutEBML::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "EBML";
    capa["friendly"] = "WebM/MKV over HTTP";
    capa["desc"] = "Pseudostreaming in MKV and WebM (EBML) formats over HTTP";
    capa["url_rel"] = "/$.webm";
    capa["url_match"].append("/$.mkv");
    capa["url_match"].append("/$.webm");
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][0u].append("VP8");
    capa["codecs"][0u][0u].append("VP9");
    capa["codecs"][0u][0u].append("theora");
    capa["codecs"][0u][0u].append("MPEG2");
    capa["codecs"][0u][0u].append("AV1");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("vorbis");
    capa["codecs"][0u][1u].append("opus");
    capa["codecs"][0u][1u].append("PCM");
    capa["codecs"][0u][1u].append("ALAW");
    capa["codecs"][0u][1u].append("ULAW");
    capa["codecs"][0u][1u].append("MP2");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("FLOAT");
    capa["codecs"][0u][1u].append("AC3");
    capa["codecs"][0u][1u].append("DTS");
    capa["codecs"][0u][2u].append("+JSON");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/webm";
    capa["methods"][0u]["hrn"] = "MKV progressive";
    capa["methods"][0u]["priority"] = 9;
    // Browsers only support VP8/VP9/Opus codecs, except Chrome which is more lenient.
    JSON::Value blacklistNonChrome = JSON::fromString(
        "[[\"blacklist\", [\"Mozilla/\"]], [\"whitelist\",[\"Chrome\",\"Chromium\"]], "
        "[\"blacklist\",[\"Edge\",\"OPR/\"]], [\"blacklist\",[\"Android\"]]]");
    capa["exceptions"]["codec:H264"] = blacklistNonChrome;
    capa["exceptions"]["codec:HEVC"] = blacklistNonChrome;
    capa["exceptions"]["codec:theora"] = blacklistNonChrome;
    capa["exceptions"]["codec:MPEG2"] = blacklistNonChrome;
    capa["exceptions"]["codec:AAC"] = blacklistNonChrome;
    capa["exceptions"]["codec:vorbis"] = blacklistNonChrome;
    capa["exceptions"]["codec:PCM"] = blacklistNonChrome;
    capa["exceptions"]["codec:ALAW"] = blacklistNonChrome;
    capa["exceptions"]["codec:ULAW"] = blacklistNonChrome;
    capa["exceptions"]["codec:MP2"] = blacklistNonChrome;
    capa["exceptions"]["codec:MP3"] = blacklistNonChrome;
    capa["exceptions"]["codec:FLOAT"] = blacklistNonChrome;
    capa["exceptions"]["codec:AC3"] = blacklistNonChrome;
    capa["exceptions"]["codec:DTS"] = blacklistNonChrome;
    capa["push_urls"].append("/*.mkv");
    capa["push_urls"].append("/*.webm");
    capa["push_urls"].append("mkv-exec:*");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target filename to store EBML file as, or - for stdout.";
    cfg->addOption("target", opt);
  }

  bool OutEBML::isRecording(){return config->getString("target").size();}

  /// Calculates the size of a Cluster (contents only) and returns it.
  /// Bases the calculation on the currently selected tracks and the given start/end time for the
  /// cluster.
  size_t OutEBML::clusterSize(uint64_t start, uint64_t end){
    size_t sendLen = EBML::sizeElemUInt(EBML::EID_TIMECODE, start);
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      DTSC::Keys keys(M.keys(it->first));
      DTSC::Parts parts(M.parts(it->first));

      uint32_t firstPart = keys.getFirstPart(keys.getFirstValid());
      uint64_t curMS = 0;

      for (size_t i = keys.getFirstValid(); i < keys.getEndValid(); ++i){
        if (keys.getTime(i) > start){break;}
        firstPart =  keys.getFirstPart(i);
        curMS = keys.getTime(i);
      }
      for (size_t i = firstPart; i < parts.getEndValid(); ++i){
        if (curMS >= end){break;}
        if (curMS >= start){
          uint32_t blkLen = EBML::sizeSimpleBlock(it->first + 1, parts.getSize(i));
          sendLen += blkLen;
        }
        curMS += parts.getDuration(i);
      }
    }
    return sendLen;
  }

  void OutEBML::sendNext(){
    if (thisPacket.getTime() >= newClusterTime){
      if (liveSeek()){return;}
      currentClusterTime = thisPacket.getTime();
      if (!M.getLive()){
        // In case of VoD, clusters are aligned with the main track fragments
        // EXCEPT when they are more than 30 seconds long, because clusters are limited to -32 to 32
        // seconds.
        size_t idx = getMainSelectedTrack();
        DTSC::Fragments fragments(M.fragments(idx));
        uint32_t fragIndice = M.getFragmentIndexForTime(idx, currentClusterTime);
        newClusterTime = M.getTimeForFragmentIndex(idx, fragIndice) + fragments.getDuration(fragIndice);
        // Limit clusters to 30s, and the last fragment should always be 30s, just in case.
        if ((newClusterTime - currentClusterTime > 30000) || (fragIndice == fragments.getEndValid() - 1)){
          newClusterTime = currentClusterTime + 30000;
        }
        EXTREME_MSG("Cluster: %" PRIu64 " - %" PRIu64 " (%" PRIu32 "/%zu) = %zu",
                    currentClusterTime, newClusterTime, fragIndice, fragments.getEndValid(),
                    clusterSize(currentClusterTime, newClusterTime));
      }else{
        // In live, clusters are aligned with the lookAhead time
        newClusterTime = currentClusterTime + (needsLookAhead ? needsLookAhead : 1);
        // EXCEPT if there's a keyframe within the lookAhead window, then align to that keyframe
        // instead This makes sure that inlineRestartCapable works as intended
        uint64_t nxtKTime = nextKeyTime();
        if (nxtKTime && nxtKTime < newClusterTime){newClusterTime = nxtKTime;}
      }
      EBML::sendElemHead(myConn, EBML::EID_CLUSTER, clusterSize(currentClusterTime, newClusterTime));
      EBML::sendElemUInt(myConn, EBML::EID_TIMECODE, currentClusterTime);
    }

    DTSC::Packet p(thisPacket, thisIdx + 1);
    EBML::sendSimpleBlock(myConn, p, currentClusterTime, M.getType(thisIdx) != "video");
  }

  std::string OutEBML::trackCodecID(size_t idx){
    std::string codec = M.getCodec(idx);
    if (codec == "opus"){return "A_OPUS";}
    if (codec == "H264"){return "V_MPEG4/ISO/AVC";}
    if (codec == "HEVC"){return "V_MPEGH/ISO/HEVC";}
    if (codec == "VP8"){return "V_VP8";}
    if (codec == "VP9"){return "V_VP9";}
    if (codec == "AV1"){return "V_AV1";}
    if (codec == "AAC"){return "A_AAC";}
    if (codec == "vorbis"){return "A_VORBIS";}
    if (codec == "theora"){return "V_THEORA";}
    if (codec == "MPEG2"){return "V_MPEG2";}
    if (codec == "PCM"){return "A_PCM/INT/BIG";}
    if (codec == "MP2"){return "A_MPEG/L2";}
    if (codec == "MP3"){return "A_MPEG/L3";}
    if (codec == "AC3"){return "A_AC3";}
    if (codec == "ALAW"){return "A_MS/ACM";}
    if (codec == "ULAW"){return "A_MS/ACM";}
    if (codec == "FLOAT"){return "A_PCM/FLOAT/IEEE";}
    if (codec == "DTS"){return "A_DTS";}
    if (codec == "JSON"){return "M_JSON";}
    return "E_UNKNOWN";
  }

  void OutEBML::sendElemTrackEntry(size_t idx){
    // First calculate the sizes of the TrackEntry and Audio/Video elements.
    size_t sendLen = 0;
    size_t subLen = 0;
    sendLen += EBML::sizeElemUInt(EBML::EID_TRACKNUMBER, idx + 1);
    sendLen += EBML::sizeElemUInt(EBML::EID_TRACKUID, idx + 1);
    sendLen += EBML::sizeElemStr(EBML::EID_CODECID, trackCodecID(idx));
    sendLen += EBML::sizeElemStr(EBML::EID_LANGUAGE, M.getLang(idx).size() ? M.getLang(idx) : "und");
    sendLen += EBML::sizeElemUInt(EBML::EID_FLAGLACING, 0);
    std::string codec = M.getCodec(idx);
    if (codec == "ALAW" || codec == "ULAW"){
      sendLen += EBML::sizeElemStr(EBML::EID_CODECPRIVATE, std::string((size_t)18, '\000'));
    }else{
      if (M.getInit(idx).size()){
        sendLen += EBML::sizeElemStr(EBML::EID_CODECPRIVATE, M.getInit(idx));
      }
    }
    if (codec == "opus" && M.getInit(idx).size() > 11){
      sendLen += EBML::sizeElemUInt(EBML::EID_CODECDELAY, Opus::getPreSkip(M.getInit(idx).data()) * 1000000 / 48);
      sendLen += EBML::sizeElemUInt(EBML::EID_SEEKPREROLL, 80000000);
    }
    std::string type = M.getType(idx);
    if (type == "video"){
      sendLen += EBML::sizeElemUInt(EBML::EID_TRACKTYPE, 1);
      subLen += EBML::sizeElemUInt(EBML::EID_PIXELWIDTH, M.getWidth(idx));
      subLen += EBML::sizeElemUInt(EBML::EID_PIXELHEIGHT, M.getHeight(idx));
      subLen += EBML::sizeElemUInt(EBML::EID_DISPLAYWIDTH, M.getWidth(idx));
      subLen += EBML::sizeElemUInt(EBML::EID_DISPLAYHEIGHT, M.getHeight(idx));
      sendLen += EBML::sizeElemHead(EBML::EID_VIDEO, subLen);
    }
    if (type == "audio"){
      sendLen += EBML::sizeElemUInt(EBML::EID_TRACKTYPE, 2);
      subLen += EBML::sizeElemUInt(EBML::EID_CHANNELS, M.getChannels(idx));
      subLen += EBML::sizeElemDbl(EBML::EID_SAMPLINGFREQUENCY, M.getRate(idx));
      subLen += EBML::sizeElemUInt(EBML::EID_BITDEPTH, M.getSize(idx));
      sendLen += EBML::sizeElemHead(EBML::EID_AUDIO, subLen);
    }
    if (type == "meta"){sendLen += EBML::sizeElemUInt(EBML::EID_TRACKTYPE, 3);}
    sendLen += subLen;

    // Now actually send.
    EBML::sendElemHead(myConn, EBML::EID_TRACKENTRY, sendLen);
    EBML::sendElemUInt(myConn, EBML::EID_TRACKNUMBER, idx + 1);
    EBML::sendElemUInt(myConn, EBML::EID_TRACKUID, idx + 1);
    EBML::sendElemStr(myConn, EBML::EID_CODECID, trackCodecID(idx));
    EBML::sendElemStr(myConn, EBML::EID_LANGUAGE, M.getLang(idx).size() ? M.getLang(idx) : "und");
    EBML::sendElemUInt(myConn, EBML::EID_FLAGLACING, 0);
    if (codec == "ALAW" || codec == "ULAW"){
      std::string init =
          RIFF::fmt::generate(((codec == "ALAW") ? 6 : 7), M.getChannels(idx), M.getRate(idx),
                              M.getBps(idx), M.getChannels(idx) * (M.getSize(idx) << 3), M.getSize(idx));
      EBML::sendElemStr(myConn, EBML::EID_CODECPRIVATE, init.substr(8));
    }else{
      if (M.getInit(idx).size()){
        EBML::sendElemStr(myConn, EBML::EID_CODECPRIVATE, M.getInit(idx));
      }
    }
    if (codec == "opus" && M.getInit(idx).size() > 11){
      EBML::sendElemUInt(myConn, EBML::EID_CODECDELAY, Opus::getPreSkip(M.getInit(idx).data()) * 1000000 / 48);
      EBML::sendElemUInt(myConn, EBML::EID_SEEKPREROLL, 80000000);
    }
    if (type == "video"){
      EBML::sendElemUInt(myConn, EBML::EID_TRACKTYPE, 1);
      EBML::sendElemHead(myConn, EBML::EID_VIDEO, subLen);
      EBML::sendElemUInt(myConn, EBML::EID_PIXELWIDTH, M.getWidth(idx));
      EBML::sendElemUInt(myConn, EBML::EID_PIXELHEIGHT, M.getHeight(idx));
      EBML::sendElemUInt(myConn, EBML::EID_DISPLAYWIDTH, M.getWidth(idx));
      EBML::sendElemUInt(myConn, EBML::EID_DISPLAYHEIGHT, M.getHeight(idx));
    }
    if (type == "audio"){
      EBML::sendElemUInt(myConn, EBML::EID_TRACKTYPE, 2);
      EBML::sendElemHead(myConn, EBML::EID_AUDIO, subLen);
      EBML::sendElemUInt(myConn, EBML::EID_CHANNELS, M.getChannels(idx));
      EBML::sendElemDbl(myConn, EBML::EID_SAMPLINGFREQUENCY, M.getRate(idx));
      EBML::sendElemUInt(myConn, EBML::EID_BITDEPTH, M.getSize(idx));
    }
    if (type == "meta"){EBML::sendElemUInt(myConn, EBML::EID_TRACKTYPE, 3);}
  }

  size_t OutEBML::sizeElemTrackEntry(size_t idx){
    // Calculate the sizes of the TrackEntry and Audio/Video elements.
    size_t sendLen = 0;
    size_t subLen = 0;
    sendLen += EBML::sizeElemUInt(EBML::EID_TRACKNUMBER, idx + 1);
    sendLen += EBML::sizeElemUInt(EBML::EID_TRACKUID, idx + 1);
    sendLen += EBML::sizeElemStr(EBML::EID_CODECID, trackCodecID(idx));
    sendLen += EBML::sizeElemStr(EBML::EID_LANGUAGE, M.getLang(idx).size() ? M.getLang(idx) : "und");
    sendLen += EBML::sizeElemUInt(EBML::EID_FLAGLACING, 0);
    std::string codec = M.getCodec(idx);
    if (codec == "ALAW" || codec == "ULAW"){
      sendLen += EBML::sizeElemStr(EBML::EID_CODECPRIVATE, std::string((size_t)18, '\000'));
    }else{
      if (M.getInit(idx).size()){
        sendLen += EBML::sizeElemStr(EBML::EID_CODECPRIVATE, M.getInit(idx));
      }
    }
    std::string type = M.getType(idx);
    if (codec == "opus" && M.getInit(idx).size() > 11){
      sendLen += EBML::sizeElemUInt(EBML::EID_CODECDELAY, Opus::getPreSkip(M.getInit(idx).data()) * 1000000 / 48);
      sendLen += EBML::sizeElemUInt(EBML::EID_SEEKPREROLL, 80000000);
    }
    if (type == "video"){
      sendLen += EBML::sizeElemUInt(EBML::EID_TRACKTYPE, 1);
      subLen += EBML::sizeElemUInt(EBML::EID_PIXELWIDTH, M.getWidth(idx));
      subLen += EBML::sizeElemUInt(EBML::EID_PIXELHEIGHT, M.getHeight(idx));
      subLen += EBML::sizeElemUInt(EBML::EID_DISPLAYWIDTH, M.getWidth(idx));
      subLen += EBML::sizeElemUInt(EBML::EID_DISPLAYHEIGHT, M.getHeight(idx));
      sendLen += EBML::sizeElemHead(EBML::EID_VIDEO, subLen);
    }
    if (type == "audio"){
      sendLen += EBML::sizeElemUInt(EBML::EID_TRACKTYPE, 2);
      subLen += EBML::sizeElemUInt(EBML::EID_CHANNELS, M.getChannels(idx));
      subLen += EBML::sizeElemDbl(EBML::EID_SAMPLINGFREQUENCY, M.getRate(idx));
      subLen += EBML::sizeElemUInt(EBML::EID_BITDEPTH, M.getSize(idx));
      sendLen += EBML::sizeElemHead(EBML::EID_AUDIO, subLen);
    }
    if (type == "meta"){sendLen += EBML::sizeElemUInt(EBML::EID_TRACKTYPE, 3);}
    sendLen += subLen;
    return EBML::sizeElemHead(EBML::EID_TRACKENTRY, sendLen) + sendLen;
  }

  void OutEBML::sendHeader(){
    double duration = 0;
    size_t idx = getMainSelectedTrack();
    if (!M.getLive()){
      duration = M.getLastms(idx) - M.getFirstms(idx);
    }else{
      needsLookAhead = 420;
    }
    // EBML header and Segment
    EBML::sendElemEBML(myConn, doctype);
    EBML::sendElemHead(myConn, EBML::EID_SEGMENT, segmentSize); // Default = Unknown size
    if (!M.getLive()){
      // SeekHead
      EBML::sendElemHead(myConn, EBML::EID_SEEKHEAD, seekSize);
      EBML::sendElemSeek(myConn, EBML::EID_INFO, seekheadSize);
      EBML::sendElemSeek(myConn, EBML::EID_TRACKS, seekheadSize + infoSize);
      EBML::sendElemSeek(myConn, EBML::EID_CUES, seekheadSize + infoSize + tracksSize);
    }
    // Info
    EBML::sendElemInfo(myConn, APPIDENT, duration);
    // Tracks
    size_t trackSizes = 0;
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      trackSizes += sizeElemTrackEntry(it->first);
    }
    EBML::sendElemHead(myConn, EBML::EID_TRACKS, trackSizes);
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      sendElemTrackEntry(it->first);
    }
    if (!M.getLive()){
      EBML::sendElemHead(myConn, EBML::EID_CUES, cuesSize);
      uint64_t tmpsegSize = infoSize + tracksSize + seekheadSize + cuesSize +
                            EBML::sizeElemHead(EBML::EID_CUES, cuesSize);
      for (std::map<size_t, size_t>::iterator it = clusterSizes.begin(); it != clusterSizes.end(); ++it){
        EBML::sendElemCuePoint(myConn, it->first, idx + 1, tmpsegSize, 0);
        tmpsegSize += it->second;
      }
    }
    sentHeader = true;
  }

  /// Seeks to the given byte position by doing a regular seek and remembering the byte offset from
  /// that point
  void OutEBML::byteSeek(size_t startPos){
    INFO_MSG("Seeking to %zu bytes", startPos);
    sentHeader = false;
    newClusterTime = 0;
    if (startPos == 0){
      seek(0);
      return;
    }
    size_t headerSize = EBML::sizeElemEBML(doctype) +
                        EBML::sizeElemHead(EBML::EID_SEGMENT, segmentSize) + seekheadSize + infoSize +
                        tracksSize + EBML::sizeElemHead(EBML::EID_CUES, cuesSize) + cuesSize;
    if (startPos < headerSize){
      HIGH_MSG("Seek went into or before header");
      seek(0);
      myConn.skipBytes(startPos);
      return;
    }
    startPos -= headerSize;
    sentHeader = true; // skip the header
    for (std::map<size_t, size_t>::iterator it = clusterSizes.begin(); it != clusterSizes.end(); ++it){
      VERYHIGH_MSG("Cluster %zu (%zu bytes) -> %zu to go", it->first, it->second, startPos);
      if (startPos < it->second){
        HIGH_MSG("Seek to fragment at %zu ms", it->first);
        myConn.skipBytes(startPos);
        seek(it->first);
        newClusterTime = it->first;
        return;
      }
      startPos -= it->second;
    }
    // End of file. This probably won't work right, but who cares, it's the end of the file.
  }

  void OutEBML::respondHTTP(const HTTP::Parser & req, bool headersOnly){
    //Set global defaults, first
    HTTPOutput::respondHTTP(req, headersOnly);

    //If non-live, we accept range requests
    if (!M.getLive()){H.SetHeader("Accept-Ranges", "bytes, parsec");}

    //We change the header's document type based on file extension
    if (req.url.find(".webm") != std::string::npos){
      doctype = "webm";
    }else{
      doctype = "matroska";
    }

    // Calculate the sizes of various parts, if we're VoD.
    size_t totalSize = 0;
    if (!M.getLive()){
      calcVodSizes();
      // We now know the full size of the segment, thus can calculate the total size
      totalSize = EBML::sizeElemEBML(doctype) + EBML::sizeElemHead(EBML::EID_SEGMENT, segmentSize) + segmentSize;
    }

    uint64_t byteEnd = totalSize - 1;
    uint64_t byteStart = 0;
    if (!M.getLive() && req.GetHeader("Range") != ""){
      //Range request
      if (parseRange(req.GetHeader("Range"), byteStart, byteEnd)){
        if (!req.GetVar("buffer").size()){
          size_t idx = getMainSelectedTrack();
          maxSkipAhead = (M.getLastms(idx) - M.getFirstms(idx)) / 20 + 7500;
        }
      }
      //Failed range request
      if (!byteEnd){
        if (req.GetHeader("Range")[0] == 'p'){
          if (!headersOnly){H.SetBody("Starsystem not in communications range");}
          H.SendResponse("416", "Starsystem not in communications range", myConn);
          return;
        }
        if (!headersOnly){H.SetBody("Requested Range Not Satisfiable");}
        H.SendResponse("416", "Requested Range Not Satisfiable", myConn);
        return;
      }
      //Successful range request
      std::stringstream rangeReply;
      rangeReply << "bytes " << byteStart << "-" << byteEnd << "/" << totalSize;
      H.SetHeader("Content-Length", byteEnd - byteStart + 1);
      H.SetHeader("Content-Range", rangeReply.str());
      /// \todo Switch to chunked?
      H.SendResponse("206", "Partial content", myConn);
      if (!headersOnly){
        byteSeek(byteStart);
      }
    }else{
      //Non-range request
      if (!M.getLive()){H.SetHeader("Content-Length", byteEnd - byteStart + 1);}
      /// \todo Switch to chunked?
      H.SendResponse("200", "OK", myConn);
    }
    //Start outputting data
    if (!headersOnly){
      parseData = true;
      wantRequest = false;
    }
  }

  void OutEBML::calcVodSizes(){
    if (segmentSize != 0xFFFFFFFFFFFFFFFFull){
      // Already calculated
      return;
    }
    size_t idx = getMainSelectedTrack();
    double duration = M.getLastms(idx) - M.getFirstms(idx);
    // Calculate the segment size
    // Segment contains SeekHead, Info, Tracks, Cues (in that order)
    // Howeveer, SeekHead is dependent on Info/Tracks sizes, so we calculate those first.
    // Calculating Info size
    infoSize = EBML::sizeElemInfo(APPIDENT, duration);
    // Calculating Tracks size
    tracksSize = 0;
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      tracksSize += sizeElemTrackEntry(it->first);
    }
    tracksSize += EBML::sizeElemHead(EBML::EID_TRACKS, tracksSize);
    // Calculating SeekHead size
    // Positions are relative to the first Segment, byte 0 = first byte of contents of Segment.
    // Tricky starts here: the size of the SeekHead element is dependent on the seek offsets contained inside,
    // which are in turn dependent on the size of the SeekHead element. Fun times! We loop until it stabilizes.
    uint32_t oldseekSize = 0;
    do{
      oldseekSize = seekSize;
      seekSize = EBML::sizeElemSeek(EBML::EID_INFO, seekheadSize) +
                 EBML::sizeElemSeek(EBML::EID_TRACKS, seekheadSize + infoSize) +
                 EBML::sizeElemSeek(EBML::EID_CUES, seekheadSize + infoSize + tracksSize);
      seekheadSize = EBML::sizeElemHead(EBML::EID_SEEKHEAD, seekSize) + seekSize;
    }while (seekSize != oldseekSize);
    // The Cues are tricky: the Cluster offsets are dependent on the size of Cues itself.
    // Which, in turn, is dependent on the Cluster offsets.
    // We make this a bit easier by pre-calculating the sizes of all clusters first
    uint64_t fragNo = 0;
    DTSC::Fragments fragments(M.fragments(idx));
    for (size_t i = fragments.getFirstValid(); i < fragments.getEndValid(); i++){
      uint64_t clusterStart = M.getTimeForFragmentIndex(idx, i);
      uint64_t clusterEnd = clusterStart + fragments.getDuration(i);
      // The first fragment always starts at time 0, even if the main track does not.
      if (!fragNo){clusterStart = 0;}
      uint64_t clusterTmpEnd = clusterEnd;
      do{
        clusterTmpEnd = clusterEnd;
        // The last fragment always ends at the end, even if the main track does not.
        if (fragNo == fragments.getEndValid() - 1){clusterTmpEnd = clusterStart + 30000;}
        // Limit clusters to 30 seconds.
        if (clusterTmpEnd - clusterStart > 30000){clusterTmpEnd = clusterStart + 30000;}
        uint64_t cSize = clusterSize(clusterStart, clusterTmpEnd);
        clusterSizes[clusterStart] = cSize + EBML::sizeElemHead(EBML::EID_CLUSTER, cSize);
        clusterStart = clusterTmpEnd; // Continue at the end of this cluster, if continuing.
      }while (clusterTmpEnd < clusterEnd);
      ++fragNo;
    }
    // Calculating Cues size
    // We also calculate Clusters here: Clusters are grouped by fragments of the main track.
    // CueClusterPosition uses the same offsets as SeekPosition.
    // CueRelativePosition is the offset from that Cluster's first content byte.
    // All this uses the same technique as above. More fun times!
    uint32_t oldcuesSize = 0;
    do{
      oldcuesSize = cuesSize;
      segmentSize = infoSize + tracksSize + seekheadSize + cuesSize +
                    EBML::sizeElemHead(EBML::EID_CUES, cuesSize);
      uint32_t cuesInside = 0;
      for (std::map<size_t, size_t>::iterator it = clusterSizes.begin(); it != clusterSizes.end(); ++it){
        cuesInside += EBML::sizeElemCuePoint(it->first, idx + 1, segmentSize, 0);
        segmentSize += it->second;
      }
      cuesSize = cuesInside;
    }while (cuesSize != oldcuesSize);
  }

}// namespace Mist
