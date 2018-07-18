#include <string>

namespace Opus{
  uint16_t getPreSkip(const char * initData);
  unsigned int Opus_getDuration(const char *part);
  std::string Opus_prettyPacket(const char *part, int len);
}

