#pragma once
#include "bitfields.h"
#include "defines.h"
#include <string>

namespace RIFF{

  /// Basic RIFF chunk class - can only read type and size.
  /// All RIFF chunks have this format.
  class Chunk{
  public:
    Chunk(const void *_p = 0, uint32_t len = 0);
    Chunk(void *_p, const char * t, uint32_t len);
    inline operator bool() const{return p;}
    inline std::string getType() const{
      if (!p){return "";}
      return std::string(p, 4);
    }
    inline uint32_t getPayloadSize() const{
      if (!p){return 0;}
      return Bit::btohl_le(p + 4);
    }
    virtual void toPrettyString(std::ostream &o, size_t indent = 0) const;

  protected:
    const char *p;
  };

  /// List-type RIFF chunk. Can read list identifier.
  /// "RIFF" and "LIST" type chunks follow this format.
  class ListChunk : public Chunk{
  public:
    ListChunk(const void *_p = 0, uint32_t len = 0) : Chunk(_p, len){}
    inline std::string getIdentifier() const{
      if (!p){return "";}
      return std::string(p + 8, 4);
    }
    virtual void toPrettyString(std::ostream &o, size_t indent = 0) const;
  };

  /// WAVE "fmt " class.
  class fmt : public Chunk{
  public:
    static std::string generate(uint16_t format, uint16_t channels, uint32_t hz, uint32_t bps, uint16_t blocksize, uint16_t size);
    fmt(const void *_p = 0, uint32_t len = 0) : Chunk(_p, len){}
    uint16_t getFormat() const;
    std::string getCodec() const;
    uint16_t getChannels() const;
    uint32_t getHz() const;
    uint32_t getBPS() const;
    uint16_t getBlockSize() const;
    uint16_t getSize() const;
    uint16_t getExtLen() const;
    uint16_t getValidBits() const;
    uint32_t getChannelMask() const;
    std::string getGUID() const;
    virtual void toPrettyString(std::ostream &o, size_t indent = 0) const;
  };
  
  /// WAVE fact class.
  class fact : public Chunk {
  public:
    static std::string generate(uint32_t samples);
    fact(const void *_p = 0, uint32_t len = 0) : Chunk(_p, len){}
    uint32_t getSamplesPerChannel() const;
    virtual void toPrettyString(std::ostream &o, size_t indent = 0) const;
  };

  /// ISFT class. Contains software name.
  class ISFT : public Chunk {
  public:
    ISFT(const void *_p = 0, uint32_t len = 0) : Chunk(_p, len){}
    std::string getSoftware() const;
    virtual void toPrettyString(std::ostream &o, size_t indent = 0) const;
  };


}

