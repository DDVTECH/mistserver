#pragma once
#include <mist/defines.h>
#include <ostream>
#include <sstream>
#include <string>
#include <unistd.h> //for stat
#include <util.h>

namespace FLAC{
  bool is_header(const char *header); ///< Checks the first 4 bytes for the string "flaC".
  size_t utfBytes(char p);            // UTF encoding byte size
  uint32_t utfVal(char *p);           // UTF encoding value

  size_t rate();
  uint8_t channels();

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

}// namespace FLAC
