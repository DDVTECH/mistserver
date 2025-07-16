/// \file cache.h Core file for managing and observing the cache.
#include <cstdint>
#include <fcntl.h>
#include <functional>
#include <string>
#include <sys/stat.h>

namespace Cache {
  // Getter, setter, and utility functions
  std::string getCachePath();
  uint64_t getMaxCacheSize();
  bool isInitialised();

  // Functions that handle file attribute reading/writing
  std::string getStrFileAttr(int fd, const std::string & attr);
  uint64_t getIntFileAttr(int fd, const std::string & attr);
  void setFileAttr(int fd, const std::string & attr, const std::string & val);
  void setFileAttr(int fd, const std::string & attr, uint64_t val);

  // Functions for managing the (contents of the) circular cache buffer
  int cacheRead(const std::string & fileName, bool hits = true);
  bool cacheWrite(const std::string & fileName, const std::string & contents, uint32_t genTime,
                  const std::string & streamName, const uint64_t expiry = 0);
  bool cacheWrite(const std::string & fileName, const std::function<void(int fd)> & write, uint32_t genTime,
                  const std::string & streamName, const uint64_t expiry = 0);
  void miss(int fd, uint32_t genTime, const std::string & streamName, const uint64_t expiry = 0);
  void hit(const std::string & cacheFile);

  // Functions for mutating the cache as a whole
  void wipeStreamFromCache(const std::string & streamName, bool suffixOnly = false, bool fromCacheUtil = false);
} // namespace Cache
