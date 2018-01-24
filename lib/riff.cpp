#include "riff.h"

namespace RIFF{

  Chunk::Chunk(const void *_p, uint32_t len){
    p = (const char *)_p;
    if (len && len < getPayloadSize() + 8){
      FAIL_MSG("Chunk %s (%lub) does not fit in %lu bytes length!", getType().c_str(),
               getPayloadSize() + 8, len);
      p = 0;
    }
  }

  Chunk::Chunk(void *_p, const char * t, uint32_t len){
    p = (const char *)_p;
    memcpy((void*)p, t, 4);
    Bit::htobl_le((char*)p+4, len);
  }

  void Chunk::toPrettyString(std::ostream &o, size_t indent) const{
    if (!p){
      o << std::string(indent, ' ') << "INVALID CHUNK" << std::endl;
      return;
    }
    switch (Bit::btohl(p)){
    case 0x52494646lu: // RIFF
    case 0x4C495354lu: // LIST
      return ListChunk(p).toPrettyString(o, indent);
    case 0x666D7420: // "fmt "
      return fmt(p).toPrettyString(o, indent);
    case 0x66616374: // fact
      return fact(p).toPrettyString(o, indent);
    case 0x49534654: // ISFT
      return ISFT(p).toPrettyString(o, indent);
    default:
      o << std::string(indent, ' ') << "[" << getType() << "] UNIMPLEMENTED ("
        << (getPayloadSize() + 8) << "b)" << std::endl;
    }
  }

  void ListChunk::toPrettyString(std::ostream &o, size_t indent) const{
    o << std::string(indent, ' ') << "[" << getType() << "] " << getIdentifier() << " ("
      << (getPayloadSize() + 8) << "b):" << std::endl;
    indent += 2;
    uint32_t i = 12;
    uint32_t len = getPayloadSize() + 8;
    while (i + 8 <= len){
      const Chunk C(p + i);
      C.toPrettyString(o, indent);
      i += C.getPayloadSize() + 8;
      if (!C){return;}
    }
  }

  uint16_t fmt::getFormat() const{
    if (!p){return 0;}
    return Bit::btohs_le(p + 8);
  }
  std::string fmt::getCodec() const{
    switch (getFormat()){
    case 0x1: return "PCM";
    case 0x2: return "ADPCM";
    case 0x3: return "FLOAT";
    case 0x102:
    case 0x172:
    case 0x6: return "PCMA";
    case 0x101:
    case 0x171:
    case 0x7: return "PCMU";
    case 0x55: return "MP3";
    case 0xFFFE: // Extended, not implemented
    default: return "?";
    }
  }
  uint16_t fmt::getChannels() const{
    if (!p){return 0;}
    return Bit::btohs_le(p + 10);
  }
  uint32_t fmt::getHz() const{
    if (!p){return 0;}
    return Bit::btohl_le(p + 12);
  }
  uint32_t fmt::getBPS() const{
    if (!p){return 0;}
    return Bit::btohl_le(p + 16);
  }
  uint16_t fmt::getBlockSize() const{
    if (!p){return 0;}
    return Bit::btohs_le(p + 20);
  }
  uint16_t fmt::getSize() const{
    if (!p){return 0;}
    return Bit::btohs_le(p + 22);
  }
  uint16_t fmt::getExtLen() const{
    if (getPayloadSize() < 18){return 0;}
    return Bit::btohs_le(p + 24);
  }
  uint16_t fmt::getValidBits() const{
    if (getPayloadSize() < 20 || getExtLen() < 2){return 0;}
    return Bit::btohs_le(p + 26);
  }
  uint32_t fmt::getChannelMask() const{
    if (getPayloadSize() < 24 || getExtLen() < 6){return 0;}
    return Bit::btohl_le(p + 28);
  }
  std::string fmt::getGUID() const{
    if (getPayloadSize() < 40 || getExtLen() < 22){return "";}
    return std::string(p + 32, 16);
  }
  void fmt::toPrettyString(std::ostream &o, size_t indent) const{
    o << std::string(indent, ' ') << "[" << getType() << "] (" << (getPayloadSize() + 8)
      << "b):" << std::endl;
    indent += 1;
    o << std::string(indent, ' ') << "Codec: " << getCodec() << " (" << getFormat() << ")"
      << std::endl;
    o << std::string(indent, ' ') << "Channels: " << getChannels() << std::endl;
    o << std::string(indent, ' ') << "Sample rate: " << getHz() << "Hz" << std::endl;
    o << std::string(indent, ' ') << "Bytes/s: " << getBPS() << std::endl;
    o << std::string(indent, ' ') << "Block size: " << getBlockSize() << " bytes" << std::endl;
    o << std::string(indent, ' ') << "Sample size: " << getSize() << " bits" << std::endl;
    if (getExtLen()){
      o << std::string(indent, ' ') << "-- extended " << getExtLen() << "bytes --" << std::endl;
      if (getExtLen() >= 2){
        o << std::string(indent, ' ') << "Valid bits: " << getValidBits() << std::endl;
      }
      if (getExtLen() >= 6){
        o << std::string(indent, ' ') << "Channel mask: " << getChannelMask() << std::endl;
      }
      if (getExtLen() >= 22){
        o << std::string(indent, ' ') << "GUID: " << getGUID() << std::endl;
      }
    }
  }
  std::string fmt::generate(uint16_t format, uint16_t channels, uint32_t hz, uint32_t bps, uint16_t blocksize, uint16_t size){
    std::string ret("fmt \022\000\000\000", 8);
    ret.append(std::string((size_t)18, '\000'));
    Bit::htobs_le((char*)ret.data()+8, format);
    Bit::htobs_le((char*)ret.data()+10, channels);
    Bit::htobl_le((char*)ret.data()+12, hz);
    Bit::htobl_le((char*)ret.data()+16, bps);
    Bit::htobs_le((char*)ret.data()+20, blocksize);
    Bit::htobs_le((char*)ret.data()+22, size);
    Bit::htobs_le((char*)ret.data()+24, 0);
    return ret;
  }

  uint32_t fact::getSamplesPerChannel() const{
    if (!p){return 0;}
    return Bit::btohl_le(p + 8);
  }
  void fact::toPrettyString(std::ostream &o, size_t indent) const{
    o << std::string(indent, ' ') << "[" << getType() << "] (" << (getPayloadSize() + 8)
      << "b):" << std::endl;
    indent += 1;
    o << std::string(indent, ' ') << "Samples per channel: " << getSamplesPerChannel() << std::endl;
  }
  std::string fact::generate(uint32_t samples){
    std::string ret("fact\004\000\000\000\000\000\000\000", 12);
    Bit::htobl_le((char*)ret.data()+8, samples);
    return ret;
  }

  std::string ISFT::getSoftware() const{
    if (!p){return 0;}
    return std::string(p+8, getPayloadSize());
  }
  void ISFT::toPrettyString(std::ostream &o, size_t indent) const{
    o << std::string(indent, ' ') << "[" << getType() << "] (" << (getPayloadSize() + 8)
      << "b):" << std::endl;
    indent += 1;
    o << std::string(indent, ' ') << "Software: " << getSoftware() << std::endl;
  }
}

