#include <string>

namespace Opus{
  unsigned int Opus_getDuration(const char *part);
  std::string Opus_prettyPacket(const char *part, int len);
}

