#include "ebml_socketglue.h"

#include "bitfields.h"

namespace EBML{

  void sendUniInt(Socket::Connection &C, const uint64_t val){
    uint8_t wSize = UniInt::writeSize(val);
    if (!wSize){
      C.SendNow("\377"); // Unknown size, all ones.
      return;
    }
    char tmp[8];
    UniInt::writeInt(tmp, val);
    C.SendNow(tmp, wSize);
  }

  uint32_t sizeElemHead(uint32_t ID, const uint64_t size){
    uint8_t sLen = UniInt::writeSize(size);
    return UniInt::writeSize(ID) + (sLen ? sLen : 1);
  }

  uint8_t sizeUInt(const uint64_t val){
    if (val >= 0x100000000000000ull){
      return 8;
    }else if (val >= 0x1000000000000ull){
      return 7;
    }else if (val >= 0x10000000000ull){
      return 6;
    }else if (val >= 0x100000000ull){
      return 5;
    }else if (val >= 0x1000000ull){
      return 4;
    }else if (val >= 0x10000ull){
      return 3;
    }else if (val >= 0x100ull){
      return 2;
    }
    return 1;
  }

  uint32_t sizeElemUInt(uint32_t ID, const uint64_t val){
    uint8_t iSize = sizeUInt(val);
    return sizeElemHead(ID, iSize) + iSize;
  }

  uint8_t sizeInt(const int64_t val){
    if (val >= 0x100000000000000ll || val <= -0x100000000000000ll){
      return 8;
    }else if (val >= 0x1000000000000ll || val <= -0x1000000000000ll){
      return 7;
    }else if (val >= 0x10000000000ll || val <= -0x10000000000ll){
      return 6;
    }else if (val >= 0x100000000ll || val <= -0x100000000ll){
      return 5;
    }else if (val >= 0x1000000ll || val <= -0x1000000ll){
      return 4;
    }else if (val >= 0x10000ll || val <= -0x10000ll){
      return 3;
    }else if (val >= 0x100ll || val <= -0x100ll){
      return 2;
    }
    return 1;
  }

  uint32_t sizeElemInt(uint32_t ID, const int64_t val){
    uint8_t iSize = sizeInt(val);
    return sizeElemHead(ID, iSize) + iSize;
  }

  uint32_t sizeElemID(uint32_t ID, const uint64_t val){
    uint8_t iSize = UniInt::writeSize(val);
    return sizeElemHead(ID, iSize) + iSize;
  }

  uint32_t sizeElemDbl(uint32_t ID, const double val){
    uint8_t iSize = (val == (float)val) ? 4 : 8;
    return sizeElemHead(ID, iSize) + iSize;
  }

  uint32_t sizeElemStr(uint32_t ID, const std::string &val){
    return sizeElemHead(ID, val.size()) + val.size();
  }

  void sendElemHead(Socket::Connection &C, uint32_t ID, const uint64_t size){
    sendUniInt(C, ID);
    sendUniInt(C, size);
  }

  void sendElemUInt(Socket::Connection &C, uint32_t ID, const uint64_t val){
    char tmp[8];
    uint8_t wSize = sizeUInt(val);
    switch (wSize){
    case 8: Bit::htobll(tmp, val); break;
    case 7: Bit::htob56(tmp, val); break;
    case 6: Bit::htob48(tmp, val); break;
    case 5: Bit::htob40(tmp, val); break;
    case 4: Bit::htobl(tmp, val); break;
    case 3: Bit::htob24(tmp, val); break;
    case 2: Bit::htobs(tmp, val); break;
    case 1: tmp[0] = val; break;
    }
    sendElemHead(C, ID, wSize);
    C.SendNow(tmp, wSize);
  }

  void sendElemInt(Socket::Connection &C, uint32_t ID, const int64_t val){
    char tmp[8];
    uint8_t wSize = sizeInt(val);
    switch (wSize){
    case 8: Bit::htobll(tmp, val); break;
    case 7: Bit::htob56(tmp, val); break;
    case 6: Bit::htob48(tmp, val); break;
    case 5: Bit::htob40(tmp, val); break;
    case 4: Bit::htobl(tmp, val); break;
    case 3: Bit::htob24(tmp, val); break;
    case 2: Bit::htobs(tmp, val); break;
    case 1: tmp[0] = val; break;
    }
    sendElemHead(C, ID, wSize);
    C.SendNow(tmp, wSize);
  }

  void sendElemID(Socket::Connection &C, uint32_t ID, const uint64_t val){
    uint8_t wSize = UniInt::writeSize(val);
    sendElemHead(C, ID, wSize);
    sendUniInt(C, val);
  }

  void sendElemDbl(Socket::Connection &C, uint32_t ID, const double val){
    char tmp[8];
    uint8_t wSize = (val == (float)val) ? 4 : 8;
    switch (wSize){
    case 4: Bit::htobf(tmp, val); break;
    case 8: Bit::htobd(tmp, val); break;
    }
    sendElemHead(C, ID, wSize);
    C.SendNow(tmp, wSize);
  }

  void sendElemStr(Socket::Connection &C, uint32_t ID, const std::string &val){
    sendElemHead(C, ID, val.size());
    C.SendNow(val);
  }

  void sendElemEBML(Socket::Connection &C, const std::string &doctype){
    sendElemHead(C, EID_EBML, 27 + doctype.size());
    sendElemUInt(C, EID_EBMLVERSION, 1);
    sendElemUInt(C, EID_EBMLREADVERSION, 1);
    sendElemUInt(C, EID_EBMLMAXIDLENGTH, 4);
    sendElemUInt(C, EID_EBMLMAXSIZELENGTH, 8);
    sendElemStr(C, EID_DOCTYPE, doctype);
    if (doctype == "matroska"){
      sendElemUInt(C, EID_DOCTYPEVERSION, 4);
      sendElemUInt(C, EID_DOCTYPEREADVERSION, 1);
    }else{
      sendElemUInt(C, EID_DOCTYPEVERSION, 1);
      sendElemUInt(C, EID_DOCTYPEREADVERSION, 1);
    }
  }

  void sendElemInfo(Socket::Connection &C, const std::string &appName, double duration, int64_t date){
    size_t contentLen = 13 + 2 * appName.size();
    if (duration > 0){
      contentLen += sizeElemDbl(EID_DURATION, duration);
    }
    if (date){
      date -= 978307200000ll;
      date *= 1000000;
      contentLen += sizeElemInt(EID_DATEUTC, date);
    }
    sendElemHead(C, EID_INFO, contentLen);
    sendElemUInt(C, EID_TIMECODESCALE, 1000000);
    if (duration > 0){sendElemDbl(C, EID_DURATION, duration);}
    if (date){sendElemInt(C, EID_DATEUTC, date);}
    sendElemStr(C, EID_MUXINGAPP, appName);
    sendElemStr(C, EID_WRITINGAPP, appName);
  }

  uint32_t sizeElemEBML(const std::string &doctype){
    return 27 + doctype.size() + sizeElemHead(EID_EBML, 27 + doctype.size());
  }

  uint32_t sizeElemInfo(const std::string &appName, double duration, int64_t date){
    size_t contentLen = 13 + 2 * appName.size();
    if (duration > 0){
      contentLen += sizeElemDbl(EID_DURATION, duration);
    }
    if (date){
      date -= 978307200000ll;
      date *= 1000000;
      contentLen += sizeElemInt(EID_DATEUTC, date);
    }
    return contentLen + sizeElemHead(EID_INFO, contentLen);
  }

  void sendSimpleBlock(Socket::Connection & C, const char *dataPointer, const size_t dataLen, size_t trackId,
                       uint64_t time, bool keyFrame, uint64_t clusterTime) {
    uint32_t blockSize = UniInt::writeSize(trackId) + 3 + dataLen;
    sendElemHead(C, EID_SIMPLEBLOCK, blockSize);
    sendUniInt(C, trackId);
    char blockHead[3] ={0, 0, 0};
    if (keyFrame) { blockHead[2] = 0x80; }
    Bit::htobs(blockHead, (int16_t)(time - clusterTime));
    C.SendNow(blockHead, 3);
    C.SendNow(dataPointer, dataLen);
  }

  uint32_t sizeSimpleBlock(uint64_t trackId, uint32_t dataSize){
    uint32_t ret = UniInt::writeSize(trackId) + 3 + dataSize;
    return ret + sizeElemHead(EID_SIMPLEBLOCK, ret);
  }

  void sendElemSeek(Socket::Connection &C, uint32_t ID, uint64_t bytePos){
    uint32_t elems = sizeElemUInt(EID_SEEKID, ID) + sizeElemUInt(EID_SEEKPOSITION, bytePos);
    sendElemHead(C, EID_SEEK, elems);
    sendElemID(C, EID_SEEKID, ID);
    sendElemUInt(C, EID_SEEKPOSITION, bytePos);
  }

  uint32_t sizeElemSeek(uint32_t ID, uint64_t bytePos){
    uint32_t elems = sizeElemID(EID_SEEKID, ID) + sizeElemUInt(EID_SEEKPOSITION, bytePos);
    return sizeElemHead(EID_SEEK, elems) + elems;
  }

  void sendElemCuePoint(Socket::Connection &C, uint64_t time, uint64_t track, uint64_t clusterPos, uint64_t relaPos){
    uint32_t elemsA = 0, elemsB = 0;
    elemsA += sizeElemUInt(EID_CUETRACK, track);
    elemsA += sizeElemUInt(EID_CUECLUSTERPOSITION, clusterPos);
    elemsA += sizeElemUInt(EID_CUERELATIVEPOSITION, relaPos);
    elemsB = elemsA + sizeElemUInt(EID_CUETIME, time) + sizeElemHead(EID_CUETRACKPOSITIONS, elemsA);
    sendElemHead(C, EID_CUEPOINT, elemsB);
    sendElemUInt(C, EID_CUETIME, time);
    sendElemHead(C, EID_CUETRACKPOSITIONS, elemsA);
    sendElemUInt(C, EID_CUETRACK, track);
    sendElemUInt(C, EID_CUECLUSTERPOSITION, clusterPos);
    sendElemUInt(C, EID_CUERELATIVEPOSITION, relaPos);
  }

  uint32_t sizeElemCuePoint(uint64_t time, uint64_t track, uint64_t clusterPos, uint64_t relaPos){
    uint32_t elems = 0;
    elems += sizeElemUInt(EID_CUETRACK, track);
    elems += sizeElemUInt(EID_CUECLUSTERPOSITION, clusterPos);
    elems += sizeElemUInt(EID_CUERELATIVEPOSITION, relaPos);
    elems += sizeElemHead(EID_CUETRACKPOSITIONS, elems);
    elems += sizeElemUInt(EID_CUETIME, time);
    return sizeElemHead(EID_CUEPOINT, elems) + elems;
  }

  bool parseTrackEntry(const Element & E, DTSC::Meta & meta) {
    EBML::Element tmpElem = E.findChild(EBML::EID_TRACKNUMBER);
    if (!tmpElem) {
      ERROR_MSG("Track without track number encountered, ignoring");
      return false;
    }
    uint64_t trackID = tmpElem.getValUInt();
    tmpElem = E.findChild(EBML::EID_CODECID);
    if (!tmpElem) {
      ERROR_MSG("Track without codec id encountered, ignoring");
      return false;
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
      tmpElem = E.findChild(EBML::EID_CODECPRIVATE);
      if (tmpElem) { init = tmpElem.getValStringUntrimmed(); }
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
    if (codec == "V_UNCOMPRESSED") {
      tmpElem = E.findChild(EBML::EID_UNCOMPRESSEDFOURCC);
      if (tmpElem) {
        std::string fourcc = tmpElem.getValStringUntrimmed();
        if (fourcc == "UYVY" || fourcc == "NV12" || fourcc == "YUYV") {
          trueCodec = fourcc;
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
      return false;
    }
    tmpElem = E.findChild(EBML::EID_LANGUAGE);
    if (tmpElem) { lang = tmpElem.getValString(); }
    size_t idx = meta.trackIDToIndex(trackID, getpid());
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
    INFO_MSG("Detected track %" PRIu64 " => %zu: %s", trackID, idx, meta.getTrackIdentifier(idx).c_str());
    return true;
  }

  bool trackPredictor::hasPackets() {
    if (finished != INVALID_TRACK_ID && finished > rem) { return true; }
    return (initialized || ctr > 16) && ctr - rem > maxDelay;
  }

  void trackPredictor::finish() {
    finished = ctr;
  }

  /// Clears all internal values, for reuse as-new.
  void trackPredictor::flush() {
    ctr = 0;
    rem = 0;
    finished = INVALID_TRACK_ID;
  }

  packetData & trackPredictor::getPacketData(bool mustCalcOffsets) {
    // grab the next packet to output
    packetData & p = pkts[rem % PKT_COUNT];
    if (!mustCalcOffsets || !maxDelay) {
      initialized = true;
      return p;
    }
    // Calculate the timeOffset when extracting the first frame
    if (!initialized) {
      size_t buffLen = (ctr - rem - 1) % PKT_COUNT;
      for (size_t i = 0; i <= buffLen; ++i) {
        if (pkts[i].time < times[i]) {
          if (times[i] - pkts[i].time > timeOffset) { timeOffset = times[i] - pkts[i].time; }
        }
        DONTEVEN_MSG("Checking time offset against entry %zu/%zu: %" PRIu64 "-%" PRIu64 " = %" PRIu32, i, buffLen,
                     times[i], pkts[i].time, timeOffset);
      }
      MEDIUM_MSG("timeOffset calculated to be %" PRIu32 ", max frame delay %zu", timeOffset, maxDelay);
      initialized = true;
    }

    uint64_t origTime = p.time;
    // Set new timestamp to first time in sorted array
    p.time = times[0];
    // Subtract timeOffset if possible
    if (p.time >= timeOffset) { p.time -= timeOffset; }
    // If possible, calculate offset based on original timestamp difference with new timestamp
    if (origTime > p.time) { p.offset = origTime - p.time; }
    // Less than 3 milliseconds off? Assume we needed 0 and it's a rounding error in timestamps.
    if (p.offset < 3) { p.offset = 0; }
    DONTEVEN_MSG("Outputting%s %" PRIu64 "+%" PRIu64 " (#%" PRIu64 "), display at %" PRIu64, p.key ? " KEY" : "",
                 p.time, p.offset, rem, p.time + p.offset);
    return p;
  }

  void trackPredictor::add(uint64_t packTime, uint64_t packTrack, uint64_t packDataSize, uint64_t packBytePos,
                           bool isKeyframe, bool isVideo, void *dataPtr) {
    pkts[ctr % PKT_COUNT].set(packTime, 0, packTrack, packDataSize, packBytePos, isKeyframe, dataPtr);
    ++ctr;
    if (!isVideo) { return; }
    size_t buffLen = ctr - rem - 1;
    // Just in case somebody messed up, ensure we don't go out of our PKT_COUNT sized array
    if (buffLen >= PKT_COUNT) { buffLen = PKT_COUNT - 1; }
    times[buffLen] = packTime;
    if (buffLen) {
      // Swap the times while the previous is higher than the current
      size_t i = buffLen;
      while (i && times[i] < times[i - 1]) {
        uint64_t tmp = times[i - 1];
        times[i - 1] = times[i];
        times[i] = tmp;
        --i;
        // Keep track of maximum delay
        if (!initialized && buffLen - i + 1 > maxDelay) { maxDelay = buffLen - i + 1; }
      }
    }
  }

  void trackPredictor::remove() {
    ++rem;
    size_t buffLen = ctr - rem;
    if (buffLen >= PKT_COUNT) { buffLen = PKT_COUNT - 1; }
    for (size_t i = 0; i < buffLen; ++i) { times[i] = times[i + 1]; }
  }

  /// Sets whether or not packet data is included, or only data about the packets
  void toDTSC::enableData(bool enable) {
    withData = enable;
  }

  void toDTSC::parseElement(const Element & E, const uint64_t bpos, DTSC::Meta & meta) {
    lastClusterBPos = bpos;
    if (E.getID() == EBML::EID_TIMECODE) {
      lastClusterTime = E.getValUInt();
      DONTEVEN_MSG("Cluster time %" PRIu64 " ms", lastClusterTime);
      return;
    }

    if (E.getID() == EBML::EID_TRACKENTRY) {
      EBML::parseTrackEntry(E, meta);
      return;
    }
    if (E.getID() == EBML::EID_TIMECODESCALE) {
      uint64_t timeScaleVal = E.getValUInt();
      meta.inputLocalVars["timescale"] = timeScaleVal;
      timeScale = ((double)timeScaleVal) / 1000000.0;
      return;
    }
    if (E.getID() == EBML::EID_DATEUTC) {
      dateVal = E.getValDate();
      return;
    }
    if (E.getType() == EBML::ELEM_BLOCK) {
      // Set timeScale if unset
      if (!timeScale && meta.inputLocalVars.isMember("timescale")) {
        timeScale = ((double)meta.inputLocalVars["timescale"].asInt()) / 1000000.0;
      }
      if (!timeScale) {
        FAIL_MSG("Timescale not set - cannot parse EBML block!");
        return;
      }

      EBML::Block B(E);
      parseBlock(B, meta);
    }
  }

  /// Local-only helper function that converts ASS format subtitles to SRT format subtitles
  static std::string ASStoSRT(const char *ptr, uint32_t len) {
    uint16_t commas = 0;
    uint16_t brackets = 0;
    std::string tmpStr;
    tmpStr.reserve(len);
    for (uint32_t i = 0; i < len; ++i) {
      // Skip everything until the 8th comma
      if (commas < 8) {
        if (ptr[i] == ',') { commas++; }
        continue;
      }
      if (ptr[i] == '{') {
        brackets++;
        continue;
      }
      if (ptr[i] == '}') {
        brackets--;
        continue;
      }
      if (!brackets) {
        if (ptr[i] == '\\' && i < len - 1 && (ptr[i + 1] == 'N' || ptr[i + 1] == 'n')) {
          tmpStr += '\n';
          ++i;
          continue;
        }
        tmpStr += ptr[i];
      }
    }
    return tmpStr;
  }

  void toDTSC::parseBlock(const Block & B, const DTSC::Meta & M) {
    uint64_t tNum = B.getTrackNum();
    uint64_t newTime = lastClusterTime + B.getTimecode();
    EBML::trackPredictor & TP = packBuf[tNum];
    size_t idx = M.trackIDToIndex(tNum, getpid());
    bool isVideo = (M.getType(idx) == "video");
    bool isAudio = (M.getType(idx) == "audio");
    bool isASS = (M.getCodec(idx) == "subtitle" && M.getInit(idx).size());
    // If this is a new video keyframe, finish the corresponding trackPredictor
    if (isVideo && B.isKeyframe()) { TP.finish(); }
    for (uint64_t frameNo = 0; frameNo < B.getFrameCount(); ++frameNo) {
      if (frameNo) {
        if (M.getCodec(idx) == "AAC") {
          newTime += (uint64_t)(1000000 / M.getRate(idx)) / timeScale; // assume ~1000 samples per frame
        } else if (M.getCodec(idx) == "MP3") {
          newTime += (uint64_t)(1152000 / M.getRate(idx)) / timeScale; // 1152 samples per frame
        } else if (M.getCodec(idx) == "DTS") {
          // Assume 512 samples per frame (DVD default)
          // actual amount can be calculated from data, but data
          // is not available during header generation...
          // See: http://www.stnsoft.com/DVD/dtshdr.html
          newTime += (uint64_t)(512000 / M.getRate(idx)) / timeScale;
        } else {
          newTime += 1 / timeScale;
          ERROR_MSG("Unknown frame duration for codec %s - timestamps WILL be wrong!", M.getCodec(idx).c_str());
        }
      }
      uint32_t frameSize = B.getFrameSize(frameNo);
      if (frameSize) {
        char *ptr = (char *)B.getFrameData(frameNo);
        std::string assStr;
        if (isASS) {
          assStr = ASStoSRT(ptr, frameSize);
          frameSize = assStr.size();
          ptr = (char *)assStr.data();
        }
        if (frameSize) {
          TP.add(newTime * timeScale, tNum, frameSize, lastClusterBPos, B.isKeyframe() && !isAudio, isVideo, withData ? ptr : 0);
          ++bufferedPacks;
        }
      }
    }
  }

  void toDTSC::flush() {
    for (auto & it : packBuf) { it.second.flush(); }
  }

  void toDTSC::finish() {
    for (auto & it : packBuf) { it.second.finish(); }
  }

  void toDTSC::postHeader(DTSC::Meta & meta) {
    // Record PCMLE tracks as being PCM with swapped endianness
    std::set<size_t> validTracks = meta.getMySourceTracks(getpid());
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++) {
      if (meta.getCodec(*it) == "PCMLE") {
        meta.setCodec(*it, "PCM");
        swapEndianness.insert(*it);
      }
    }
  }

  bool toDTSC::hasPackets() {
    if (bufferedPacks && packBuf.size()) {
      for (std::map<uint64_t, EBML::trackPredictor>::iterator it = packBuf.begin(); it != packBuf.end(); ++it) {
        if (it->second.hasPackets()) { return true; }
      }
    }
    return false;
  }

  bool toDTSC::fillPacket(const DTSC::Meta & M, size_t & thisIdx, uint64_t & thisTime, DTSC::Packet & thisPacket) {
    if (bufferedPacks && packBuf.size()) {
      for (auto & it : packBuf) {
        EBML::trackPredictor & TP = it.second;
        if (TP.hasPackets()) {
          thisIdx = M.trackIDToIndex(it.first, getpid());
          EBML::packetData & C = TP.getPacketData(M.getType(thisIdx) == "video");

          if (swapEndianness.count(C.track)) {
            switch (M.getSize(thisIdx)) {
              case 16: {
                char *ptr = C.ptr;
                uint32_t ptrSize = C.dsize;
                for (uint32_t i = 0; i < ptrSize; i += 2) {
                  char tmpchar = ptr[i];
                  ptr[i] = ptr[i + 1];
                  ptr[i + 1] = tmpchar;
                }
              } break;
              case 24: {
                char *ptr = C.ptr;
                uint32_t ptrSize = C.dsize;
                for (uint32_t i = 0; i < ptrSize; i += 3) {
                  char tmpchar = ptr[i];
                  ptr[i] = ptr[i + 2];
                  ptr[i + 2] = tmpchar;
                }
              } break;
              case 32: {
                char *ptr = C.ptr;
                uint32_t ptrSize = C.dsize;
                for (uint32_t i = 0; i < ptrSize; i += 4) {
                  char tmpchar = ptr[i];
                  ptr[i] = ptr[i + 3];
                  ptr[i + 3] = tmpchar;
                  tmpchar = ptr[i + 1];
                  ptr[i + 1] = ptr[i + 2];
                  ptr[i + 2] = tmpchar;
                }
              } break;
            }
          }
          thisPacket.genericFill(C.time, C.offset, C.track, C.ptr, C.dsize, C.bpos, C.key);
          thisTime = C.time;
          TP.remove();
          --bufferedPacks;
          return true;
        }
      }
    }
    return false;
  }

  void toDTSC::fillPacketData(DTSC::Meta & meta) {
    if (bufferedPacks && packBuf.size()) {
      for (auto & it : packBuf) {
        EBML::trackPredictor & TP = it.second;
        size_t thisIdx = meta.trackIDToIndex(it.first, getpid());
        while (TP.hasPackets()) {
          EBML::packetData & C = TP.getPacketData(meta.getType(thisIdx) == "video");
          meta.update(C.time, C.offset, thisIdx, C.dsize, C.bpos, C.key);
          if (dateVal) {
            meta.setUTCOffset(dateVal - C.time, UTCSRC_PROTOCOL);
            if (!meta.getUTCOffset()) { meta.setUTCOffset(1, UTCSRC_PROTOCOL); }
            dateVal = 0;
          }
          TP.remove();
          --bufferedPacks;
        }
      }
    }
  }

}// namespace EBML
