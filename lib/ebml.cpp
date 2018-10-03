#include "ebml.h"
#include "bitfields.h"
#include "defines.h"
#include <iomanip>
#include <sstream>

namespace EBML{

  /// Reads the size of an EBML-encoded integer from a pointer
  uint8_t UniInt::readSize(const char *p){
    if (p[0] & 0x80){return 1;}
    if (p[0] & 0x40){return 2;}
    if (p[0] & 0x20){return 3;}
    if (p[0] & 0x10){return 4;}
    if (p[0] & 0x08){return 5;}
    if (p[0] & 0x04){return 6;}
    if (p[0] & 0x02){return 7;}
    if (p[0] & 0x01){return 8;}
    return 1;
  }

  /// Returns the size of an EBML-encoded integer for a given numerical value
  uint8_t UniInt::writeSize(const uint64_t val){
    if (val <= 0x7Eull){return 1;}
    if (val <= 0x3FFEull){return 2;}
    if (val <= 0x1FFFFEull){return 3;}
    if (val <= 0xFFFFFFEull){return 4;}
    if (val <= 0x7FFFFFFFEull){return 5;}
    if (val <= 0x3FFFFFFFFFEull){return 6;}
    if (val <= 0x1FFFFFFFFFFFEull){return 7;}
    if (val <= 0xFFFFFFFFFFFFFEull){return 8;}
    return 0;
  }

  /// Reads an EBML-encoded integer from a pointer. Expects the whole number to be readable without
  /// bounds checking.
  uint64_t UniInt::readInt(const char *p){
    switch (readSize(p)){
    case 1:
      if (p[0] == 0xFF){
        return 0xFFFFFFFFFFFFFFFFull;
      }else{
        return p[0] & 0x7F;
      }
    case 2: return Bit::btohs(p) & 0x3FFFull;
    case 3: return Bit::btoh24(p) & 0x1FFFFFull;
    case 4: return Bit::btohl(p) & 0xFFFFFFFull;
    case 5: return Bit::btoh40(p) & 0x7FFFFFFFFull;
    case 6: return Bit::btoh48(p) & 0x3FFFFFFFFFFull;
    case 7: return Bit::btoh56(p) & 0x1FFFFFFFFFFFFull;
    case 8: return Bit::btohll(p) & 0xFFFFFFFFFFFFFFull;
    }
    return 0;
  }

  void UniInt::writeInt(char *p, const uint64_t val){
    switch (writeSize(val)){
    case 1: p[0] = val | 0x80; break;
    case 2: Bit::htobs(p, val | 0x4000ull); break;
    case 3: Bit::htob24(p, val | 0x200000ull); break;
    case 4: Bit::htobl(p, val | 0x10000000ull); break;
    case 5: Bit::htob40(p, val | 0x800000000ull); break;
    case 6: Bit::htob48(p, val | 0x40000000000ull); break;
    case 7: Bit::htob56(p, val | 0x2000000000000ull); break;
    case 8: Bit::htobll(p, val | 0x100000000000000ull); break;
    }
  }

  /// Reads an EBML-encoded singed integer from a pointer. Expects the whole number to be readable without
  /// bounds checking.
  int64_t UniInt::readSInt(const char *p){
    switch (readSize(p)){
    case 1: return ((int64_t)readInt(p)) - 0x3Fll;
    case 2: return ((int64_t)readInt(p)) - 0x1FFFll;
    case 3: return ((int64_t)readInt(p)) - 0xFFFFFll;
    case 4: return ((int64_t)readInt(p)) - 0x7FFFFFFll;
    case 5: return ((int64_t)readInt(p)) - 0x3FFFFFFFFll;
    case 6: return ((int64_t)readInt(p)) - 0x1FFFFFFFFFFll;
    case 7: return ((int64_t)readInt(p)) - 0xFFFFFFFFFFFFll;
    case 8: return ((int64_t)readInt(p)) - 0x7FFFFFFFFFFFFFll;
    }
    return 0;
  }

  void UniInt::writeSInt(char *p, const int64_t sval){
    FAIL_MSG("Writing signed UniInt values not yet implemented!");
  }

  /// Given a pointer and available byte count, returns how many bytes must be available for more
  /// data to be readable.
  /// If minimal is true, returns only the header size if the element is an ELEM_MASTER type.
  uint64_t Element::needBytes(const char *p, uint64_t availBytes, bool minimal){
    if (availBytes < 2){return 2;}
    uint64_t needed = UniInt::readSize(p);
    if (availBytes < needed + 1){return needed + 1;}
    const char *sizeOffset = p + needed;
    needed += UniInt::readSize(sizeOffset);
    if (availBytes < needed){return needed;}
    // ELEM_MASTER types do not contain payload if minimal is true
    if (minimal && Element(p, true).getType() == ELEM_MASTER){return needed;}
    uint64_t pSize = UniInt::readInt(sizeOffset);
    if (pSize != 0xFFFFFFFFFFFFFFFFull){
      needed += pSize;
    }
    return needed;
  }

  std::string Element::getIDString(uint32_t id){
    if (id > 0xFFFFFF){
      id &= 0xFFFFFFF;
    }else{
      if (id > 0xFFFF){
        id &= 0x1FFFFF;
      }else{
        if (id > 0xFF){
          id &= 0x3FFF;
        }else{
          id &= 0x7F;
        }
      }
    }
    switch (id){
    case EID_EBML: return "EBML";
    case EID_SEGMENT: return "Segment";
    case EID_CLUSTER: return "Cluster";
    case EID_TIMECODE: return "Timecode";
    case 0x20: return "BlockGroup";
    case 0x21: return "Block";
    case EID_SIMPLEBLOCK: return "SimpleBlock";
    case 0x35A2: return "DiscardPadding";
    case EID_SEEKHEAD: return "SeekHead";
    case EID_SEEK: return "Seek";
    case EID_SEEKID: return "SeekID";
    case EID_SEEKPOSITION: return "SeekPosition";
    case EID_INFO: return "Info";
    case EID_TIMECODESCALE: return "TimecodeScale";
    case EID_MUXINGAPP: return "MuxingApp";
    case EID_WRITINGAPP: return "WritingApp";
    case EID_DURATION: return "Duration";
    case EID_TRACKS: return "Tracks";
    case EID_TRACKENTRY: return "TrackEntry";
    case EID_TRACKNUMBER: return "TrackNumber";
    case EID_TRACKUID: return "TrackUID";
    case EID_FLAGLACING: return "FlagLacing";
    case EID_LANGUAGE: return "Language";
    case EID_CODECID: return "CodecID";
    case EID_TRACKTYPE: return "TrackType";
    case EID_VIDEO: return "Video";
    case EID_PIXELWIDTH: return "PixelWidth";
    case EID_PIXELHEIGHT: return "PixelHeight";
    case 0x1A: return "FlagInterlaced";
    case EID_DISPLAYWIDTH: return "DisplayWidth";
    case EID_DISPLAYHEIGHT: return "DisplayHeight";
    case 0x15B0: return "Colour";
    case 0x15B7: return "ChromaSitingHorz";
    case 0x15B8: return "ChromaSitingVert";
    case 0x15BA: return "TransferCharacteristics";
    case 0x15B1: return "MatrixCoefficients";
    case 0x15BB: return "Primaries";
    case 0x15B9: return "Range";
    case 0x136E: return "Name";
    case 0x2DE7: return "MinCache";
    case EID_AUDIO: return "Audio";
    case EID_CHANNELS: return "Channels";
    case EID_SAMPLINGFREQUENCY: return "SamplingFrequency";
    case EID_BITDEPTH: return "BitDepth";
    case EID_CODECDELAY: return "CodecDelay";
    case EID_SEEKPREROLL: return "SeekPreRoll";
    case EID_CODECPRIVATE: return "CodecPrivate";
    case EID_DEFAULTDURATION: return "DefaultDuration";
    case EID_EBMLVERSION: return "EBMLVersion";
    case EID_EBMLREADVERSION: return "EBMLReadVersion";
    case EID_EBMLMAXIDLENGTH: return "EBMLMaxIDLength";
    case EID_EBMLMAXSIZELENGTH: return "EBMLMaxSizeLength";
    case EID_DOCTYPE: return "DocType";
    case EID_DOCTYPEVERSION: return "DocTypeVersion";
    case EID_DOCTYPEREADVERSION: return "DocTypeReadVersion";
    case EID_CUES: return "Cues";
    case EID_CUEPOINT: return "CuePoint";
    case EID_CUETIME: return "CueTime";
    case EID_CUETRACKPOSITIONS: return "CueTrackPositions";
    case EID_CUETRACK: return "CueTrack";
    case EID_CUECLUSTERPOSITION: return "CueClusterPosition";
    case EID_CUERELATIVEPOSITION: return "CueRelativePosition";
    case 0x6C: return "Void";
    case 0x3F: return "CRC-32";
    case 0x33A4: return "SegmentUID";
    case EID_TAGS: return "Tags";
    case 0x3373: return "Tag";
    case 0x23C0: return "Targets";
    case 0x27C8: return "SimpleTag";
    case 0x5A3: return "TagName";
    case 0x487: return "TagString";
    case 0x23C5: return "TagTrackUID";
    case 0x43a770: return "Chapters";
    case  0x3a770: return "Chapters";
    case 0x941a469: return "Attachments";
    case 0x8: return "FlagDefault";
    case 0x461: return "DateUTC";
    case 0x3BA9: return "Title";
    case 0x1B: return "BlockDuration";
    case 0x21A7: return "AttachedFile";
    case 0x66E: return "FileName";
    case 0x65C: return "FileData";
    case 0x6AE: return "FileUID";
    case 0x67E: return "FileDescription";
    case 0x660: return "FileMimeType";
    case 0x5B9: return "EditionEntry";
    case 0x5BD: return "EditionFlagHidden";
    case 0x5DB: return "EditionFlagDefault";
    case 0x5BC: return "EditionUID";
    case 0x36: return "ChapterAtom";
    case 0x33C4: return "ChapterUID";
    case 0x11: return "ChapterTimeStart";
    case 0x18: return "ChapterFlagHidden";
    case 0x598: return "ChapterFlagEnabled";
    case 0x0: return "ChapterDisplay";
    case 0x5: return "ChapString";
    case 0x37C: return "ChapLanguage";
    default:
      std::stringstream ret;
      ret << "UNKNOWN: 0x" << std::hex << std::setw(8) << std::setfill('0') << id;
      return ret.str();
    }
  }

  /// If minimal is set to true, ELEM_MASTER elements will never attempt to access their payload
  /// data.
  Element::Element(const char *p, bool minimal){
    data = p;
    minimalMode = minimal;
  }

  uint32_t Element::getID() const{return UniInt::readInt(data);}

  uint64_t Element::getPayloadLen() const{
    uint8_t sizeOffset = UniInt::readSize(data);
    return UniInt::readInt(data + sizeOffset);
  }

  uint8_t Element::getHeaderLen() const{
    uint8_t sizeOffset = UniInt::readSize(data);
    return sizeOffset + UniInt::readSize(data + sizeOffset);
  }

  const char *Element::getPayload() const{return data + getHeaderLen();}

  uint64_t Element::getOuterLen() const{
    uint8_t sizeOffset = UniInt::readSize(data);
    if (minimalMode && UniInt::readInt(data + sizeOffset) == 0xFFFFFFFFFFFFFFFFull){
      return sizeOffset + UniInt::readSize(data + sizeOffset);
    }else{
      return UniInt::readInt(data + sizeOffset) + sizeOffset + UniInt::readSize(data + sizeOffset);
    }
  }

  ElementType Element::getType() const{
    switch (getID()){
    case EID_EBML:
    case EID_SEGMENT:
    case EID_CLUSTER:
    case EID_SEEKHEAD:
    case EID_INFO:
    case EID_TRACKS:
    case EID_CUES:
    case EID_SEEK:
    case EID_TRACKENTRY:
    case EID_VIDEO:
    case EID_AUDIO:
    case 0x20:
    case EID_CUEPOINT:
    case EID_CUETRACKPOSITIONS:
    case 0x15B0:
    case EID_TAGS:
    case 0x3373:
    case 0x23C0:
    case 0x43a770:
    case  0x3a770:
    case 0x941a469:
    case 0x21A7:
    case 0x5B9:
    case 0x36:
    case 0x0:
    case 0x27C8: return ELEM_MASTER;
    case EID_EBMLVERSION:
    case EID_EBMLREADVERSION:
    case EID_EBMLMAXIDLENGTH:
    case EID_EBMLMAXSIZELENGTH:
    case EID_DOCTYPEVERSION:
    case EID_DOCTYPEREADVERSION:
    case EID_SEEKPOSITION:
    case EID_TIMECODESCALE:
    case EID_TIMECODE:
    case EID_TRACKNUMBER:
    case EID_TRACKUID:
    case EID_FLAGLACING:
    case EID_TRACKTYPE:
    case EID_DEFAULTDURATION:
    case EID_CODECDELAY:
    case EID_SEEKPREROLL:
    case EID_CUETIME:
    case EID_CUETRACK:
    case EID_CUECLUSTERPOSITION:
    case EID_CUERELATIVEPOSITION:
    case EID_PIXELWIDTH:
    case EID_PIXELHEIGHT:
    case 0x1A:
    case EID_DISPLAYWIDTH:
    case EID_DISPLAYHEIGHT:
    case EID_CHANNELS:
    case EID_BITDEPTH:
    case 0x15B7:
    case 0x15B8:
    case 0x15BA:
    case 0x15B9:
    case 0x15B1:
    case 0x15BB:
    case 0x2DE7:
    case 0x8:
    case 0x1B:
    case 0x6AE:
    case 0x5BD:
    case 0x5DB:
    case 0x5BC:
    case 0x33C4:
    case 0x11:
    case 0x18:
    case 0x598:
    case 0x23C5: return ELEM_UINT;
    case 0x35A2: return ELEM_INT;
    case EID_SAMPLINGFREQUENCY:
    case EID_DURATION: return ELEM_FLOAT;
    case EID_DOCTYPE:
    case EID_LANGUAGE:
    case 0x660:
    case 0x37C:
    case EID_CODECID: return ELEM_STRING;
    case EID_MUXINGAPP:
    case EID_WRITINGAPP:
    case 0x5A3:
    case 0x136E:
    case 0x3BA9:
    case 0x66E:
    case 0x67E:
    case 0x5:
    case 0x487: return ELEM_UTF8;
    case 0x6C:
    case EID_SEEKID:
    case EID_CODECPRIVATE:
    case 0x3F:
    case 0x65C:
    case 0x33A4: return ELEM_BIN;
    case EID_SIMPLEBLOCK:
    case 0x21: return ELEM_BLOCK;
    case 0x461: return ELEM_DATE;
    default: return ELEM_UNKNOWN;
    }
  }

  const Element Element::findChild(uint32_t id) const{
    if (getID() == id){return *this;}
    if (getType() != ELEM_MASTER){return Element();}
    if (minimalMode){
      ERROR_MSG("Attempted to find child element in header-only EBML buffer!");
      return Element();
    }
    const uint64_t payLen = getPayloadLen();
    const char *payDat = getPayload();
    uint64_t offset = 0;
    while (offset < payLen){
      if (needBytes(payDat + offset, payLen - offset) > payLen - offset){
        WARN_MSG("Trying to read beyond boundaries of element! Aborted.");
        break;
      }
      Element e(payDat + offset);
      Element f = e.findChild(id);
      if (f){return f;}
      offset += e.getOuterLen();
    }
    return Element();
  }

  std::string Element::toPrettyString(const uint8_t indent, const uint8_t detail) const{
    std::stringstream ret;
    switch (getType()){
    case ELEM_MASTER:{
      const uint64_t payLen = getPayloadLen();
      ret << std::string(indent, ' ') << "Element [" << getIDString(getID()) << "] ("
          << getOuterLen() << "b, ";
      if (payLen == 0xFFFFFFFFFFFFFFFFull){
        ret << "infinite";
      }else{
        ret << payLen << "b";
      }
      ret << " payload)" << std::endl;
      const char *payDat = getPayload();
      uint64_t offset = 0;
      while (!minimalMode && offset < payLen){
        if (needBytes(payDat + offset, payLen - offset) > payLen - offset){
          WARN_MSG("Trying to read beyond boundaries of element! Aborted.");
          break;
        }
        Element e(payDat + offset);
        ret << e.toPrettyString(indent + 2, detail);
        offset += e.getOuterLen();
      }
    }break;
    case ELEM_UINT:{
      ret << std::string(indent, ' ') << "Element (" << getPayloadLen() << "/" << getOuterLen()
          << ") [" << getIDString(getID()) << "] = " << getValUInt() << std::endl;
    }break;
    case ELEM_INT:{
      ret << std::string(indent, ' ') << "Element (" << getPayloadLen() << "/" << getOuterLen()
          << ") [" << getIDString(getID()) << "] = " << getValInt() << std::endl;
    }break;
    case ELEM_FLOAT:{
      ret << std::string(indent, ' ') << "Element (" << getPayloadLen() << "/" << getOuterLen()
          << ") [" << getIDString(getID()) << "] = " << getValFloat() << std::endl;
    }break;
    case ELEM_STRING:
    case ELEM_UTF8:{
      ret << std::string(indent, ' ') << "Element (" << getPayloadLen() << "/" << getOuterLen()
          << ") [" << getIDString(getID()) << "] = " << getValString() << std::endl;
    }break;
    case ELEM_BLOCK:{
      return Block(data).toPrettyString(indent, detail);
    }break;
    case ELEM_BIN:{
      const uint32_t EID = getID();
      const char *payDat = getPayload();
      const uint64_t payLen = getPayloadLen();
      if (EID == EID_SEEKID){
        ret << std::string(indent, ' ') << "Element (" << payLen << "/" << getOuterLen() << ") ["
            << getIDString(getID()) << "] = " << getIDString(getValUInt()) << std::endl;
        return ret.str();
      }
      if (payLen > 256 || (detail < 4 && payLen > 32)){
        ret << std::string(indent, ' ') << "Element (" << getOuterLen() << ") ["
            << getIDString(getID()) << "] = " << payLen << " bytes of binary data" << std::endl;
      }else{
        if (getPayloadLen() <= 32){
          ret << std::string(indent, ' ') << "Element (" << payLen << "/" << getOuterLen() << ") ["
              << getIDString(getID()) << "] = ";
          for (uint64_t i = 0; i < payLen; ++i){
            ret << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)payDat[i];
          }

        }else{
          ret << std::string(indent, ' ') << "Element (" << payLen << "/" << getOuterLen() << ") ["
              << getIDString(getID()) << "] =";
          for (uint64_t i = 0; i < payLen; ++i){
            if ((i % 32) == 0){ret << std::endl << std::string(indent + 2, ' ');}
            ret << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)payDat[i];
          }
        }
        ret << std::endl;
      }
    }break;
    default:
      ret << std::string(indent, ' ') << "Element [" << getIDString(getID()) << "] ("
          << getOuterLen() << "b, " << getPayloadLen() << "b payload)" << std::endl;
      ret << std::string(indent + 2, ' ') << "{Payload type not implemented}" << std::endl;
    }
    return ret.str();
  }

  uint64_t Element::getValUInt() const{
    const char *payDat = getPayload();
    uint64_t val = 0;
    switch (getPayloadLen()){
    case 1: val = payDat[0]; break;
    case 2: val = Bit::btohs(payDat); break;
    case 3: val = Bit::btoh24(payDat); break;
    case 4: val = Bit::btohl(payDat); break;
    case 5: val = Bit::btoh40(payDat); break;
    case 6: val = Bit::btoh48(payDat); break;
    case 7: val = Bit::btoh56(payDat); break;
    case 8: val = Bit::btohll(payDat); break;
    default: WARN_MSG("UInt payload size %llu not implemented", getPayloadLen());
    }
    return val;
  }

  int64_t Element::getValInt() const{
    const char *payDat = getPayload();
    int64_t val = 0;
    switch (getPayloadLen()){
    case 1: val = (int8_t)payDat[0]; break;
    case 2: val = (((int64_t)Bit::btohs(payDat)) << 48) >> 48; break;
    case 3: val = (((int64_t)Bit::btoh24(payDat)) << 40) >> 40; break;
    case 4: val = (int32_t)Bit::btohl(payDat); break;
    case 5: val = (((int64_t)Bit::btoh40(payDat)) << 24) >> 24; break;
    case 6: val = (((int64_t)Bit::btoh48(payDat)) << 16) >> 16; break;
    case 7: val = (((int64_t)Bit::btoh56(payDat)) << 8) >> 8; break;
    case 8: val = Bit::btohll(payDat); break;
    default: WARN_MSG("Int payload size %llu not implemented", getPayloadLen());
    }
    return val;
  }

  double Element::getValFloat() const{
    const char *payDat = getPayload();
    double val = 0;
    switch (getPayloadLen()){
    case 4: val = Bit::btohf(payDat); break;
    case 8: val = Bit::btohd(payDat); break;
    default: WARN_MSG("Float payload size %llu not implemented", getPayloadLen());
    }
    return val;
  }

  std::string Element::getValString() const{
    uint64_t strLen = getPayloadLen();
    const char * strPtr = getPayload();
    while (strLen && strPtr[strLen-1] == 0){--strLen;}
    return std::string(strPtr, strLen);
  }

  uint64_t Block::getTrackNum() const{return UniInt::readInt(getPayload());}

  int16_t Block::getTimecode() const{
    return Bit::btohs(getPayload() + UniInt::readSize(getPayload()));
  }

  bool Block::isKeyframe() const{return getPayload()[UniInt::readSize(getPayload()) + 2] & 0x80;}

  bool Block::isInvisible() const{
    return getPayload()[UniInt::readSize(getPayload()) + 2] & 0x08;
  }

  bool Block::isDiscardable() const{
    return getPayload()[UniInt::readSize(getPayload()) + 2] & 0x01;
  }

  uint8_t Block::getLacing() const{
    return (getPayload()[UniInt::readSize(getPayload()) + 2] & 0x6) >> 1;
  }

  uint8_t Block::getFrameCount() const{
    if (getLacing() == 0){return 1;}
    return getPayload()[UniInt::readSize(getPayload()) + 3] + 1;
  }

  uint32_t Block::getFrameSize(uint8_t no) const{
    switch (getLacing()){
      case 0://No lacing
        return getPayloadLen() - (UniInt::readSize(getPayload()) + 3);
      case 1:{//Xiph lacing
        uint64_t offset = (UniInt::readSize(getPayload()) + 3) + 1;
        uint8_t frames = getFrameCount();
        if (no > frames - 1){return 0;}//out of bounds
        uint64_t laceNo = 0;
        uint32_t currSize = 0;
        uint32_t totSize = 0;
        while (laceNo <= no && (laceNo < frames-1) && offset < getPayloadLen()){
          currSize += getPayload()[offset];
          if (getPayload()[offset] != 255){
            totSize += currSize;
            if (laceNo == no){return currSize;}
            currSize = 0;
            ++laceNo;
          }
          ++offset;
        }
        return getPayloadLen() - offset - totSize;//last frame is rest of the data
      }
      case 3:{//EBML lacing
        const char * pl = getPayload();
        uint64_t offset = (UniInt::readSize(pl) + 3) + 1;
        uint8_t frames = getFrameCount();
        if (no > frames - 1){return 0;}//out of bounds
        uint64_t laceNo = 0;
        uint32_t currSize = 0;
        uint32_t totSize = 0;
        while (laceNo <= no && (laceNo < frames-1) && offset < getPayloadLen()){
          if (laceNo == 0){
            currSize = UniInt::readInt(pl + offset);
          }else{
            currSize += UniInt::readSInt(pl + offset);
          }
          totSize += currSize;
          if (laceNo == no){return currSize;}
          ++laceNo;
          offset += UniInt::readSize(pl + offset);
        }
        return getPayloadLen() - offset - totSize;//last frame is rest of the data 
      }
      case 2://Fixed lacing
        return (getPayloadLen() - (UniInt::readSize(getPayload()) + 3)) / getFrameCount();
    }
    WARN_MSG("Lacing type not yet implemented!");
    return 0;
  }

  const char *Block::getFrameData(uint8_t no) const{
    switch (getLacing()){
      case 0://No lacing
        return getPayload() + (UniInt::readSize(getPayload()) + 3);
      case 1:{//Xiph lacing
        uint64_t offset = (UniInt::readSize(getPayload()) + 3) + 1;
        uint8_t frames = getFrameCount();
        if (no > frames - 1){return 0;}//out of bounds
        uint64_t laceNo = 0;
        uint32_t currSize = 0;
        while ((laceNo < frames-1) && offset < getPayloadLen()){
          if (laceNo < no){
            currSize += getPayload()[offset];
          }
          if (getPayload()[offset] != 255){
            ++laceNo;
          }
          ++offset;
        }
        return getPayload() + offset + currSize;
      }
      case 3:{//EBML lacing
        const char * pl = getPayload();
        uint64_t offset = (UniInt::readSize(pl) + 3) + 1;
        uint8_t frames = getFrameCount();
        if (no > frames - 1){return 0;}//out of bounds
        uint64_t laceNo = 0;
        uint32_t currSize = 0;
        uint32_t totSize = 0;
        while ((laceNo < frames-1) && offset < getPayloadLen()){
          if (laceNo == 0){
            currSize = UniInt::readInt(pl + offset);
          }else{
            currSize += UniInt::readSInt(pl + offset);
          }
          if (laceNo < no){
            totSize += currSize;
          }
          ++laceNo;
          offset += UniInt::readSize(pl + offset);
        }
        return pl + offset + totSize;
      }
      case 2://Fixed lacing
        return getPayload() + (UniInt::readSize(getPayload()) + 3) + 1 + no * getFrameSize(no);
    }
    WARN_MSG("Lacing type not yet implemented!");
    return 0;
  }

  std::string Block::toPrettyString(const uint8_t indent, const uint8_t detail) const{
    std::stringstream ret;
    ret << std::string(indent, ' ') << getIDString(getID()) << " with "
        << (unsigned int)getFrameCount() << " frame(s) for track " << getTrackNum() << " @ "
        << getTimecode();
    if (isKeyframe()){ret << " [KeyOnly]";}
    if (isInvisible()){ret << " [Invisible]";}
    if (isDiscardable()){ret << " [Discardable]";}
    switch (getLacing()){
    case 0:
      break; // No lacing
    case 1: ret << " [Lacing: Xiph]"; break;
    case 3: ret << " [Lacing: EMBL]"; break;
    case 2: ret << " [Lacing: Fixed]"; break;
    }
    ret << std::endl;
    if (detail >= 4){
      for (uint32_t frameNo = 0; frameNo < getFrameCount(); ++frameNo){
        const char *payDat = getFrameData(frameNo);
        const uint64_t payLen = getFrameSize(frameNo);
        ret << std::dec << std::string(indent + 4, ' ') << "Frame " << (frameNo+1) << " (" << payLen << "b):";
        if (!payDat || !payLen || detail < 6){
          ret << std::endl;
          continue;
        }
        for (uint64_t i = 0; i < payLen; ++i){
          if ((i % 32) == 0){ret << std::endl << std::string(indent + 6, ' ');}
          ret << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)payDat[i];
        }
        ret << std::endl;
      }
    }
    if (detail >= 10){
      uint32_t extraStuff = (UniInt::readSize(getPayload()) + 3);
      const char *payDat = getPayload() + extraStuff;
      const uint64_t payLen = getPayloadLen() - extraStuff;
      ret << std::dec << std::string(indent + 4, ' ') << "Raw data:";
      for (uint64_t i = 0; i < payLen; ++i){
        if ((i % 32) == 0){ret << std::endl << std::string(indent + 6, ' ');}
        ret << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)payDat[i];
      }
    }
    ret << std::endl;
    return ret.str();
  }
}

