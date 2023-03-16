#pragma once
#include <ostream>
#include <sstream>
#include <string>
#include <unistd.h> //for stat
#include "util.h"

namespace FLAC{
  bool is_header(const char *header); ///< Checks the first 4 bytes for the string "flaC".
  size_t utfBytes(char p);            // UTF encoding byte size
  uint32_t utfVal(char *p);           // UTF encoding value

  class Frame{
  public:
    Frame(char *pkt);
    uint16_t samples();
    uint32_t rate();
    uint8_t channels();
    uint8_t size();

    uint32_t utfVal(); // UTF encoding value
    std::string toPrettyString();

  private:
    char *data;
  };

  class MetaBlock{
  public:
    MetaBlock();
    MetaBlock(const char *_ptr, size_t _len);
    std::string getType();
    size_t getSize();
    bool getLast();
    std::string toPrettyString();
    virtual void toPrettyString(std::ostream &out);

  protected:
    const char *ptr;
    size_t len;
  };

  class StreamInfo : public MetaBlock{
  public:
    StreamInfo() : MetaBlock(){};
    StreamInfo(const char *_ptr, size_t _len) : MetaBlock(_ptr, _len){};
    uint16_t getMinBlockSize();
    uint16_t getMaxBlockSize();
    uint32_t getMinFrameSize();
    uint32_t getMaxFrameSize();
    uint32_t getSampleRate();
    uint8_t getChannels();
    uint8_t getBits();
    uint64_t getSamples();
    std::string getMD5();
    void toPrettyString(std::ostream &out);
  };

  class Picture : public MetaBlock{
  public:
    Picture() : MetaBlock(){};
    Picture(const char *_ptr, size_t _len) : MetaBlock(_ptr, _len){};
    std::string getPicType();
    std::string getMime();
    uint32_t getMimeLen();
    std::string getDesc();
    uint32_t getDescLen();
    uint32_t getWidth();
    uint32_t getHeight();
    uint32_t getDepth();
    uint32_t getColors();
    uint32_t getDataLen();
    const char *getData();
    void toPrettyString(std::ostream &out);
  };

  class VorbisComment : public MetaBlock{
  public:
    VorbisComment() : MetaBlock(){};
    VorbisComment(const char *_ptr, size_t _len) : MetaBlock(_ptr, _len){};
    uint32_t getVendorSize();
    std::string getVendor();
    std::string getComment(uint32_t _num);
    uint32_t getCommentCount();
    void toPrettyString(std::ostream &out);
  };

}// namespace FLAC
