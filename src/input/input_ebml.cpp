#include "input_ebml.h"

#include <mist/bitfields.h>
#include <mist/defines.h>
#include <mist/procs.h>

namespace Mist{

  InputEBML::InputEBML(Util::Config *cfg) : Input(cfg){
    timeScale = 1.0;
    capa["name"] = "EBML";
    capa["desc"] = "Allows loading MKV, MKA, MK3D, MKS and WebM files for Video on Demand, or "
                   "accepts live streams in those formats over standard input.";
    capa["source_match"].append("/*.mkv");
    capa["source_match"].append("/*.mka");
    capa["source_match"].append("/*.mk3d");
    capa["source_match"].append("/*.mks");
    capa["source_match"].append("/*.webm");
    capa["source_match"].append("http://*.mkv");
    capa["source_match"].append("http://*.mka");
    capa["source_match"].append("http://*.mk3d");
    capa["source_match"].append("http://*.mks");
    capa["source_match"].append("http://*.webm");
    capa["source_match"].append("https://*.mkv");
    capa["source_match"].append("https://*.mka");
    capa["source_match"].append("https://*.mk3d");
    capa["source_match"].append("https://*.mks");
    capa["source_match"].append("https://*.webm");
    capa["source_match"].append("s3+http://*.mkv");
    capa["source_match"].append("s3+http://*.mka");
    capa["source_match"].append("s3+http://*.mk3d");
    capa["source_match"].append("s3+http://*.mks");
    capa["source_match"].append("s3+http://*.webm");
    capa["source_match"].append("s3+https://*.mkv");
    capa["source_match"].append("s3+https://*.mka");
    capa["source_match"].append("s3+https://*.mk3d");
    capa["source_match"].append("s3+https://*.mks");
    capa["source_match"].append("s3+https://*.webm");
    capa["source_prefill"].append("/");
    capa["source_prefill"].append("http://");
    capa["source_prefill"].append("https://");
    capa["source_prefill"].append("s3+http://");
    capa["source_prefill"].append("s3+https://");
    capa["source_prefill"].append("mkv-exec:");
#if defined(__CYGWIN__)
    capa["source_syntax"].append("/cygdrive/[DRIVE/path/to/][file_name]");
#else
    capa["source_syntax"].append("/[path/to/][file_name]");
#endif
    capa["source_syntax"].append("http://[address]");
    capa["source_syntax"].append("https://[address]");
    capa["source_syntax"].append("s3+http://[address]");
    capa["source_syntax"].append("s3+https://[address]");
    capa["source_syntax"].append("mkv-exec:[COMMAND]");
    capa["source_help"]["default"] = "Location where MistServer can find the input file.";
    capa["source_help"]["mkv-exec:[COMMAND]"] = "MistServer will execute the command as if it's ran in the terminal and will expect to receive matroska data from the command.";
    capa["source_match"].append("mkv-exec:*");
    capa["always_match"].append("mkv-exec:*");
    capa["source_file"] = "$source";
    capa["priority"] = 9;
    capa["codecs"]["video"].append("H264");
    capa["codecs"]["video"].append("HEVC");
    capa["codecs"]["video"].append("VP8");
    capa["codecs"]["video"].append("VP9");
    capa["codecs"]["video"].append("AV1");
    capa["codecs"]["video"].append("theora");
    capa["codecs"]["video"].append("MPEG2");
    capa["codecs"]["video"].append("JPEG");
    capa["codecs"]["audio"].append("opus");
    capa["codecs"]["audio"].append("vorbis");
    capa["codecs"]["audio"].append("AAC");
    capa["codecs"]["audio"].append("PCM");
    capa["codecs"]["audio"].append("ALAW");
    capa["codecs"]["audio"].append("ULAW");
    capa["codecs"]["audio"].append("MP2");
    capa["codecs"]["audio"].append("MP3");
    capa["codecs"]["audio"].append("AC3");
    capa["codecs"]["audio"].append("FLOAT");
    capa["codecs"]["audio"].append("DTS");
    capa["codecs"]["audio"].append("FLAC");
    capa["codecs"]["metadata"].append("JSON");
    capa["codecs"]["subtitle"].append("subtitle");
    lastClusterBPos = 0;
    lastClusterTime = 0;
    bufferedPacks = 0;
    wantBlocks = true;
    totalBytes = 0;
    readBufferOffset = 0;
    readPos = 0;
    readingMinimal = true;
    firstRead = true;
  }

  std::string ASStoSRT(const char *ptr, uint32_t len){
    uint16_t commas = 0;
    uint16_t brackets = 0;
    std::string tmpStr;
    tmpStr.reserve(len);
    for (uint32_t i = 0; i < len; ++i){
      // Skip everything until the 8th comma
      if (commas < 8){
        if (ptr[i] == ','){commas++;}
        continue;
      }
      if (ptr[i] == '{'){
        brackets++;
        continue;
      }
      if (ptr[i] == '}'){
        brackets--;
        continue;
      }
      if (!brackets){
        if (ptr[i] == '\\' && i < len - 1 && (ptr[i + 1] == 'N' || ptr[i + 1] == 'n')){
          tmpStr += '\n';
          ++i;
          continue;
        }
        tmpStr += ptr[i];
      }
    }
    return tmpStr;
  }

  bool InputEBML::checkArguments(){
    if (!config->getString("streamname").size()){
      if (config->getString("output") == "-"){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "Output to stdout not yet supported");
        return false;
      }
    }else{
      if (config->getString("output") != "-"){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "File output in player mode not supported");
        return false;
      }
    }
    return true;
  }

  bool InputEBML::needsLock(){
    // Streamed input requires no lock, non-streamed does
    if (!standAlone){return false;}
    if (config->getString("input") == "-" || config->getString("input").substr(0, 9) == "mkv-exec:"){return false;}
    return Input::needsLock();
  }

  bool InputEBML::preRun(){
    if (config->getString("input").substr(0, 9) == "mkv-exec:"){
      standAlone = false;
      std::deque<std::string> args;
      Util::shellSplit(config->getString("input").substr(9), args);

      int fin = -1, fout = -1;
      pid_t inProc = Util::Procs::StartPiped(args, &fin, &fout, 0);
      if (fout == -1){
        Util::logExitReason(ER_EXEC_FAILURE, "Unable to start mkv-exec process `%s`",
                            config->getString("input").substr(9).c_str());
        return false;
      }
      dup2(fout, 0);
      inFile.open(0);
      INFO_MSG("Reading from process %d: %s", inProc, config->getString("input").substr(9).c_str());
      return true;
    }
    if (config->getString("input") == "-"){
      standAlone = false;
      inFile.open(0);
    }else{
      // open File
      inFile.open(config->getString("input"));
      if (!inFile){
        Util::logExitReason(ER_READ_START_FAILURE, "Opening input '%s' failed", config->getString("input").c_str());
        return false;
      }
      standAlone = inFile.isSeekable();
    }
    return true;
  }

  void InputEBML::dataCallback(const char *ptr, size_t size){
    readBuffer.append(ptr, size);
    totalBytes += size;
  }
  size_t InputEBML::getDataCallbackPos() const{return readPos + readBuffer.size();}

  bool InputEBML::readElement(){
    uint32_t needed = EBML::Element::needBytes(readBuffer + readBufferOffset, readBuffer.size() - readBufferOffset, readingMinimal);
    if (!firstRead && readBuffer.size() >= needed + readBufferOffset){
      readBufferOffset += needed;
      needed = EBML::Element::needBytes(readBuffer + readBufferOffset, readBuffer.size() - readBufferOffset, readingMinimal);
      readingMinimal = true;
      if (readBuffer.size() >= needed + readBufferOffset){
        // Make sure TrackEntry types are read whole
        if (readingMinimal && EBML::Element(readBuffer + readBufferOffset).getID() == EBML::EID_TRACKENTRY){
          readingMinimal = false;
          needed = EBML::Element::needBytes(readBuffer + readBufferOffset, readBuffer.size() - readBufferOffset, readingMinimal);
        }
      }
    }

    while (readBuffer.size() < needed + readBufferOffset && config->is_active){
      if (!readBuffer.allocate(needed + readBufferOffset)){return false;}
      if (!inFile){return false;}
      int64_t toRead = needed - readBuffer.size() + readBufferOffset;

      if (standAlone){
        //If we have more than 10MiB buffered and are more than 10MiB into the buffer, shift the first 4MiB off the buffer.
        //This prevents infinite growth of the read buffer for large files, but allows for some re-use of data.
        if (readBuffer.size() >= 10*1024*1024 && readBufferOffset > 10*1024*1024){
          readBuffer.shift(4*1024*1024);
          readBufferOffset -= 4*1024*1024;
          readPos += 4*1024*1024;
        }
      }else{
        //For non-standalone mode, we know we're always live streaming, and can always cut off what we've shifted
        if (readBufferOffset){
          readBuffer.shift(readBufferOffset);
          readPos += readBufferOffset;
          readBufferOffset = 0;
        }
      }

      size_t preSize = readBuffer.size();
      inFile.readSome(toRead, *this);
      if (readBuffer.size() == preSize){
        Util::sleep(5);
        continue;
      }

      needed = EBML::Element::needBytes(readBuffer + readBufferOffset, readBuffer.size() - readBufferOffset, readingMinimal);
      if (readBuffer.size() >= needed + readBufferOffset){
        // Make sure TrackEntry types are read whole
        if (readingMinimal && EBML::Element(readBuffer + readBufferOffset).getID() == EBML::EID_TRACKENTRY){
          readingMinimal = false;
          needed = EBML::Element::needBytes(readBuffer + readBufferOffset, readBuffer.size() - readBufferOffset, readingMinimal);
        }
      }
    }
    EBML::Element E(readBuffer + readBufferOffset);
    if (E.getID() == EBML::EID_CLUSTER){
      if (!inFile.isSeekable()){
        lastClusterBPos = 0;
      }else{
        int64_t bp = readPos + readBufferOffset;
        if (bp == -1 && errno == ESPIPE){
          lastClusterBPos = 0;
        }else{
          lastClusterBPos = bp;
        }
      }
      DONTEVEN_MSG("Found a cluster at position %" PRIu64, lastClusterBPos);
    }
    if (E.getID() == EBML::EID_TIMECODE){
      lastClusterTime = E.getValUInt();
      DONTEVEN_MSG("Cluster time %" PRIu64 " ms", lastClusterTime);
    }
    firstRead = false;
    return true;
  }

  bool InputEBML::readExistingHeader(){
    if (!Input::readExistingHeader()){return false;}
    if (M.inputLocalVars.isMember("timescale")){
      timeScale = ((double)M.inputLocalVars["timescale"].asInt()) / 1000000.0;
    }
    if (!M.inputLocalVars.isMember("version") || M.inputLocalVars["version"].asInt() < 2){
      INFO_MSG("Header needs update, regenerating");
      return false;
    }
    return true;
  }

  bool InputEBML::readHeader(){
    if (!inFile){
      Util::logExitReason(ER_READ_START_FAILURE, "Reading header for '%s' failed: Could not open input stream", config->getString("input").c_str());
      return false;
    }
    if (!meta || (needsLock() && isSingular())){
      meta.reInit(isSingular() ? streamName : "");
    }
    int64_t dateVal = 0; 
    while (readElement()){
      if (!config->is_active){
        WARN_MSG("Aborting header generation due to shutdown: %s", Util::exitReason);
        return false;
      }
      EBML::Element E(readBuffer + readBufferOffset, readingMinimal);
      if (E.getID() == EBML::EID_TRACKENTRY) { readTrack(E); }
      if (E.getID() == EBML::EID_TIMECODESCALE){
        uint64_t timeScaleVal = E.getValUInt();
        meta.inputLocalVars["timescale"] = timeScaleVal;
        timeScale = ((double)timeScaleVal) / 1000000.0;
      }
      if (E.getID() == EBML::EID_DATEUTC){dateVal = E.getValDate();}
      // Live streams stop parsing the header as soon as the first Cluster is encountered
      if (E.getID() == EBML::EID_CLUSTER){
        if (!needsLock()){return true;}
        //Set progress counter for non-live inputs
        if (streamStatus && streamStatus.len > 1 && inFile.getSize()){
          streamStatus.mapped[1] = (255 * (readPos + readBufferOffset)) / inFile.getSize();
        }
      }
      if (E.getType() == EBML::ELEM_BLOCK){
        EBML::Block B(readBuffer + readBufferOffset);
        uint64_t tNum = B.getTrackNum();
        uint64_t newTime = lastClusterTime + B.getTimecode();
        trackPredictor &TP = packBuf[tNum];
        size_t idx = meta.trackIDToIndex(tNum, getpid());
        bool isVideo = (M.getType(idx) == "video");
        bool isAudio = (M.getType(idx) == "audio");
        bool isASS = (M.getCodec(idx) == "subtitle" && M.getInit(idx).size());
        // If this is a new video keyframe, flush the corresponding trackPredictor
        if (isVideo && B.isKeyframe()){
          while (TP.hasPackets(true)){
            packetData &C = TP.getPacketData(true);
            meta.update(C.time, C.offset, idx, C.dsize, C.bpos, C.key);
            if (dateVal){
              meta.setUTCOffset(dateVal - C.time);
              if (!meta.getUTCOffset()){meta.setUTCOffset(1);}
              dateVal = 0;
            }
            TP.remove();
          }
          TP.flush();
        }
        for (uint64_t frameNo = 0; frameNo < B.getFrameCount(); ++frameNo){
          if (frameNo){
            if (M.getCodec(idx) == "AAC"){
              newTime += (uint64_t)(1000000 / M.getRate(idx)) / timeScale; // assume ~1000 samples per frame
            }else if (M.getCodec(idx) == "MP3"){
              newTime += (uint64_t)(1152000 / M.getRate(idx)) / timeScale; // 1152 samples per frame
            }else if (M.getCodec(idx) == "DTS"){
              // Assume 512 samples per frame (DVD default)
              // actual amount can be calculated from data, but data
              // is not available during header generation...
              // See: http://www.stnsoft.com/DVD/dtshdr.html
              newTime += (uint64_t)(512000 / M.getRate(idx)) / timeScale;
            }else{
              newTime += 1 / timeScale;
              ERROR_MSG("Unknown frame duration for codec %s - timestamps WILL be wrong!",
                        M.getCodec(idx).c_str());
            }
          }
          uint32_t frameSize = B.getFrameSize(frameNo);
          if (isASS){
            char *ptr = (char *)B.getFrameData(frameNo);
            std::string assStr = ASStoSRT(ptr, frameSize);
            frameSize = assStr.size();
          }
          if (frameSize){
            TP.add(newTime * timeScale, tNum, frameSize, lastClusterBPos, B.isKeyframe() && !isAudio, isVideo);
          }
        }
        while (TP.hasPackets()){
          packetData &C = TP.getPacketData(isVideo);
          meta.update(C.time, C.offset, idx, C.dsize, C.bpos, C.key);
          if (dateVal){
            meta.setUTCOffset(dateVal - C.time);
            if (!meta.getUTCOffset()){meta.setUTCOffset(1);}
            dateVal = 0;
          }
          TP.remove();
        }
      }
    }

    if (packBuf.size()){
      for (std::map<uint64_t, trackPredictor>::iterator it = packBuf.begin(); it != packBuf.end(); ++it){
        trackPredictor &TP = it->second;
        while (TP.hasPackets(true)){
          packetData &C =
              TP.getPacketData(M.getType(M.trackIDToIndex(it->first, getpid())) == "video");
          meta.update(C.time, C.offset, M.trackIDToIndex(C.track, getpid()), C.dsize, C.bpos, C.key);
          if (dateVal){
            meta.setUTCOffset(dateVal - C.time);
            if (!meta.getUTCOffset()){meta.setUTCOffset(1);}
            dateVal = 0;
          }
          TP.remove();
        }
      }
    }

    meta.inputLocalVars["version"] = 2;
    clearPredictors();
    bufferedPacks = 0;
    return true;
  }

  void InputEBML::readTrack(const EBML::Element & E) {
    EBML::Element tmpElem = E.findChild(EBML::EID_TRACKNUMBER);
    if (!tmpElem) {
      ERROR_MSG("Track without track number encountered, ignoring");
      return;
    }
    uint64_t trackID = tmpElem.getValUInt();
    tmpElem = E.findChild(EBML::EID_CODECID);
    if (!tmpElem) {
      ERROR_MSG("Track without codec id encountered, ignoring");
      return;
    }
    std::string codec = tmpElem.getValString(), trueCodec, trueType, lang, init;
    if (codec == "V_MPEG4/ISO/AVC") {
      trueCodec = "H264";
      trueType = "video";
      tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
      if (tmpElem) { init = tmpElem.getValStringUntrimmed(); }
    }
    if (codec == "V_MPEGH/ISO/HEVC") {
      trueCodec = "HEVC";
      trueType = "video";
      tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
      if (tmpElem) { init = tmpElem.getValStringUntrimmed(); }
    }
    if (codec == "V_AV1") {
      trueCodec = "AV1";
      trueType = "video";
    }
    if (codec == "V_VP9") {
      trueCodec = "VP9";
      trueType = "video";
    }
    if (codec == "V_VP8") {
      trueCodec = "VP8";
      trueType = "video";
    }
    if (codec == "A_OPUS") {
      trueCodec = "opus";
      trueType = "audio";
      tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
      if (tmpElem) { init = tmpElem.getValStringUntrimmed(); }
    }
    if (codec == "A_VORBIS") {
      trueCodec = "vorbis";
      trueType = "audio";
      tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
      if (tmpElem) { init = tmpElem.getValStringUntrimmed(); }
    }
    if (codec == "V_THEORA") {
      trueCodec = "theora";
      trueType = "video";
      tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
      if (tmpElem) { init = tmpElem.getValStringUntrimmed(); }
    }
    if (codec == "A_AAC") {
      trueCodec = "AAC";
      trueType = "audio";
      tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
      if (tmpElem) { init = tmpElem.getValStringUntrimmed(); }
    }
    if (codec == "A_DTS") {
      trueCodec = "DTS";
      trueType = "audio";
    }
    if (codec == "A_PCM/INT/BIG") {
      trueCodec = "PCM";
      trueType = "audio";
    }
    if (codec == "A_PCM/INT/LIT") {
      trueCodec = "PCMLE";
      trueType = "audio";
    }
    if (codec == "A_AC3") {
      trueCodec = "AC3";
      trueType = "audio";
    }
    if (codec == "A_FLAC") {
      trueCodec = "FLAC";
      trueType = "audio";
      tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
      if (tmpElem) { init = tmpElem.getValStringUntrimmed(); }
    }
    if (codec == "A_MPEG/L3") {
      trueCodec = "MP3";
      trueType = "audio";
    }
    if (codec == "A_MPEG/L2") {
      trueCodec = "MP2";
      trueType = "audio";
    }
    if (codec == "V_MPEG2") {
      trueCodec = "MPEG2";
      trueType = "video";
    }
    if (codec == "V_MJPEG") {
      trueCodec = "JPEG";
      trueType = "video";
    }
    if (codec == "V_MS/VFW/FOURCC") {
      tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
      if (tmpElem) {
        std::string bitmapheader = tmpElem.getValStringUntrimmed();
        if (bitmapheader.substr(16, 4) == "MJPG") {
          trueCodec = "JPEG";
          trueType = "video";
        }
      }
    }
    if (codec == "A_PCM/FLOAT/IEEE") {
      trueCodec = "FLOAT";
      trueType = "audio";
    }
    if (codec == "M_JSON") {
      trueCodec = "JSON";
      trueType = "meta";
    }
    if (codec == "S_TEXT/UTF8") {
      trueCodec = "subtitle";
      trueType = "meta";
    }
    if (codec == "S_TEXT/ASS" || codec == "S_TEXT/SSA") {
      trueCodec = "subtitle";
      trueType = "meta";
      tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
      if (tmpElem) { init = tmpElem.getValStringUntrimmed(); }
    }
    if (codec == "A_MS/ACM") {
      tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
      if (tmpElem) {
        std::string WAVEFORMATEX = tmpElem.getValStringUntrimmed();
        unsigned int formatTag = Bit::btohs_le(WAVEFORMATEX.data());
        switch (formatTag) {
          case 3:
            trueCodec = "FLOAT";
            trueType = "audio";
            break;
          case 6:
            trueCodec = "ALAW";
            trueType = "audio";
            break;
          case 7:
            trueCodec = "ULAW";
            trueType = "audio";
            break;
          case 85:
            trueCodec = "MP3";
            trueType = "audio";
            break;
          default: ERROR_MSG("Unimplemented A_MS/ACM formatTag: %u", formatTag); break;
        }
      }
    }
    if (!trueCodec.size()) {
      WARN_MSG("Unrecognised codec id %s ignoring", codec.c_str());
      return;
    }
    tmpElem = E.findChild(EBML::EID_LANGUAGE);
    if (tmpElem) { lang = tmpElem.getValString(); }
    size_t idx = M.trackIDToIndex(trackID, getpid());
    if (idx == INVALID_TRACK_ID) { idx = meta.addTrack(); }
    meta.setID(idx, trackID);
    meta.setLang(idx, lang);
    meta.setCodec(idx, trueCodec);
    meta.setType(idx, trueType);
    meta.setInit(idx, init);
    if (trueType == "video") {
      tmpElem = E.findChild(EBML::EID_PIXELWIDTH);
      meta.setWidth(idx, tmpElem ? tmpElem.getValUInt() : 0);
      tmpElem = E.findChild(EBML::EID_PIXELHEIGHT);
      meta.setHeight(idx, tmpElem ? tmpElem.getValUInt() : 0);
      meta.setFpks(idx, 0);
    }
    if (trueType == "audio") {
      tmpElem = E.findChild(EBML::EID_CHANNELS);
      meta.setChannels(idx, tmpElem ? tmpElem.getValUInt() : 1);
      tmpElem = E.findChild(EBML::EID_BITDEPTH);
      meta.setSize(idx, tmpElem ? tmpElem.getValUInt() : 0);
      tmpElem = E.findChild(EBML::EID_SAMPLINGFREQUENCY);
      meta.setRate(idx, tmpElem ? (int)tmpElem.getValFloat() : 8000);
    }
    INFO_MSG("Detected track: %s", M.getTrackIdentifier(idx).c_str());
  }

  void InputEBML::postHeader(){
    //Record PCMLE tracks as being PCM with swapped endianness
    std::set<size_t> validTracks = M.getValidTracks();
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
      if (M.getCodec(*it) == "PCMLE"){
        meta.setCodec(*it, "PCM");
        swapEndianness.insert(*it);
      }
    }
  }

  void InputEBML::fillPacket(packetData &C){
    thisIdx = M.trackIDToIndex(C.track, getpid());
    if (swapEndianness.count(C.track)){
      switch (M.getSize(thisIdx)){
      case 16:{
        char *ptr = C.ptr;
        uint32_t ptrSize = C.dsize;
        for (uint32_t i = 0; i < ptrSize; i += 2){
          char tmpchar = ptr[i];
          ptr[i] = ptr[i + 1];
          ptr[i + 1] = tmpchar;
        }
      }break;
      case 24:{
        char *ptr = C.ptr;
        uint32_t ptrSize = C.dsize;
        for (uint32_t i = 0; i < ptrSize; i += 3){
          char tmpchar = ptr[i];
          ptr[i] = ptr[i + 2];
          ptr[i + 2] = tmpchar;
        }
      }break;
      case 32:{
        char *ptr = C.ptr;
        uint32_t ptrSize = C.dsize;
        for (uint32_t i = 0; i < ptrSize; i += 4){
          char tmpchar = ptr[i];
          ptr[i] = ptr[i + 3];
          ptr[i + 3] = tmpchar;
          tmpchar = ptr[i + 1];
          ptr[i + 1] = ptr[i + 2];
          ptr[i + 2] = tmpchar;
        }
      }break;
      }
    }
    thisPacket.genericFill(C.time, C.offset, C.track, C.ptr, C.dsize,
                           C.bpos, C.key);
    thisTime = C.time;
  }

  void InputEBML::getNext(size_t idx){
    bool singleTrack = (idx != INVALID_TRACK_ID);
    size_t wantedID = singleTrack?M.getID(idx):0;
    // Make sure we empty our buffer first
    if (bufferedPacks && packBuf.size()){
      for (std::map<uint64_t, trackPredictor>::iterator it = packBuf.begin(); it != packBuf.end(); ++it){
        trackPredictor &TP = it->second;
        if (TP.hasPackets()){
          size_t tIdx = M.trackIDToIndex(it->first, getpid());
          if (tIdx != INVALID_TRACK_ID) {
            packetData & C = TP.getPacketData(M.getType(tIdx) == "video");
            fillPacket(C);
          }
          TP.remove();
          --bufferedPacks;
          if (singleTrack && it->first != wantedID){getNext(idx);}
          return;
        }
      }
    }
    EBML::Block B;
    if (wantBlocks){
      do{
        if (!config->is_active){return;}
        if (!readElement()){
          // Make sure we empty our buffer first
          if (bufferedPacks && packBuf.size()){
            for (std::map<uint64_t, trackPredictor>::iterator it = packBuf.begin(); it != packBuf.end(); ++it){
              trackPredictor &TP = it->second;
              if (TP.hasPackets(true)){
                size_t tIdx = M.trackIDToIndex(it->first, getpid());
                if (tIdx != INVALID_TRACK_ID) {
                  packetData & C = TP.getPacketData(M.getType(tIdx) == "video");
                  fillPacket(C);
                }
                TP.remove();
                --bufferedPacks;
                if (singleTrack && it->first != wantedID){getNext(idx);}
                return;
              }
            }
          }
          // No more buffer? Set to empty
          thisPacket.null();
          return;
        }
        B = EBML::Block(readBuffer + readBufferOffset);
      }while (!B || B.getType() != EBML::ELEM_BLOCK ||
               (singleTrack && wantedID != B.getTrackNum()));
    }else{
      B = EBML::Block(readBuffer + readBufferOffset);
    }

    uint64_t tNum = B.getTrackNum();
    uint64_t newTime = lastClusterTime + B.getTimecode();
    trackPredictor &TP = packBuf[tNum];
    thisIdx = M.trackIDToIndex(tNum, getpid());
    bool isVideo = (thisIdx != INVALID_TRACK_ID) && (M.getType(thisIdx) == "video");
    bool isAudio = (thisIdx != INVALID_TRACK_ID) && (M.getType(thisIdx) == "audio");
    bool isASS = (thisIdx != INVALID_TRACK_ID) && (M.getCodec(thisIdx) == "subtitle" && M.getInit(thisIdx).size());

    // If this is a new video keyframe, flush the corresponding trackPredictor
    if (isVideo && B.isKeyframe() && bufferedPacks){
      if (TP.hasPackets(true)){
        wantBlocks = false;
        packetData &C = TP.getPacketData(true);
        fillPacket(C);
        TP.remove();
        --bufferedPacks;
        if (singleTrack && thisIdx != idx){getNext(idx);}
        return;
      }
    }
    if (isVideo && B.isKeyframe()){TP.flush();}
    wantBlocks = true;

    for (uint64_t frameNo = 0; frameNo < B.getFrameCount(); ++frameNo){
      if (frameNo){
        if ((thisIdx != INVALID_TRACK_ID) && M.getCodec(thisIdx) == "AAC"){
          newTime += (uint64_t)(1000000 / M.getRate(thisIdx)) / timeScale; // assume ~1000 samples per frame
        }else if ((thisIdx != INVALID_TRACK_ID) && M.getCodec(thisIdx) == "MP3"){
          newTime += (uint64_t)(1152000 / M.getRate(thisIdx)) / timeScale; // 1152 samples per frame
        }else if ((thisIdx != INVALID_TRACK_ID) && M.getCodec(thisIdx) == "DTS"){
          // Assume 512 samples per frame (DVD default)
          // actual amount can be calculated from data, but data
          // is not available during header generation...
          // See: http://www.stnsoft.com/DVD/dtshdr.html
          newTime += (uint64_t)(512000 / M.getRate(thisIdx)) / timeScale;
        }else{
          ERROR_MSG("Unknown frame duration for codec %s - timestamps WILL be wrong!",
                    (thisIdx != INVALID_TRACK_ID) ? M.getCodec(thisIdx).c_str() : "UNKNOWN");
        }
      }
      uint32_t frameSize = B.getFrameSize(frameNo);
      if (frameSize){
        char *ptr = (char *)B.getFrameData(frameNo);
        if (isASS){
          std::string assStr = ASStoSRT(ptr, frameSize);
          frameSize = assStr.size();
          memcpy(ptr, assStr.data(), frameSize);
        }
        if (frameSize){
          TP.add(newTime * timeScale, tNum, frameSize, lastClusterBPos,
                 B.isKeyframe() && !isAudio, isVideo, (void *)ptr);
          ++bufferedPacks;
        }
      }
    }
    if (TP.hasPackets()){
      packetData &C = TP.getPacketData(isVideo);
      if (thisIdx != INVALID_TRACK_ID) { fillPacket(C); }
      TP.remove();
      --bufferedPacks;
      if ((thisIdx == INVALID_TRACK_ID) || (singleTrack && thisIdx != idx)) { getNext(idx); }
    }else{
      // We didn't set thisPacket yet. Read another.
      // Recursing is fine, this can only happen a few times in a row.
      getNext(idx);
    }
  }

  void InputEBML::seek(uint64_t seekTime, size_t idx){
    wantBlocks = true;
    clearPredictors();
    bufferedPacks = 0;
    uint64_t mainTrack = M.mainTrack();

    DTSC::Keys keys(M.keys(mainTrack));
    DTSC::Parts parts(M.parts(mainTrack));
    uint64_t seekPos = keys.getBpos(0);
    // Replay the parts of the previous keyframe, so the timestaps match up
    for (size_t i = 0; i < keys.getEndValid(); i++){
      if (keys.getTime(i) > seekTime){break;}
      DONTEVEN_MSG("Seeking to %" PRIu64 ", found %" PRIu64 "...", seekTime, keys.getTime(i));
      seekPos = keys.getBpos(i);
    }


    firstRead = true;
    if (readPos > seekPos || seekPos > readPos + readBuffer.size() + 4*1024*1024){
      readBuffer.truncate(0);
      readBufferOffset = 0;
      if (!inFile.seek(seekPos)){
        FAIL_MSG("Seek to %" PRIu64 " failed! Aborting load", seekPos);
      }
      readPos = inFile.getPos();
    }else{
      while (seekPos > readPos + readBuffer.size() && config->is_active){
        size_t preSize = readBuffer.size();
        inFile.readSome(seekPos - (readPos + readBuffer.size()), *this);
        if (readBuffer.size() == preSize){
          Util::sleep(5);
        }
      }
      if (seekPos > readPos + readBuffer.size()){
        Util::logExitReason(ER_READ_START_FAILURE, "Input file seek abort");
        config->is_active = false;
        readBufferOffset = 0;
        return;
      }
      readBufferOffset = seekPos - readPos;
    }


  }

  /// Flushes all trackPredictors without deleting permanent data from them.
  void InputEBML::clearPredictors(){
    if (!packBuf.size()){return;}
    for (std::map<uint64_t, trackPredictor>::iterator it = packBuf.begin(); it != packBuf.end(); ++it){
      it->second.flush();
    }
  }

}// namespace Mist
