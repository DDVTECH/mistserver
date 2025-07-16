/// \file cache.cpp Implements cache access and controls the cache utility process.
#include "cache.h"

#include "auth.h"
#include "config.h"
#include "defines.h"
#include "procs.h"
#include "stream.h"
#include "timing.h"
#include "urireader.h"
#include "util.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

static std::string cachePath{};

// Reads out the cache-related global config settings
bool load_config() {
  cachePath = Util::getGlobalConfig("cachePath").asStringRef();
  if (cachePath.empty()) return false;
  cachePath = HTTP::localURIResolver().link(cachePath).getFilePath();
  if (cachePath[cachePath.size() - 1] != '/') cachePath += '/';
  return true;
}

// Hashes absolute path, retains the filename, and appends the hash if the file was in a directory
void file_to_cache_path(std::string & input, bool createDirs = true, bool hashFilename = false) {
  std::string hash = Secure::fnv1a(input);
  size_t lastSlash = input.rfind('/');
  if (lastSlash != std::string::npos) {
    input.erase(0, lastSlash + 1);
    size_t lastDot = input.rfind('.');

    // Pages are always prepended with a slash, we do not insert the hash for (shared memory) pages
    if (lastSlash) input.insert(lastDot == std::string::npos ? input.size() : lastDot, '_' + hash);
  }
  input = cachePath + hash.substr(0, 2) + "/" + hash.substr(2, 1) + "/" + (hashFilename ? hash.substr(3) : input);
  if (createDirs) Util::createPathFor(input, true);
}

namespace Cache {
  std::string getCachePath() {
    return cachePath;
  }

  std::string getStrFileAttr(int fd, const std::string & attr) {
    char strVal[NAME_BUFFER_SIZE];
    ssize_t len = fgetxattr(fd, attr.c_str(), strVal, NAME_BUFFER_SIZE);
    if (len <= 0) return "";
    return std::string(strVal, len);
  }

  uint64_t getIntFileAttr(int fd, const std::string & attr) {
    JSON::Value A = getStrFileAttr(fd, attr);
    return A.asInt();
  }

  void setFileAttr(int fd, const std::string & attr, const std::string & val) {
    if (val.size()) { fsetxattr(fd, attr.c_str(), val.c_str(), val.size(), 0); }
  }

  void setFileAttr(int fd, const std::string & attr, uint64_t val) {
    if (!val) { return; }
    setFileAttr(fd, attr, std::to_string(val));
  }

  bool isInitialised() {
    return !cachePath.empty();
  }

  int cacheRead(const std::string & fileName, bool hits) {
    std::string cacheFile = fileName;
    if (!load_config()) return -1;
    file_to_cache_path(cacheFile, false);

    int fileFd = open(cacheFile.c_str(), O_RDONLY);
    if (fileFd != -1 && hits) hit(cacheFile);

    INSANE_MSG("[cacheRead] file=%s (fd=%d) cache=%s", fileName.c_str(), fileFd, cacheFile.c_str());
    return fileFd;
  }

  bool cacheWrite(const std::string & fileName, const std::string & contents, uint32_t genTime,
                  const std::string & streamName, const uint64_t expiry) {
    if (!load_config() || contents.empty()) return false;
    std::string cacheFile = fileName;
    file_to_cache_path(cacheFile);

    return Util::atomicWriteFile(cacheFile, [&](int fd) -> bool {
      size_t i = 0;
      while (i < contents.size()) {
        int ret = write(fd, contents.data() + i, contents.size() - i);
        if (ret == -1) return false;
        i += ret;
      }
      miss(fd, genTime, streamName, expiry);
      INSANE_MSG("[cacheWrite] file=%s cache=%s stream=%s", fileName.c_str(), cacheFile.c_str(), streamName.c_str());
      return true;
    });
  }

  bool cacheWrite(const std::string & fileName, const std::function<void(int fd)> & write, uint32_t genTime,
                  const std::string & streamName, const uint64_t expiry) {
    if (!load_config() || write == nullptr) return false;
    std::string cacheFile = fileName;
    file_to_cache_path(cacheFile);

    return Util::atomicWriteFile(cacheFile, [&](int fd) -> bool {
      write(fd);
      miss(fd, genTime, streamName, expiry);
      INSANE_MSG("[cacheWrite] file=%s cache=%s stream=%s", fileName.c_str(), cacheFile.c_str(), streamName.c_str());
      return true;
    });
  }

  void miss(int fd, uint32_t genTime, const std::string & streamName, const uint64_t expiry) {
    setFileAttr(fd, "user.mist.frequency", 1024);
    setFileAttr(fd, "user.mist.gentime", genTime);
    setFileAttr(fd, "user.mist.lastdecay", Util::unixMS());
    setFileAttr(fd, "user.mist.expiry", expiry);
    setFileAttr(fd, "user.mist.stream", streamName);
  }

  void hit(const std::string & cacheFile) {
    HTTP::URL UDPAddr(Util::getGlobalConfig("cacheSocket").asStringRef());
    if (UDPAddr.protocol != "udp") { return; }
    Socket::UDPConnection uSock(UDPAddr);
    uSock.SendNow(JSONOBJECT({{"hit", cacheFile}}).toString());
  }

  void wipeStreamFromCache(const std::string & streamName, bool suffixOnly, bool fromCacheUtil) {
    if (streamName.empty() || fromCacheUtil) return;
    std::string bin = Util::getMyPath() + "MistUtilCache";
    std::deque<std::string> args = {bin, "-s", streamName};
    if (suffixOnly) args.push_back("--suffix-only");
    int err = STDERR_FILENO;
    Util::Procs::StartPiped(args, 0, 0, &err);
  }

} // namespace Cache
