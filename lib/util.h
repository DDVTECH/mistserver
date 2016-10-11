#include <string>
#include <deque>

namespace Util {
  bool stringScan(const std::string & src, const std::string & pattern, std::deque<std::string> & result);
  uint64_t ftell(FILE * stream);
  uint64_t fseek(FILE * stream, uint64_t offset, int whence);
}
