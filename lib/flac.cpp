#include "bitfields.h"
#include "defines.h"
#include "encode.h"
#include "flac.h"
#include <sstream>

namespace FLAC{

  /// Checks the first 4 bytes for the string "flaC". Implementing a basic FLAC header check,
  /// returning true if it is, false if not.
  bool is_header(const char *header){
    if (header[0] != 'f') return false;
    if (header[1] != 'L') return false;
    if (header[2] != 'a') return false;
    if (header[3] != 'C') return false;
    return true;
  }// FLAC::is_header

  size_t utfBytes(char p){
    if ((p & 0x80) == 0x00){return 1;}
    if ((p & 0xE0) == 0xC0){return 2;}
    if ((p & 0xF0) == 0xE0){return 3;}
    if ((p & 0xF8) == 0xF0){return 4;}
    if ((p & 0xFC) == 0xF8){return 5;}
    if ((p & 0xFE) == 0xFC){return 6;}
    if ((p & 0xFF) == 0xFE){return 7;}
    return 9;
  }

  uint32_t utfVal(char *p){
    size_t bytes = utfBytes(*p);
    uint32_t ret = 0;

    if (bytes == 1){
      ret = (uint32_t)*p;
    }else if (bytes == 2){
      ret = (uint32_t)(*p & 0x1F) << 6;
      ret = ret | (*(p + 1) & 0x3f);
    }else if (bytes == 3){
      ret = (uint32_t)(*p & 0x1F) << 6;
      ret = (ret | (*(p + 1) & 0x3f)) << 6;
      ret = ret | (*(p + 2) & 0x3f);
    }else if (bytes == 4){
      ret = (uint32_t)(*p & 0x1F) << 6;
      ret = (ret | (*(p + 1) & 0x3f)) << 6;
      ret = (ret | (*(p + 2) & 0x3f)) << 6;
      ret = ret | (*(p + 3) & 0x3f);
    }

    return ret;
  }

  Frame::Frame(char *pkt){
    data = pkt;
    if (data[0] != 0xFF || (data[1] & 0xFC) != 0xF8){
      WARN_MSG("Sync code incorrect! Ignoring FLAC frame");
      FAIL_MSG("%x %x", data[0], data[1]);
      data = 0;
    }
  }

  uint16_t Frame::samples(){
    if (!data){return 0;}
    switch ((data[2] & 0xF0) >> 4){
    case 0: return 0; // reserved
    case 1: return 192;
    case 2: return 576;
    case 3: return 1152;
    case 4: return 2304;
    case 5: return 4608;
    case 6: return 1; // 1b at end
    case 7: return 2; // 2b at end
    default: return 256 << (((data[2] & 0xf0) >> 4) - 8);
    }
  }
  uint32_t Frame::rate(){
    if (!data){return 0;}
    switch (data[2] & 0x0F){
    case 0: return 0; // get from STREAMINFO
    case 1: return 88200;
    case 2: return 176400;
    case 3: return 192000;
    case 4: return 8000;
    case 5: return 16000;
    case 6: return 22050;
    case 7: return 24000;
    case 8: return 32000;
    case 9: return 44100;
    case 10: return 48000;
    case 11: return 96000;
    case 12: return 1; // 1b at end, *1000
    case 13: return 2; // 2b at end
    case 14: return 3; // 2b at end, *10
    case 15: return 0; // invalid, get from STREAMINFO
    default: return 0;
    }
  }

  uint8_t Frame::channels(){
    if (!data){return 0;}
    uint8_t ret = ((data[3] & 0xF0) >> 4) + 1;
    if (ret > 8 && ret < 12){return 2;}// special stereo
    return ret;
  }
  uint8_t Frame::size(){
    if (!data){return 0;}
    switch (data[3] & 0x0E){
    case 0: return 0; // get from STREAMINFO
    case 1: return 8;
    case 2: return 12;
    case 3: return 0; // reserved
    case 4: return 16;
    case 5: return 20;
    case 6: return 24;
    case 7: return 0; // reserved
    default: return 0;
    }
  }

  uint32_t Frame::utfVal(){
    return ::FLAC::utfVal(data + 4);
  }

  std::string Frame::toPrettyString(){
    if (!data){return "Invalid frame";}
    std::stringstream r;
    r << "FLAC frame, " << ((data[1] & 0x1) ? "variable" : "fixed") << " block size, " << samples() << " samples, "
      << rate() << " Hz, " << (int)channels() << " Ch, " << (int)size() << "-bit" << std::endl;
    return r.str();
  }

  MetaBlock::MetaBlock() : ptr(0), len(0){}
  MetaBlock::MetaBlock(const char *_ptr, size_t _len) : ptr(_ptr), len(_len){}

  std::string MetaBlock::getType(){
    if (!ptr || !len){return "";}
    switch (ptr[0] & 0x7F){
    case 0: return "STREAMINFO";
    case 1: return "PADDING";
    case 2: return "APPLICATION";
    case 3: return "SEEKTABLE";
    case 4: return "VORBIS_COMMENT";
    case 5: return "CUESHEET";
    case 6: return "PICTURE";
    case 127: return "INVALID";
    default: return "UNKNOWN";
    }
  }

  size_t MetaBlock::getSize(){
    if (!ptr || len < 4){return 0;}
    return Bit::btoh24(ptr + 1);
  }

  bool MetaBlock::getLast(){
    if (!ptr || !len){return false;}
    return ptr[0] & 0x80;
  }

  void MetaBlock::toPrettyString(std::ostream &out){
    if (len < 4){return;}
    out << getType() << " metadata block (" << getSize() << "b, "
        << (getLast() ? "last" : "non-last") << ")" << std::endl;
  }

  /// Helper function that calls the other toPrettyString function and returns the output as a string
  std::string MetaBlock::toPrettyString(){
    std::stringstream str;
    toPrettyString(str);
    return str.str();
  }

  uint16_t StreamInfo::getMinBlockSize(){
    return Bit::btohs(ptr + 4);
  }

  uint16_t StreamInfo::getMaxBlockSize(){
    return Bit::btohs(ptr + 6);
  }

  uint32_t StreamInfo::getMinFrameSize(){
    return Bit::btoh24(ptr + 8);
  }

  uint32_t StreamInfo::getMaxFrameSize(){
    return Bit::btoh24(ptr + 11);
  }

  uint32_t StreamInfo::getSampleRate(){
    return ((uint64_t)ptr[14] << 12) + ((uint64_t)ptr[15] << 4) + ((ptr[16] & 0xf0) >> 4);
  }

  uint8_t StreamInfo::getChannels(){
    return ((ptr[16] & 0x0e) >> 1) + 1;
  }

  uint8_t StreamInfo::getBits(){
    return ((ptr[17] & 0xf0) >> 4) + (ptr[16] & 1) * 16 + 1;
  }

  uint64_t StreamInfo::getSamples(){
    return ((uint64_t)(ptr[17] & 0x0f) << 32) + ((uint64_t)ptr[18] << 24) +
           ((uint64_t)ptr[19] << 16) + ((uint64_t)ptr[20] << 8) + ptr[21];
  }

  std::string StreamInfo::getMD5(){
    return Encodings::Hex::encode(std::string(ptr + 19, 16));
  }

  void StreamInfo::toPrettyString(std::ostream &out){
    if (len < 4){return;}
    out << getType() << " metadata block (" << getSize() << "b, "
        << (getLast() ? "last" : "non-last") << "):" << std::endl;
    out << "  Min block size: " << getMinBlockSize() << std::endl;
    out << "  Max block size: " << getMaxBlockSize() << std::endl;
    out << "  Min frame size: " << getMinFrameSize() << std::endl;
    out << "  Max frame size: " << getMaxFrameSize() << std::endl;
    out << "  Sample rate: " << getSampleRate() << std::endl;
    out << "  Channels: " << (size_t)getChannels() << std::endl;
    out << "  Bits: " << (size_t)getBits() << std::endl;
    out << "  Samples: " << getSamples() << std::endl;
    out << "  Checksum: " << getMD5() << std::endl;
  }

  std::string Picture::getPicType(){
    uint32_t t = Bit::btohl(ptr + 4);
    switch (t){
    case 0: return "Other";
    case 1: return "File icon";
    case 2: return "Other file icon";
    case 3: return "Cover (front)";
    case 4: return "Cover (back)";
    case 5: return "Leaflet";
    case 6: return "Media";
    case 7: return "Lead artist";
    case 8: return "Artist";
    case 9: return "Conductor";
    case 10: return "Band";
    case 11: return "Composer";
    case 12: return "Text writer";
    case 13: return "Recording location";
    case 14: return "During recording";
    case 15: return "During performance";
    case 16: return "Movie capture";
    case 17: return "A bright coloured fish";
    case 18: return "Illustration";
    case 19: return "Band logo";
    case 20: return "Publisher logo";
    }
    return "Unknown";
  }

  std::string Picture::getMime(){
    return std::string(ptr + 12, getMimeLen());
  }

  uint32_t Picture::getMimeLen(){
    return Bit::btohl(ptr + 8);
  }

  std::string Picture::getDesc(){
    return std::string(ptr + 16 + getMimeLen(), getDescLen());
  }

  uint32_t Picture::getDescLen(){
    return Bit::btohl(ptr + 12 + getMimeLen());
  }

  uint32_t Picture::getWidth(){
    return Bit::btohl(ptr + 16 + getMimeLen() + getDescLen());
  }

  uint32_t Picture::getHeight(){
    return Bit::btohl(ptr + 20 + getMimeLen() + getDescLen());
  }

  uint32_t Picture::getDepth(){
    return Bit::btohl(ptr + 24 + getMimeLen() + getDescLen());
  }

  uint32_t Picture::getColors(){
    return Bit::btohl(ptr + 28 + getMimeLen() + getDescLen());
  }

  uint32_t Picture::getDataLen(){
    return Bit::btohl(ptr + 32 + getMimeLen() + getDescLen());
  }

  const char *Picture::getData(){
    return ptr + 36 + getMimeLen() + getDescLen();
  }

  void Picture::toPrettyString(std::ostream &out){
    if (len < 4){return;}
    out << getType() << " metadata block (" << getSize() << "b, "
        << (getLast() ? "last" : "non-last") << "):" << std::endl;
    out << "  Picture type: " << getPicType() << std::endl;
    out << "  Mime type: " << getMime() << std::endl;
    out << "  Description: " << getDesc() << std::endl;
    out << "  Dimensions: " << getWidth() << "x" << getHeight() << std::endl;
    out << "  Color depth: " << getDepth() << std::endl;
    out << "  Color count: " << getColors() << std::endl;
    out << "  Picture data size: " << getDataLen() << "b" << std::endl;
  }

  uint32_t VorbisComment::getVendorSize(){
    return Bit::btohl_le(ptr + 4);
  }

  std::string VorbisComment::getVendor(){
    return std::string(ptr + 8, getVendorSize());
  }

  std::string VorbisComment::getComment(uint32_t _num){
    size_t offset = 12 + getVendorSize();
    size_t i = 0;
    while (offset < getSize() - 4){
      size_t len = Bit::btohl_le(ptr + offset);
      if (i == _num){return std::string(ptr + offset + 4, len);}
      offset += 4 + len;
      ++i;
    }
    return "";
  }

  uint32_t VorbisComment::getCommentCount(){
    return Bit::btohl_le(ptr + 8 + getVendorSize());
  }

  void VorbisComment::toPrettyString(std::ostream &out){
    if (len < 4){return;}
    out << getType() << " metadata block (" << getSize() << "b, "
        << (getLast() ? "last" : "non-last") << "):" << std::endl;
    out << "  Vendor: " << getVendor() << std::endl;
    out << "  Comment count: " << getCommentCount() << std::endl;
    size_t offset = 12 + getVendorSize();
    while (offset < getSize() - 4){
      size_t len = Bit::btohl_le(ptr + offset);
      out << "    " << std::string(ptr + offset + 4, len) << std::endl;
      offset += 4 + len;
    }
  }

}; // Namespace FLAC
