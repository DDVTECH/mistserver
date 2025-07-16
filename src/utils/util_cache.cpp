#include <mist/cache.h>
#include <mist/config.h>
#include <mist/ev.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/urireader.h>
#include <mist/url.h>
#include <mist/util.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <dirent.h>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/xattr.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

int inotFd{-1}, expiryInterval{-1};
uint64_t nextExpiry = Util::unixMS() + CACHE_CHECK_INTERVAL;
uint64_t currentSize{0}, maxBytes{0}, maxPercentBytes{0}, lastScoreComputation{0}, fsSize{0};
double maxPercent{0.0};
std::deque<std::pair<uint64_t, std::string>> scores;
Event::Loop evLp;
std::set<int> wdTop, wdLower, wdFiles;
std::mutex attributeMutex;
std::unordered_map<int, std::string> watchPaths;
static std::map<int, std::string> openFiles{};

/// Cache attribute container with some constructors and a score function
struct cacheAttribute {
    int fd{-1};
    std::string stream;
    uint64_t frequency{};
    uint64_t fileSize{};
    uint32_t genTime{};
    uint64_t lastHit{};
    uint64_t expiry{};
    cacheAttribute() = default;
    cacheAttribute(const std::string & path) {
      fd = open(path.c_str(), O_RDONLY);
      if (fd == -1) { return; }

      struct stat buffStats;
      if (!fstat(fd, &buffStats)) { fileSize = buffStats.st_size; }

      stream = Cache::getStrFileAttr(fd, "user.mist.stream");
      frequency = Cache::getIntFileAttr(fd, "user.mist.frequency");
      genTime = Cache::getIntFileAttr(fd, "user.mist.gentime");
      lastHit = Cache::getIntFileAttr(fd, "user.mist.lastdecay");
      expiry = Cache::getIntFileAttr(fd, "user.mist.expiry");

      close(fd);
      fd = -1;
    }
    ~cacheAttribute() {
      if (fd != -1) { close(fd); }
    }
    uint64_t freq(uint64_t now) const {
      return frequency * std::pow(2.0, -double(now - lastHit) / CACHE_SCORE_HALFLIFE);
    }
    /// Computes the GDSF score, used by clearCache() to determine which files need to be evicted
    double score(uint64_t now) const {
      if (!lastHit || !fileSize) return 0;
      return ((double)freq(now)) * (genTime + 1) / fileSize;
    }
};

std::unordered_map<std::string, cacheAttribute> attributeMap;

/// Sets up the inotify watch for the given path, mask specifies the events, watchSet contains the inotify fds
bool addWatch(const std::string & path, uint32_t mask, std::set<int> & watchSet) {
  if (&watchSet == &wdTop) {
    struct statvfs cacheFs;
    statvfs(path.c_str(), &cacheFs);
    fsSize = (cacheFs.f_blocks * cacheFs.f_frsize);
    if (maxPercent) {
      uint64_t newPBytes = (maxPercent / 100.0) * fsSize;
      if (newPBytes != maxPercentBytes) {
        INFO_MSG("Setting cache percent-size to %.3f ( = %" PRIu64 "b)", maxPercent, maxPercentBytes);
        maxPercentBytes = newPBytes;
      }
    } else {
      maxPercentBytes = 0;
    }
  }

  int wd = inotify_add_watch(inotFd, path.c_str(), mask);
  if (wd == -1) {
    if (errno == ENOENT) return false;
    ERROR_MSG("Could not watch '%s': %s", path.c_str(), strerror(errno));
    return false;
  }
  watchSet.insert(wd);
  watchPaths[wd] = path;
  return true;
}

/// Updates the attribute of a file in the global attribute map
void updateAttribute(const std::string & filePath) {
  cacheAttribute ca(filePath);
  if (ca.expiry && ca.expiry < nextExpiry) {
    nextExpiry = ca.expiry;
    uint64_t now = Util::unixMS();
    if (expiryInterval != -1) {
      if (nextExpiry <= now) {
        evLp.rescheduleInterval(expiryInterval, 1);
      } else {
        evLp.rescheduleInterval(expiryInterval, nextExpiry - now);
      }
    }
  }

  std::lock_guard<std::mutex> lock(attributeMutex);
  std::unordered_map<std::string, cacheAttribute>::iterator it = attributeMap.find(filePath);
  if (it != attributeMap.end()) {
    currentSize = currentSize >= it->second.fileSize ? currentSize - it->second.fileSize : 0;
    it->second = ca;
  } else {
    attributeMap.insert({filePath, ca});
  }
  currentSize += ca.fileSize;
  lastScoreComputation = 0;
}

/// Removes a file and its attributes in the global attribute map
void removeAttribute(const std::string & filePath) {
  std::lock_guard<std::mutex> lock(attributeMutex);
  std::unordered_map<std::string, cacheAttribute>::iterator it = attributeMap.find(filePath);
  if (it == attributeMap.end()) return;
  currentSize = currentSize >= it->second.fileSize ? currentSize - it->second.fileSize : 0;
  attributeMap.erase(it);
  lastScoreComputation = 0;
}

// Scans the cache directory for directories (top two levels) and files (lowest level)
void scanCacheDirectory(const std::string & path, unsigned int level) {
  uint32_t flags = IN_CREATE | IN_MOVED_TO | IN_ONLYDIR | (level < 2 ? 0 : IN_DELETE | IN_MOVED_FROM);
  if (!addWatch(path, flags, level < 2 ? wdTop : wdLower)) return;

  DIR *dir = opendir(path.c_str());
  if (!dir) {
    WARN_MSG("Could not scan cache directory '%s': %s", path.c_str(), strerror(errno));
    return;
  }
  struct dirent *entry;
  struct stat st;
  while ((entry = readdir(dir)) != nullptr) {
    if (level < 2) {
      if (entry->d_name[0] == '.') continue;
      if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) continue;
      const std::string entryPath = path + entry->d_name + '/';
      if (stat(entryPath.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;
      scanCacheDirectory(entryPath, level + 1);
    } else {
      if (entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN) continue;
      if (!S_ISREG(st.st_mode)) continue;
      const std::string filePath = path + entry->d_name;
      updateAttribute(filePath);
    }
  }
  closedir(dir);
}

/// Function which initialises the cache and memory structures from an existing cache directory
void initCache(const std::string & path) {
  DIR *upper = opendir(path.c_str());
  if (!upper) return;

  struct stat st;
  struct dirent *upperEntry;
  std::vector<std::thread> threads;
  std::vector<std::string> upperPaths;
  addWatch(path, IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ONLYDIR, wdTop);

  // Iterate over the top-level directory, this should only contain subdirectories
  while ((upperEntry = readdir(upper)) != nullptr) {
    if (upperEntry->d_name[0] == '.') continue;
    if (upperEntry->d_type != DT_DIR && upperEntry->d_type != DT_UNKNOWN) continue;

    const std::string upperPath = path + upperEntry->d_name + '/';
    if (upperEntry->d_type == DT_UNKNOWN && (stat(upperPath.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))) continue;
    upperPaths.emplace_back(upperPath);
    addWatch(upperPath, IN_CREATE | IN_MOVED_TO | IN_ONLYDIR, wdTop);
  }
  closedir(upper);

  const size_t hardwareThreads = std::thread::hardware_concurrency();
  const size_t threadCount = std::min({hardwareThreads ? hardwareThreads : 4UL, 32UL, upperPaths.size()});
  std::atomic<size_t> nextUpperPath(0);

  // Create worker threads that each process the next available top-level directory
  for (size_t i = 0; i < threadCount; ++i) {
    threads.emplace_back([&]() {
      std::unordered_map<std::string, cacheAttribute> caMap;
      std::set<std::string> files;
      struct stat threadSt;
      size_t upperIndex{};
      size_t threadSize{};

      do {
        upperIndex = nextUpperPath.fetch_add(1);
        if (upperIndex >= upperPaths.size()) break;
        const std::string & upperPath = upperPaths[upperIndex];

        // Set-up the second level directory dirent structures
        DIR *lower = opendir(upperPath.c_str());
        if (!lower) continue;
        struct dirent *lowerEntry;

        // Iterate the middle-level directory, there are no files just directories
        while ((lowerEntry = readdir(lower)) != nullptr) {
          if (lowerEntry->d_name[0] == '.') continue;
          if (lowerEntry->d_type != DT_DIR && lowerEntry->d_type != DT_UNKNOWN) continue;

          const std::string lowerPath = upperPath + lowerEntry->d_name + '/';
          if (lowerEntry->d_type == DT_UNKNOWN && (stat(lowerPath.c_str(), &threadSt) != 0 || !S_ISDIR(threadSt.st_mode)))
            continue;

          // Set-up the lowest directory, here be files
          {
            std::lock_guard<std::mutex> lock(attributeMutex);
            addWatch(lowerPath, IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_FROM | IN_ONLYDIR, wdLower);
          }
          DIR *fileDir = opendir(lowerPath.c_str());
          if (!fileDir) continue;
          struct dirent *fileDirEntry;

          // Read the actual files
          while ((fileDirEntry = readdir(fileDir)) != nullptr) {
            if (fileDirEntry->d_type != DT_REG && fileDirEntry->d_type != DT_UNKNOWN) continue;
            const std::string filePath = lowerPath + fileDirEntry->d_name;
            if (stat(filePath.c_str(), &threadSt) != 0 || !S_ISREG(threadSt.st_mode)) continue;

            // Get the file size from stat, and the remainder from extended attributes
            caMap.insert({filePath, cacheAttribute(filePath)});
            files.insert(filePath);
            threadSize += threadSt.st_size;
          }
          closedir(fileDir);
        }
        closedir(lower);
      } while (upperIndex + 1 < upperPaths.size());

      {
        std::lock_guard<std::mutex> lock(attributeMutex);
        currentSize += threadSize;
        attributeMap.insert(std::make_move_iterator(caMap.begin()), std::make_move_iterator(caMap.end()));
        for (std::string file : files)
          addWatch(file, IN_MOVED_TO | IN_MODIFY | IN_ATTRIB | IN_DELETE | IN_DELETE_SELF, wdFiles);
      }
    });
  }
  for (size_t i = 0; i < threads.size(); ++i) threads[i].join();
  INFO_MSG(
    "Restored cache directory at %s with %zu files in %zu directories (%zu accounted bytes, limit %zub / %.3f%%)",
    path.c_str(), attributeMap.size(), wdTop.size() + wdLower.size() - 1, currentSize, maxBytes, maxPercent);
}

/// Rebuilds the cache watch state and then calls initialise
void rebuildCache(const std::string & path) {
  INFO_MSG("Rebuilding cache accounting at '%s' after lost inotify events", path.c_str());
  for (std::unordered_map<int, std::string>::const_iterator it = watchPaths.begin(); it != watchPaths.end(); ++it) {
    if (inotify_rm_watch(inotFd, it->first) == -1 && errno != EINVAL)
      WARN_MSG("Could not remove cache watch %d: %s", it->first, strerror(errno));
  }
  wdTop.clear();
  wdLower.clear();
  wdFiles.clear();
  watchPaths.clear();

  // Discard events from the removed watches before creating replacement watches.
  char data[4096];
  for (;;) {
    ssize_t data_len = read(inotFd, data, sizeof(data));
    if (data_len > 0) continue;
    if (data_len == -1 && errno == EINTR) continue;
    if (data_len == -1 && errno != EAGAIN) ERROR_MSG("Could not drain old cache events: %s", strerror(errno));
    break;
  }

  {
    std::lock_guard<std::mutex> lock(attributeMutex);
    attributeMap.clear();
    currentSize = 0;
    scores.clear();
    lastScoreComputation = 0;
  }
  initCache(path);
}

/// Removes the given files and returns the number of bytes removed. Assumes attributeMutex is locked by the caller
uint64_t removeMarkedFiles(std::set<std::string> & markedForDeletion) {
  std::map<std::string, int> reverseOpenFiles;
  for (auto & ofile : openFiles) reverseOpenFiles[ofile.second] = ofile.first;

  // Remove all the files that are marked for deletion, also close the file if it is currently open
  uint64_t clearedSize = 0;
  size_t removedFiles = 0;
  for (const std::string & fn : markedForDeletion) {
    auto it = attributeMap.find(fn);
    if (it == attributeMap.end()) continue;
    if (unlink(fn.c_str()) != 0 && errno != ENOENT) {
      ERROR_MSG("Could not remove cached file '%s': %s", fn.c_str(), strerror(errno));
      lastScoreComputation = 0;
      continue;
    }
    clearedSize += it->second.fileSize;
    attributeMap.erase(it);
    ++removedFiles;
    if (reverseOpenFiles.find(fn) == reverseOpenFiles.end()) continue;
    ::close(reverseOpenFiles[fn]);
    openFiles.erase(reverseOpenFiles[fn]);
    reverseOpenFiles.erase(fn);
  }
  if (removedFiles) {
    HIGH_MSG("Removed %zu cached files weighing %" PRIu64 " accounted bytes", removedFiles, clearedSize);
  }
  return clearedSize;
}

/// Clears the cache until sizeNeeded is met, returns false if not enough space can be made available
/// Eviction is based on a modified version of GDSF (Greedy-Dual-Size-Frequency) where we compute
///   L = 2^(-(now - lastHit)/halfLife), as exponential decay factor (replacing the age factor in GDSF)
///   K = hits * L * (genTime + 1) / fileSize, as priority key (just as in GDSF)
/// We favour evicting files with less recent accesses, larger files, and files with high generation time
bool clearCache(uint64_t sizeNeeded = 0) {
  std::set<std::string> markedForDeletion;
  uint64_t clearedSize = 0;
  const uint64_t now = Util::unixMS();

  // Block edits to attributes while we are working on clearing the cache
  std::lock_guard<std::mutex> lock(attributeMutex);
  if (sizeNeeded > currentSize) {
    WARN_MSG("Requested cache clear exceeds current cache size! (%" PRIu64 " > %" PRIu64 ")", sizeNeeded, currentSize);
    return false;
  }
  if (attributeMap.empty()) {
    // Nothing cached, this is normal for the periodic expiry sweep and not worth warning about
    return false;
  }

  // Check whether there are any files that are expired; expired files are always removed
  nextExpiry = now + CACHE_CHECK_INTERVAL;
  for (const auto & fhr : attributeMap) {
    const std::string & fn = fhr.first;
    const cacheAttribute & ca = fhr.second;
    if (!ca.expiry) { continue; }
    if (ca.expiry > now) {
      if (nextExpiry > ca.expiry) { nextExpiry = ca.expiry; }
      continue;
    }
    INSANE_MSG("Marking %s for deletion because it expired", fn.c_str());
    markedForDeletion.insert(fn);
    clearedSize += ca.fileSize;
  }

  // If no additional size is needed, we are done after removing the expired files
  if (!sizeNeeded) {
    if (markedForDeletion.empty()) return true; // avoid spamming the log on the periodic expiry sweep
    const uint64_t expiredSize = removeMarkedFiles(markedForDeletion);
    currentSize = currentSize >= expiredSize ? currentSize - expiredSize : 0;
    if (expiredSize) { MEDIUM_MSG("Removed %" PRIu64 "b of expired cache files", expiredSize); }
    return true;
  }

  // Update the expiryInterval, if it's running already
  if (expiryInterval != -1) {
    uint64_t now = Util::unixMS();
    if (nextExpiry <= now) { evLp.rescheduleInterval(expiryInterval, 1); }
    evLp.rescheduleInterval(expiryInterval, nextExpiry - now);
  }

  // Recompute the scores after attribute changes, or periodically as decay moves files around in the ranking
  if (scores.empty() || !lastScoreComputation || now - lastScoreComputation >= CACHE_CHECK_INTERVAL) {
    scores.clear();

    // Compute the modified GDSF key for each file; never-hit files have a key of zero and sort to the front
    for (const auto & fhr : attributeMap) {
      const std::string & fn = fhr.first;
      const cacheAttribute & ca = fhr.second;
      scores.emplace(scores.end(), (uint64_t)(ca.score(now)), fn);
    }
    std::sort(scores.begin(), scores.end());
    lastScoreComputation = now;
  }

  // Stop deleting once we have enough space in the cache, otherwise mark more low-key files for deletion
  while (scores.size() && clearedSize < sizeNeeded) {
    std::string fn = scores[0].second;
    scores.erase(scores.begin());
    auto it = attributeMap.find(fn);
    if (it == attributeMap.end()) continue;
    const cacheAttribute & ca = it->second;
    // Expired files were already marked earlier, do not count their size twice
    const bool alreadyMarked = !markedForDeletion.insert(fn).second;
    if (alreadyMarked) continue;
    INSANE_MSG("Marking %s (size=%" PRIu64 ") for deletion as we need space in the cache", fn.c_str(), ca.fileSize);
    clearedSize += ca.fileSize;
  }

  // Remove all the files that are marked for deletion, also close the file if it is currently open
  clearedSize = removeMarkedFiles(markedForDeletion);
  currentSize = currentSize >= clearedSize ? currentSize - clearedSize : 0;

  uint64_t trueMax = maxPercentBytes;
  if (!trueMax || (maxBytes && maxBytes < trueMax)) { trueMax = maxBytes; }
  if (!trueMax) { trueMax = fsSize; }
  MEDIUM_MSG("Cache was full, removed %" PRIu64 "b of data, cache is now %.2f%% full", clearedSize, (currentSize * 100.0) / trueMax);

  if (clearedSize < sizeNeeded) {
    WARN_MSG("Cache overfull and could not clear! (%" PRIu64 " < %" PRIu64 ")", clearedSize, sizeNeeded);
    return false;
  }
  return true;
}

void checkSize() {
  if (maxBytes && currentSize > maxBytes) { clearCache(currentSize - maxBytes); }
  if (maxPercentBytes && currentSize > maxPercentBytes) { clearCache(currentSize - maxPercentBytes); }
}

/// Retrieves cache settings, initialises the cache, and creates an inotify instance using the event loop
int main(int argc, char **argv) {
  evLp.setup();
  Util::redirectLogsIfNeeded();

  // Parse the arguments, if not present retrieve them from the global config
  Util::Config conf = Util::Config(argv[0]);
  conf.addOption("bytes", R"-({ "arg":"integer", "short":"b", "long":"bytes", "default":0, "help":"Size in bytes."})-");
  conf.addOption("percent", R"-({ "arg":"integer", "short":"P", "long":"percent", "default":0, "help":"Size in percent."})-");
  conf.addOption("path", R"-({ "arg":"string", "short":"p", "long":"path", "default":"", "help":"Cache directory."})-");
  conf.addOption("stream", R"-({ "arg":"string", "short":"s", "long":"stream", "default":"", "help":"Wipe this stream from the cache and exit."})-");
  conf.parseArgs(argc, argv);
  std::string path = conf.getString("path");
  std::string streamName = conf.getString("stream");
  maxBytes = conf.getInteger("bytes");
  maxPercent = conf.getInteger("percent");
  bool serviceMode = false;

  if (path.empty()) {
    serviceMode = true;
    evLp.addInterval([&path, &conf]() {
      JSON::Value confCache = Util::getGlobalConfig("cachePath");
      if (confCache.isString() && confCache) {
        std::string newDir = confCache.asStringRef();
        if (*newDir.rbegin() != '/') newDir += '/';
        if (path.size() && path != newDir) {
          INFO_MSG("Changed cache directory from '%s' to '%s' -> exiting", path.c_str(), confCache.asStringRef().c_str());
          conf.is_active = false;
          return 0;
        }
        if (!path.size()) {
          path = newDir;
          INFO_MSG("Cache path set to %s", path.c_str());
        }
      }
      JSON::Value confBytes = Util::getGlobalConfig("cacheBytes");
      if (confBytes.isInt() && confBytes.asInt() != maxBytes) {
        INFO_MSG("Changed cache byte-size from %" PRIu64 " to %" PRIu64, maxBytes, confBytes.asInt());
        maxBytes = confBytes.asInt();
        checkSize();
      }
      JSON::Value confPercent = Util::getGlobalConfig("cachePercent");
      if (confPercent.isInt() && confPercent.asDouble() / 1000.0 != maxPercent) {
        if (fsSize) {
          maxPercentBytes = (confPercent.asDouble() / 100000.0) * fsSize;
          INFO_MSG("Changed cache percent-size from %.3f to %.3f ( = %" PRIu64 "b)", maxPercent,
                   confPercent.asDouble() / 1000.0, maxPercentBytes);
        } else {
          INFO_MSG("Changed cache percent-size from %.3f to %.3f", maxPercent, confPercent.asDouble() / 1000.0);
        }
        maxPercent = confPercent.asDouble() / 1000.0;
        checkSize();
      }
      return 5000;
    }, 0);
  } else {
    if (path[0] != '/') { path = HTTP::localURIResolver().link(path).getFilePath(); }
    if (!maxBytes && !maxPercent && !streamName.size()) {
      FAIL_MSG("Running the cache utility without a stream name to delete and without a maximum size, but with a "
               "manually set cache path makes no sense: it would do nothing. Aborting.");
      return 1;
    }
  }
  if (path.empty()) {
    FAIL_MSG("No cache directory set; exiting");
    return 1;
  }
  if (*path.rbegin() != '/') path += '/';

  // Start setting up inotify and the event loop
  inotFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (inotFd == -1) {
    ERROR_MSG("Could not initialise inotify instance: %s", strerror(errno));
    return 1;
  }

  struct stat st;
  if (stat(path.c_str(), &st) == -1) {
    // Cache initialisation first case where cache does not exist yet and we only have to create the directory for it
    if (errno != ENOENT || mkdir(path.c_str(), ACCESSPERMS) != 0) {
      ERROR_MSG("Could not open cache directory at '%s': %s", path.c_str(), strerror(errno));
      return 1;
    }
    INFO_MSG("Created new cache directory at '%s'", path.c_str());
    addWatch(path, IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ONLYDIR, wdTop);
  } else {
    // Cache initialisation second case where we are initialising with files that already have metadata
    initCache(path);
    checkSize();
  }

  if (!streamName.empty()) {
    std::vector<std::string> markedForDeletion;
    for (const auto & it : attributeMap) {
      const std::string & filePath = it.first;
      if (it.second.stream == streamName) { markedForDeletion.push_back(filePath); }
    }

    INFO_MSG("Wiping %zu cached files for stream %s", markedForDeletion.size(), streamName.c_str());
    for (const auto & i : markedForDeletion) {
      if (unlink(i.c_str()) != 0 && errno != ENOENT) {
        ERROR_MSG("Could not remove cached file '%s': %s", i.c_str(), strerror(errno));
        continue;
      }
      removeAttribute(i);
    }
    return 0;
  }

  Socket::UDPConnection uSock(true);
  std::map<std::string, uint64_t> recentHits;
  if (serviceMode) {
    uint16_t boundPort = 0;
    HTTP::URL udpAddr("udp://localhost:14242");
    if (getenv("UDP_CACHE")) { udpAddr = HTTP::URL(getenv("UDP_CACHE")); }
    boundPort = uSock.bind(udpAddr.getPort(), udpAddr.host);
    if (!boundPort) {
      boundPort = uSock.bind(0, udpAddr.host);
      if (!boundPort) {
        std::stringstream newHost;
        char ranNums[3];
        Util::getRandomBytes(ranNums, 3);
        newHost << "127." << (int)ranNums[0] << "." << (int)ranNums[1] << "." << (int)ranNums[2];
        boundPort = uSock.bind(0, newHost.str());
        WARN_MSG("Could not open API port on any port on %s - bound instead to %s", udpAddr.host.c_str(),
                 uSock.getBoundAddr().toString().c_str());
      } else {
        WARN_MSG("Could not open local UDP socket on %s:%" PRIu16 " - bound to ephemeral port %" PRIu16 " instead",
                 udpAddr.host.c_str(), udpAddr.getPort(), boundPort);
      }
    } else {
      INFO_MSG("Local UDP socket bound on %s", uSock.getBoundAddr().toString().c_str());
    }
    if (boundPort) {
      HTTP::URL boundAddr;
      boundAddr.protocol = "udp";
      boundAddr.setPort(boundPort);
      boundAddr.host = uSock.getBoundAddr().host();
      Util::sendUDPApi(JSONOBJECT({{"cache_udp_socket", boundAddr.getUrl()}}));
      evLp.addSocket(uSock.getSock(), [&](void *) {
        while (uSock.Receive()) {
          if (!uSock.data.size() || uSock.data[0] != '{') { continue; }
          MEDIUM_MSG("UDP API: %.*s", (int)uSock.data.size(), (const char *)uSock.data);
          JSON::Value R(PARSEJSON, uSock.data, uSock.data.size());
          if (R.isMember("hit")) { ++recentHits[R["hit"]]; }
        }
      }, 0);
      // Once per minute, process decay and write updated frequency values if cache hits happened for a file
      evLp.addInterval([&]() {
        if (recentHits.size()) {
          uint64_t now = Util::unixMS();
          for (const auto & H : recentHits) {
            if (!attributeMap.count(H.first)) { continue; }
            cacheAttribute & attr = attributeMap[H.first];
            uint64_t newScore = attr.freq(now) + H.second * 1024;
            int tempFd = open(H.first.c_str(), O_RDWR);
            if (tempFd != -1) {
              Cache::setFileAttr(tempFd, "user.mist.frequency", newScore);
              Cache::setFileAttr(tempFd, "user.mist.lastdecay", now);
              close(tempFd);
            }
          }
          recentHits.clear();
        }
        return 60000;
      }, 60000);
    } else {
      WARN_MSG("Could not open local UDP socket; not running cache hit tracking");
    }
  }

  evLp.addSocket(inotFd, [&](void *) {
    char data[4096];
    ssize_t data_len;
    const struct inotify_event *event;
    bool rescanNeeded = false;

    while ((data_len = read(inotFd, data, sizeof(data))) != -1) {
      // Read each event that was written to the inotify instance
      for (char *ptr = data; ptr < data + data_len; ptr += sizeof(struct inotify_event) + event->len) {
        event = (const struct inotify_event *)ptr;
        if (event->mask & IN_Q_OVERFLOW) {
          WARN_MSG("Cache inotify queue overflowed; filesystem accounting will be rebuilt");
          rescanNeeded = true;
          break;
        }

        // Retrieve path for this watch descriptor
        std::unordered_map<int, std::string>::iterator watchPath = watchPaths.find(event->wd);
        if (watchPath == watchPaths.end()) continue;

        // Remove watches which inotify has stopped monitoring
        if (event->mask & IN_IGNORED) {
          wdTop.erase(event->wd);
          wdLower.erase(event->wd);
          wdFiles.erase(event->wd);
          watchPaths.erase(watchPath);
          continue;
        }

        std::string eventPath = watchPath->second;
        if (event->len) eventPath += event->name;

        // Top directory and middle-level directories only contain directories, lower-level directories only files
        if (wdTop.count(event->wd)) {
          if ((event->mask & (IN_CREATE | IN_MOVED_TO)) && (event->mask & IN_ISDIR)) {
            eventPath += '/';
            scanCacheDirectory(eventPath, watchPath->second == path ? 1 : 2);
          }
          continue;
        }

        // Watch newly-created files and remove deleted files from the attribute map
        if (wdLower.count(event->wd)) {
          if ((event->mask & (IN_MOVED_TO | IN_CLOSE_WRITE)) && !(event->mask & IN_ISDIR)) {
            if (stat(eventPath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
              addWatch(eventPath, IN_MOVED_TO | IN_MODIFY | IN_ATTRIB | IN_DELETE | IN_DELETE_SELF, wdFiles);
              updateAttribute(eventPath);
            }
          }
          if ((event->mask & (IN_DELETE | IN_MOVED_FROM)) && !(event->mask & IN_ISDIR)) removeAttribute(eventPath);
          continue;
        }

        if (!wdFiles.count(event->wd)) continue;

        // Files may be deleted using IN_DELETE_SELF rather than IN_DELETE
        if (event->mask & IN_DELETE_SELF) {
          removeAttribute(eventPath);
          continue;
        }

        // Recreate the attributes after changes or after we are done writing
        if (event->mask & (IN_MODIFY | IN_MOVED_TO | IN_ATTRIB)) {
          if (stat(eventPath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
          updateAttribute(eventPath);
        }
      }
    }
    if (rescanNeeded) {
      rebuildCache(path);
      rescanNeeded = false;
    }
    checkSize();
  }, 0);

  // Periodically sweep for expired files and update the size of the cache
  expiryInterval = evLp.addInterval([]() {
    clearCache();
    uint64_t now = Util::unixMS();
    if (nextExpiry <= now) { return (size_t)1; }
    return (size_t)(nextExpiry - now);
  }, nextExpiry);

  conf.activate();
  while (conf.is_active) evLp.await(10000);
  return 0;
}
