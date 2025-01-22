#include "ebml_socketglue.h"

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

  void sendSimpleBlock(Socket::Connection &C, DTSC::Packet &pkt, uint64_t clusterTime, bool forceKeyframe){
    size_t dataLen = 0;
    char *dataPointer = 0;
    pkt.getString("data", dataPointer, dataLen);
    uint32_t blockSize = UniInt::writeSize(pkt.getTrackId()) + 3 + dataLen;
    sendElemHead(C, EID_SIMPLEBLOCK, blockSize);
    sendUniInt(C, pkt.getTrackId());
    char blockHead[3] ={0, 0, 0};
    if (pkt.hasMember("keyframe") || forceKeyframe){blockHead[2] = 0x80;}
    int offset = 0;
    if (pkt.hasMember("offset")){offset = pkt.getInt("offset");}
    Bit::htobs(blockHead, (int16_t)(pkt.getTime() + offset - clusterTime));
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

}// namespace EBML
