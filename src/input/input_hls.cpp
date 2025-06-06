#include "input_hls.h"
#include <mist/defines.h>
#include <thread>
#include <mutex>

#define SEM_TS_CLAIM "/MstTSIN%s"

/// Local RAM buffer for recently accessed segments
std::map<Mist::playListEntries, Util::ResizeablePointer> segBufs;
/// Order of adding/accessing for local RAM buffer of segments
std::deque<Mist::playListEntries> segBufAccs;
/// Order of adding/accessing sizes for local RAM buffer of segments
std::deque<size_t> segBufSize;
size_t segBufTotalSize = 0;

/// Read data for a segment, update buffer sizes to match
bool readNext(Mist::SegmentReader & S, DTSC::Packet & thisPacket, uint64_t bytePos){
  //Overwrite the current segment size
  segBufTotalSize -= segBufSize.front();
  bool ret = S.readNext(thisPacket, bytePos);
  segBufSize.front() = S.getDataCallbackPos();
  segBufTotalSize += segBufSize.front();
  return ret;
}

/// Load a new segment, use cache if possible or create a new cache entry
bool loadSegment(Mist::SegmentReader & S, const Mist::playListEntries & entry){
  if (!segBufs.count(entry)){
    HIGH_MSG("Reading non-cache: %s", entry.shortName().c_str());
    //Remove cache entries while above 16MiB in total size, unless we only have 1 entry (we keep two at least at all times)
    while (segBufTotalSize > 16 * 1024 * 1024 && segBufs.size() > 1){
      HIGH_MSG("Dropping from segment cache: %s", segBufAccs.back().shortName().c_str());
      segBufs.erase(segBufAccs.back());
      segBufTotalSize -= segBufSize.back();
      segBufAccs.pop_back();
      segBufSize.pop_back();
    }
    segBufAccs.push_front(entry);
    segBufSize.push_front(0);
  }else{
    HIGH_MSG("Reading from cache: %s", entry.shortName().c_str());
    // Ensure current entry is the front entry in the deques
    std::deque<Mist::playListEntries> segBufAccsCopy = segBufAccs;
    std::deque<size_t> segBufSizeCopy = segBufSize;
    segBufAccs.clear();
    segBufSize.clear();
    size_t thisSize = 0;
    
    while (segBufSizeCopy.size()){
      if (segBufAccsCopy.back() == entry){
        thisSize = segBufSizeCopy.back();
      }else{
        segBufAccs.push_front(segBufAccsCopy.back());
        segBufSize.push_front(segBufSizeCopy.back());
      }
      segBufAccsCopy.pop_back();
      segBufSizeCopy.pop_back();
    }
    segBufAccs.push_front(entry);
    segBufSize.push_front(thisSize);
  }
  return S.load(entry.filename, entry.startAtByte, entry.stopAtByte, entry.ivec, entry.keyAES, &(segBufs[entry]));
}




static uint64_t ISO8601toUnixmillis(const std::string &ts){
  // Format examples:
  //  2019-12-05T09:41:16.765000+00:00
  //  2019-12-05T09:41:16.765Z
  uint64_t unxTime = 0;
  const size_t T = ts.find('T');
  if (T == std::string::npos){
    ERROR_MSG("Timestamp is date-only (no time marker): %s", ts.c_str());
    return 0;
  }
  const size_t Z = ts.find_first_of("Z+-", T);
  const std::string date = ts.substr(0, T);
  std::string time;
  std::string zone;
  if (Z == std::string::npos){
    WARN_MSG("HLS segment timestamp is missing timezone information! Assumed to be UTC.");
    time = ts.substr(T + 1);
  }else{
    time = ts.substr(T + 1, Z - T - 1);
    zone = ts.substr(Z);
  }
  unsigned long year, month, day;
  if (sscanf(date.c_str(), "%lu-%lu-%lu", &year, &month, &day) != 3){
    ERROR_MSG("Could not parse date: %s", date.c_str());
    return 0;
  }
  unsigned int hour, minute;
  double seconds;
  if (sscanf(time.c_str(), "%u:%d:%lf", &hour, &minute, &seconds) != 3){
    ERROR_MSG("Could not parse time: %s", time.c_str());
    return 0;
  }
  // Fill the tm struct with the values we just read.
  // We're ignoring time zone for now, and forcing seconds to zero since we add them in later with more precision
  struct tm tParts;
  tParts.tm_sec = 0;
  tParts.tm_min = minute;
  tParts.tm_hour = hour;
  tParts.tm_mon = month - 1;
  tParts.tm_year = year - 1900;
  tParts.tm_mday = day;
  tParts.tm_isdst = 0;
  // convert to unix time, in seconds
  unxTime = timegm(&tParts);
  // convert to milliseconds
  unxTime *= 1000;
  // finally add the seconds (and milliseconds)
  unxTime += (seconds * 1000);

  // Now, adjust for time zone if needed
  if (zone.size() && zone[0] != 'Z'){
    bool sign = (zone[0] == '+');
    {
      unsigned long hrs, mins;
      if (sscanf(zone.c_str() + 1, "%lu:%lu", &hrs, &mins) == 2){
        if (sign){
          unxTime += mins * 60000 + hrs * 3600000;
        }else{
          unxTime -= mins * 60000 + hrs * 3600000;
        }
      }else if (sscanf(zone.c_str() + 1, "%lu", &hrs) == 1){
        if (hrs > 100){
          if (sign){
            unxTime += (hrs % 100) * 60000 + ((uint64_t)(hrs / 100)) * 3600000;
          }else{
            unxTime -= (hrs % 100) * 60000 + ((uint64_t)(hrs / 100)) * 3600000;
          }
        }else{
          if (sign){
            unxTime += hrs * 3600000;
          }else{
            unxTime -= hrs * 3600000;
          }
        }
      }else{
        WARN_MSG("Could not parse time zone '%s'; assuming UTC", zone.c_str());
      }
    }
  }
  DONTEVEN_MSG("Time '%s' = %" PRIu64, ts.c_str(), unxTime);
  return unxTime;
}

namespace Mist{
  /// Save playlist objects for manual reloading
  std::map<uint64_t, Playlist*> playlistMapping;

  /// Track which segment numbers have been parsed
  std::map<uint64_t, uint64_t> parsedSegments;

  /// Mutex for accesses to listEntries
  std::mutex entryMutex;

  JSON::Value playlist_urls; ///< Relative URLs to the various playlists

  static unsigned int plsTotalCount = 0; /// Total playlists active
  static unsigned int plsInitCount = 0;  /// Count of playlists fully inited

  bool streamIsLive; //< Playlist can be sliding window or get new segments appended
  bool streamIsVOD; //< Playlist segments do not disappear
  uint32_t globalWaitTime; //< Time between playlist reloads, based on TARGETDURATION
  std::map<uint32_t, std::deque<playListEntries> > listEntries; //< Segments currently in the playlist

  // These are used in the HTTP::Downloader callback, to prevent timeouts when downloading
  // segments/playlists.
  InputHLS *self = 0;
  bool callbackFunc(uint8_t){return self->callback();}

  /// Called by the global callbackFunc, to prevent timeouts
  bool InputHLS::callback(){
    keepAlive();
    return config->is_active;
  }

  /// Helper function that removes trailing \r characters
  void cleanLine(std::string &s){
    if (s.length() > 0 && s.at(s.length() - 1) == '\r'){s.erase(s.size() - 1);}
  }

  Playlist::Playlist(const std::string &uriSource){
    nextUTC = 0;
    oUTC = 0;
    id = 0; // to be set later
    //If this is the copy constructor, just be silent.
    std::string uriSrc;
    if (uriSource.find('\n') != std::string::npos){
      uriSrc = uriSource.substr(0, uriSource.find('\n'));
      relurl = uriSource.substr(uriSource.find('\n') + 1);
    }else{
      uriSrc = uriSource;
    }
    if (uriSrc.size()){INFO_MSG("Adding variant playlist: %s -> %s", relurl.c_str(), uriSrc.c_str());}
    lastSegment = 0;
    waitTime = 2;
    noChangeCount = 0;
    lastTimestamp = 0;
    root = HTTP::URL(uriSrc);
    if (root.isLocalPath()){
      uri = root.getFilePath();
    }else{
      uri = root.getUrl();
    }
    memset(keyAES, 0, 16);
    startTime = Util::bootSecs();
    reloadNext = 0;
    firstIndex = 0;
  }

  /// Returns true if there is no protocol defined in the playlist root URL.
  bool Playlist::isUrl() const{return root.protocol.size();}

  void parseKey(std::string key, char *newKey, unsigned int len){
    memset(newKey, 0, len);
    for (size_t i = 0; i < key.size() && i < (len << 1); ++i){
      char c = key[i];
      newKey[i >> 1] |= ((c & 15) + (((c & 64) >> 6) | ((c & 64) >> 3))) << ((~i & 1) << 2);
    }
  }

  /// Handles both initial load and future reloads.
  /// Returns how many segments were added to the internal segment list.
  bool Playlist::reload(){
    uint64_t bposCounter = 1;
    nextUTC = 0; // Make sure we don't use old timestamps
    std::string line;
    std::string key;
    std::string val;
    float segDur = 0.0;
    uint64_t startByte = std::string::npos, lenByte = 0;

    std::string keyMethod;
    std::string keyUri;
    std::string keyIV;

    std::string mapUri;
    std::string mapRange;

    int count = 0;

    std::istringstream urlSource;

    HTTP::URIReader plsDL;
    plsDL.open(uri);
    char * dataPtr;
    size_t dataLen;
    plsDL.readAll(dataPtr, dataLen);
    if (!dataLen){
      FAIL_MSG("Could not download playlist '%s', aborting.", uri.c_str());
      reloadNext = Util::bootSecs() + waitTime;
      return false;
    }
    urlSource.str(std::string(dataPtr, dataLen));

    std::istream &input = (std::istream &)urlSource;
    std::getline(input, line);

    {// Mutex scope
      // Block the main thread from reading listEntries and firstIndex
      std::lock_guard<std::mutex> guard(entryMutex);
      DONTEVEN_MSG("Reloading playlist '%s'", uri.c_str());
      while (std::getline(input, line)){
        DONTEVEN_MSG("Parsing line '%s'", line.c_str());
        cleanLine(line);
        if (line.empty()){continue;}// skip empty lines

        if (line.compare(0, 7, "#EXTINF") == 0){
          segDur = atof(line.c_str() + 8);
          continue;
        }
        if (line.compare(0, 7, "#EXT-X-") == 0){
          size_t pos = line.find(":");
          key = line.substr(7, pos - 7);
          val = line.c_str() + pos + 1;

          if (key == "KEY"){
            size_t tmpPos = val.find("METHOD=");
            size_t tmpPos2 = val.substr(tmpPos).find(",");
            keyMethod = val.substr(tmpPos + 7, tmpPos2 - tmpPos - 7);

            tmpPos = val.find("URI=\"");
            tmpPos2 = val.substr(tmpPos + 5).find("\"");
            keyUri = val.substr(tmpPos + 5, tmpPos2);

            tmpPos = val.find("IV=");
            if (tmpPos != std::string::npos){keyIV = val.substr(tmpPos + 5, 32);}

            // when key not found, download and store it in the map
            if (!keys.count(keyUri)){
              HTTP::URIReader keyDL;
              if (!keyDL.open(root.link(keyUri)) || !keyDL){
                FAIL_MSG("Could not retrieve decryption key from '%s'", root.link(keyUri).getUrl().c_str());
                continue;
              }
              char *keyPtr;
              size_t keyLen;
              keyDL.readAll(keyPtr, keyLen);
              if (!keyLen){
                FAIL_MSG("Could not retrieve decryption key from '%s'", root.link(keyUri).getUrl().c_str());
                continue;
              }
              keys.insert(std::pair<std::string, std::string>(keyUri, std::string(keyPtr, keyLen)));
            }
            continue;
          }

          if (key == "BYTERANGE"){
            size_t atSign = val.find('@');
            if (atSign != std::string::npos){
              std::string len = val.substr(0, atSign);
              std::string pos = val.substr(atSign+1);
              lenByte = atoll(len.c_str());
              startByte = atoll(pos.c_str());
            }else{
              lenByte = atoll(val.c_str());
            }
            continue;
          }

          if (key == "TARGETDURATION"){
            waitTime = atoi(val.c_str()) / 2;
            if (waitTime < 2){waitTime = 2;}
            continue;
          }

          // Assuming this always comes before any segment
          if (key == "MEDIA-SEQUENCE"){
            // Reinit the segment counter
            firstIndex = atoll(val.c_str());
            bposCounter = firstIndex + 1;
            continue;
          }

          if (key == "PROGRAM-DATE-TIME"){
            nextUTC = ISO8601toUnixmillis(val);
            continue;
          }

          if (key == "MAP"){
            size_t mapLen = 0, mapOffset = 0;
            size_t tmpPos = val.find("BYTERANGE=\"");
            if (tmpPos != std::string::npos){
              size_t tmpPos2 = val.substr(tmpPos).find('"');
              mapRange = val.substr(tmpPos + 11, tmpPos2 - tmpPos - 11);

              size_t atSign = mapRange.find('@');
              if (atSign != std::string::npos){
                std::string len = mapRange.substr(0, atSign);
                std::string pos = mapRange.substr(atSign+1);
                mapLen = atoll(len.c_str());
                mapOffset = atoll(pos.c_str());
              }else{
                mapLen = atoll(val.c_str());
              }
            }

            tmpPos = val.find("URI=\"");
            if (tmpPos != std::string::npos){
              size_t tmpPos2 = val.substr(tmpPos + 5).find('"');
              mapUri = val.substr(tmpPos + 5, tmpPos2);
            }

            // when key not found, download and store it in the map
            if (!maps.count(mapUri+mapRange)){
              HTTP::URIReader mapDL;
              if (!mapDL.open(root.link(mapUri)) || !mapDL){
                FAIL_MSG("Could not retrieve map from '%s'", root.link(mapUri).getUrl().c_str());
                continue;
              }
              char *mapPtr;
              size_t mapPLen;
              mapDL.readAll(mapPtr, mapPLen);
              if (mapOffset){
                if (mapOffset <= mapPLen){
                  mapPtr += mapOffset;
                  mapPLen -= mapOffset;
                }else{
                  mapPLen = 0;
                }
              }
              if (!mapLen){mapLen = mapPLen;}
              if (mapLen < mapPLen){mapPLen = mapLen;}
              if (!mapPLen){
                FAIL_MSG("Could not retrieve map from '%s'", root.link(mapUri).getUrl().c_str());
                continue;
              }
              maps.insert(std::pair<std::string, std::string>(mapUri+mapRange, std::string(mapPtr, mapPLen)));
            }
            continue;
          }


          if (key == "PLAYLIST-TYPE"){
            if (val == "VOD"){
              streamIsVOD = true;
              streamIsLive = false;
            }else if (val == "LIVE"){
              streamIsVOD = false;
              streamIsLive = true;
            }else if (val == "EVENT"){
              streamIsVOD = true;
              streamIsLive = true;
            }
            continue;
          }

          // Once we see this tag, the entire playlist becomes VOD
          if (key == "ENDLIST"){
            streamIsLive = false;
            streamIsVOD = true;
            continue;
          }
          VERYHIGH_MSG("ignoring line: %s.", line.c_str());
          continue;
        }
        if (line[0] == '#'){
          VERYHIGH_MSG("ignoring line: %s.", line.c_str());
          continue;
        }

        // check for already added segments
        VERYHIGH_MSG("Current segment #%" PRIu64 ", last segment was #%" PRIu64 "", bposCounter, lastSegment);
        if (bposCounter > lastSegment){
          INFO_MSG("Playlist #%u: Adding new segment #%" PRIu64 " to playlist entries", id, bposCounter);
          char ivec[16];
          if (keyIV.size()){
            parseKey(keyIV, ivec, 16);
          }else{
            memset(ivec, 0, 16);
            Bit::htobll(ivec + 8, bposCounter);
          }
          addEntry(root.link(line).getUrl(), line, segDur, bposCounter, keys[keyUri], std::string(ivec, 16), mapUri+mapRange, startByte, lenByte);
          lastSegment = bposCounter;
          ++count;
        }
        nextUTC = 0;
        segDur = 0.0;
        startByte = std::string::npos;
        lenByte = 0;
        ++bposCounter;
      }// Mutex scope
    }

    if (globalWaitTime < waitTime){globalWaitTime = waitTime;}

    reloadNext = Util::bootSecs() + waitTime;
    return (count > 0);
  }

  /// Adds playlist segments to be processed
  void Playlist::addEntry(const std::string &absolute_filename, const std::string &filename, float duration, uint64_t &bpos,
                          const std::string &key, const std::string &iv, const std::string mapName, uint64_t startByte, uint64_t lenByte){
    playListEntries entry;
    entry.filename = absolute_filename;
    entry.relative_filename = filename;
    entry.mapName = mapName;
    cleanLine(entry.filename);
    entry.bytePos = bpos;
    entry.duration = duration;
    if (entry.duration * 1000 > DTSC::veryUglyJitterOverride){
      DTSC::veryUglyJitterOverride = entry.duration * 1000;
    }

    if (id && listEntries[id].size()){
      // If the UTC has gone backwards, shift forward.
      playListEntries & prev = listEntries[id].back();
      if (nextUTC && prev.mUTC && nextUTC < prev.mUTC + prev.duration * 1000){
        WARN_MSG("UTC time went from %s to %s; adjusting!", Util::getUTCStringMillis(prev.mUTC + prev.duration * 1000).c_str(), Util::getUTCStringMillis(nextUTC).c_str());
        // Reset UTC time, this will cause the next check to set it correctly
        nextUTC = 0;
      }
      // If we ever had a UTC time, ensure it's set for all segments going forward
      if (!nextUTC && prev.mUTC){
        nextUTC = prev.mUTC + (uint64_t)(prev.duration * 1000);
      }
      // If startByte unknown and we have a length, calculate it from previous entry
      if (startByte == std::string::npos && lenByte){
        if (filename == prev.relative_filename){startByte = prev.stopAtByte;}
      }
    }else{
      // If startByte unknown and we have a length, set to zero
      if (startByte == std::string::npos && lenByte){startByte = 0;}
    }
    if ((lenByte && startByte == std::string::npos) || (!lenByte && startByte != std::string::npos)){
      WARN_MSG("Invalid byte range entry for segment: %s", filename.c_str());
      lenByte = 0;
      startByte = std::string::npos;
    }
    if (lenByte){
      entry.startAtByte = startByte;
      entry.stopAtByte = startByte + lenByte;
    }

    entry.mUTC = nextUTC;
    if (nextUTC && !oUTC){
      oUTC = nextUTC - (lastTimestamp + startTime);
    }

    if (key.size() && iv.size()){
      memcpy(entry.ivec, iv.data(), 16);
      memcpy(entry.keyAES, key.data(), 16);
    }else{
      memset(entry.ivec, 0, 16);
      memset(entry.keyAES, 0, 16);
    }

    // Base timestamp of entry on UTC time if we have it, otherwise on a simple addition of the previous duration
    if (nextUTC && oUTC){
      entry.timestamp = nextUTC - oUTC;
    }else{
      entry.timestamp = lastTimestamp + startTime;
    }
    lastTimestamp = entry.timestamp - startTime + duration;

    // Set a playlist ID if we haven't assigned one yet.
    // Note: This method requires never removing playlists, only adding.
    // The mutex assures we have a unique count/number.
    if (!id){id = listEntries.size() + 1;}
    if (entry.startAtByte){
      HIGH_MSG("Adding entry '%s' (%" PRIu64 "-%" PRIu64 ") to ID %u", filename.c_str(), entry.startAtByte, entry.stopAtByte, id);
    }else{
      HIGH_MSG("Adding entry '%s' to ID %u", filename.c_str(), id);
    }
    playlist_urls[JSON::Value(id).asString()] = relurl;
    listEntries[id].push_back(entry);
  }

  /// Constructor of HLS Input
  InputHLS::InputHLS(Util::Config *cfg) : Input(cfg){
    zUTC = 0;
    self = this;
    streamIsLive = true; //< default to sliding window playlist
    streamIsVOD = false; //< default to sliding window playlist
    globalWaitTime = 0;
    currentPlaylist = 0;
    streamOffset = 0;
    isInitialRun = false;
    segDowner.onProgress(callbackFunc);

    pidCounter = 1;

    previousSegmentIndex = -1;
    currentIndex = 0;

    capa["name"] = "HLS";
    capa["desc"] = "This input allows you to both play Video on Demand and live HLS streams stored "
                   "on the filesystem, as well as pull live HLS streams over HTTP and HTTPS.";
    capa["source_match"].append("/*.m3u8");
    capa["source_match"].append("/*.m3u");
    capa["source_match"].append("http://*.m3u8");
    capa["source_match"].append("http://*.m3u");
    capa["source_match"].append("https://*.m3u8");
    capa["source_match"].append("https://*.m3u");
    capa["source_match"].append("https-hls://*");
    capa["source_match"].append("http-hls://*");
    capa["source_match"].append("s3+http://*.m3u8");
    capa["source_match"].append("s3+http://*.m3u");
    capa["source_match"].append("s3+https://*.m3u8");
    capa["source_match"].append("s3+https://*.m3u");
    capa["source_match"].append("s3+https-hls://*");
    capa["source_match"].append("s3+http-hls://*");

    // All URLs can be set to always-on mode.
    capa["always_match"] = capa["source_match"];

    capa["priority"] = 9;
    capa["codecs"]["video"].append("H264");
    capa["codecs"]["audio"].append("AAC");
    capa["codecs"]["audio"].append("AC3");
    capa["codecs"]["audio"].append("MP3");

    JSON::Value option;
    option["arg"] = "integer";
    option["long"] = "buffer";
    option["short"] = "b";
    option["help"] = "Live buffer window in ms. Segments within this range from the live point will be kept in memory";
    option["value"].append(50000);
    config->addOption("bufferTime", option);
    capa["optional"]["bufferTime"]["name"] = "Buffer time (ms)";
    capa["optional"]["bufferTime"]["help"] =
        "Live buffer window in ms. Segments within this range from the live point will be kept in memory";
    capa["optional"]["bufferTime"]["option"] = "--buffer";
    capa["optional"]["bufferTime"]["type"] = "uint";
    capa["optional"]["bufferTime"]["default"] = 50000;
    option.null();

  }

  bool InputHLS::checkArguments(){
    config->is_active = true;
    if (config->getString("input") == "-"){
      return false;
    }

    if (!initPlaylist(config->getString("input"), false)){
      Util::logExitReason(ER_UNKNOWN, "Failed to load HLS playlist, aborting");
      return false;
    }
    
    // Segments can be added (and removed if VOD is false)
    meta.setLive(streamIsLive);
    // Segments can not be removed
    meta.setVod(streamIsVOD);

    return true;
  }

  void InputHLS::postHeader(){
    // Run continuous playlist updaters after the main thread is forked
    if (!initPlaylist(config->getString("input"), true)){
      Util::logExitReason(ER_UNKNOWN, "Failed to load HLS playlist, aborting");
      config->is_active = false;
    }
  }

  bool InputHLS::readExistingHeader(){
    if (!Input::readExistingHeader()){
      INFO_MSG("Could not read existing header, regenerating");
      return false;
    }
    if (!M.inputLocalVars.isMember("version") || M.inputLocalVars["version"].asInt() < 4){
      INFO_MSG("Header needs update, regenerating");
      return false;
    }
    // Check if the DTSH file contains all expected data
    if (!M.inputLocalVars.isMember("playlistEntries")){
      INFO_MSG("Header needs update as it contains no playlist entries, regenerating");
      return false;
    }
    if (!M.inputLocalVars.isMember("playlist_urls")){
      INFO_MSG("Header needs update as it contains no playlist URLs, regenerating");
      return false;
    }
    if (!M.inputLocalVars.isMember("pidMappingR")){
      INFO_MSG("Header needs update as it contains no packet id mappings, regenerating");
      return false;
    }
    // Recover playlist entries
    std::lock_guard<std::mutex> guard(entryMutex);
    HTTP::URL root = HTTP::localURIResolver().link(config->getString("input"));
    jsonForEachConst(M.inputLocalVars["playlistEntries"], i){
      uint64_t plNum = JSON::Value(i.key()).asInt();
      if (M.inputLocalVars["playlistEntries"][i.key()].size() > listEntries[plNum].size()){
        INFO_MSG("Header needs update as the amount of segments in the playlist has decreased, regenerating header");
        return false;
      }
      std::deque<playListEntries> newList;
      jsonForEachConst(*i, j){
        const JSON::Value & thisEntry = *j;
        if (thisEntry[1u].asInt() < playlistMapping[plNum]->firstIndex + 1){
          INFO_MSG("Skipping segment %" PRId64 " which is present in the header, but no longer available in the playlist", thisEntry[1u].asInt());
          continue;
        }
        if (thisEntry[1u].asInt() > playlistMapping[plNum]->firstIndex + listEntries[plNum].size()){
          INFO_MSG("Header needs update as the segment index has decreased. The stream has likely restarted, regenerating");
          return false;
        }
        playListEntries newEntry;
        newEntry.relative_filename = thisEntry[0u].asString();
        newEntry.filename = root.link(M.inputLocalVars["playlist_urls"][i.key()]).link(thisEntry[0u].asString()).getUrl();
        newEntry.bytePos = thisEntry[1u].asInt();
        newEntry.mUTC = thisEntry[2u].asInt();
        newEntry.duration = thisEntry[3u].asDouble();
        if (newEntry.duration * 1000 > DTSC::veryUglyJitterOverride){
          DTSC::veryUglyJitterOverride = newEntry.duration * 1000;
        }
        newEntry.timestamp = thisEntry[4u].asInt();
        newEntry.timeOffset = thisEntry[5u].asInt();
        newEntry.wait = thisEntry[6u].asInt();
        if (thisEntry[7u].asString().size() && thisEntry[8u].asString().size()){
          memcpy(newEntry.ivec, thisEntry[7u].asString().data(), 16);
          memcpy(newEntry.keyAES, thisEntry[8u].asString().data(), 16);
        }else{
          memset(newEntry.ivec, 0, 16);
          memset(newEntry.keyAES, 0, 16);
        }
        if (thisEntry.size() >= 11){
          newEntry.startAtByte = thisEntry[9u].asInt();
          newEntry.stopAtByte = thisEntry[10u].asInt();
          if (thisEntry.size() >= 12){
            newEntry.mapName = thisEntry[11u].asStringRef();
          }
        }
        newList.push_back(newEntry);
      }
      listEntries[plNum] = newList;
    }
    // Recover pidMappings
    jsonForEachConst(M.inputLocalVars["pidMappingR"], i){
      uint64_t key = JSON::Value(i.key()).asInt();
      uint64_t val = i->asInt();
      pidMappingR[key] = val;
      pidMapping[val] = key;
    }
    if (M.inputLocalVars.isMember("parsedSegments")){
      jsonForEachConst(M.inputLocalVars["parsedSegments"], i){
        uint64_t key = JSON::Value(i.key()).asInt();
        uint64_t val = i->asInt();
        // If there was a jump in MEDIA-SEQUENCE, start from there
        if (val < playlistMapping[key]->firstIndex){
          INFO_MSG("Detected a jump in MEDIA-SEQUENCE, adjusting segment counter from %" PRIu64 " to %" PRIu64, val, playlistMapping[key]->firstIndex);
          val = playlistMapping[key]->firstIndex;
        }
        parsedSegments[key] = val;
        playlistMapping[key]->lastSegment = val;
        INFO_MSG("Playlist %" PRIu64 " already parsed %" PRIu64 " segments", key, val);
      }

    }
    // Set bootMsOffset in order to display the program time correctly in the player
    zUTC = M.inputLocalVars["zUTC"].asInt();
    meta.setUTCOffset(zUTC);
    if (M.getLive()){meta.setBootMsOffset(streamOffset);}
    return true;
  }

  void InputHLS::parseStreamHeader(){
    streamIsVOD = false;
    readHeader();
  }

  bool InputHLS::readHeader(){
    // to analyse and extract data
    TS::Packet packet; 
    char *data;
    size_t dataLen;
    meta.reInit(isSingular() ? streamName : "");

    std::lock_guard<std::mutex> guard(entryMutex);

    size_t totalSegments = 0, currentSegment = 0;
    for (std::map<uint32_t, std::deque<playListEntries> >::iterator pListIt = listEntries.begin();
         pListIt != listEntries.end(); pListIt++){
      totalSegments += pListIt->second.size();
    }

    for (std::map<uint32_t, std::deque<playListEntries> >::iterator pListIt = listEntries.begin();
         pListIt != listEntries.end() && config->is_active; pListIt++){
      segDowner.reset();
      std::string lastMapName;
      uint32_t entId = 0;
      bool foundAtLeastOnePacket = false;
      VERYHIGH_MSG("Playlist %" PRIu32 " starts at media index %" PRIu64, pListIt->first, playlistMapping[pListIt->first]->firstIndex);

      for (std::deque<playListEntries>::iterator entryIt = pListIt->second.begin();
           entryIt != pListIt->second.end() && config->is_active; entryIt++){
        ++currentSegment;

        if (entryIt->mapName != lastMapName){
          lastMapName = entryIt->mapName;
          segDowner.setInit(playlistMapping[pListIt->first]->maps[lastMapName]);
        }
        if (!loadSegment(segDowner, *entryIt)){
          FAIL_MSG("Failed to load segment - skipping to next");
          continue;
        }
        entId++;
        allowRemap = true;
        DTSC::Packet headerPack;
        while (config->is_active && readNext(segDowner, headerPack, entryIt->bytePos)){
          if (!config->is_active){return false;}
          if (!headerPack){
            continue;
          }
          size_t tmpTrackId = headerPack.getTrackId();
          uint64_t packetId = getPacketID(pListIt->first, tmpTrackId);
          uint64_t packetTime = getPacketTime(headerPack.getTime(), tmpTrackId, pListIt->first, entryIt->mUTC);
          size_t idx = M.trackIDToIndex(packetId, getpid());
          if (idx == INVALID_TRACK_ID || !M.getCodec(idx).size()){
            segDowner.initializeMetadata(meta, tmpTrackId, packetId);
            idx = M.trackIDToIndex(packetId, getpid());
          }
          if (!streamIsLive){
            headerPack.getString("data", data, dataLen);

            // keyframe data exists, so always add 19 bytes keyframedata.
            uint32_t packOffset = headerPack.hasMember("offset") ? headerPack.getInt("offset") : 0;
            INSANE_MSG("Adding packet (%zuB) at %" PRIu64 " with an offset of %" PRIu32 " on track %zu", dataLen, packetTime, packOffset, idx);
            meta.update(packetTime, packOffset, idx, dataLen, entryIt->bytePos, headerPack.hasMember("keyframe"));
          }
          foundAtLeastOnePacket = true;
        }
        // Finally save the offset as part of the TS segment. This is required for bufferframe
        // to work correctly, since not every segment might have an UTC timestamp tag
        if (plsTimeOffset.count(pListIt->first)){
          listEntries[pListIt->first].at(entId-1).timeOffset = plsTimeOffset[pListIt->first];
        }else{
          listEntries[pListIt->first].at(entId-1).timeOffset = 0;
        }

        //Set progress counter
        if (streamStatus && streamStatus.len > 1){
          streamStatus.mapped[1] = (255 * currentSegment) / totalSegments;
        }

        // If live, don't actually parse anything. If non-live, we read all the segments
        parsedSegments[pListIt->first] = playlistMapping[pListIt->first]->firstIndex + (streamIsLive ? 0 : currentSegment);

        // For still-appending streams, only parse the first segment for each playlist
        if (streamIsLive && foundAtLeastOnePacket){break;}
      }
    }
    if (!config->is_active){return false;}

    // set bootMsOffset in order to display the program time correctly in the player
    meta.setUTCOffset(zUTC);
    if (M.getLive()){meta.setBootMsOffset(streamOffset);}

    injectLocalVars();
    isInitialRun = true;
    return true;
  }

  /// Sets inputLocalVars based on data ingested
  void InputHLS::injectLocalVars(){
    meta.inputLocalVars.null();
    meta.inputLocalVars["version"] = 4;

    // Write playlist entry info
    JSON::Value allEntries;
    for (std::map<uint32_t, std::deque<playListEntries> >::iterator pListIt = listEntries.begin();
         pListIt != listEntries.end(); pListIt++){
      JSON::Value thisPlaylist;
      for (std::deque<playListEntries>::iterator entryIt = pListIt->second.begin();
         entryIt != pListIt->second.end(); entryIt++){
        JSON::Value thisEntries;
        thisEntries.append(entryIt->relative_filename);
        thisEntries.append(entryIt->bytePos);
        thisEntries.append(entryIt->mUTC);
        thisEntries.append(entryIt->duration);
        thisEntries.append(entryIt->timestamp);
        thisEntries.append(entryIt->timeOffset);
        thisEntries.append(entryIt->wait);
        thisEntries.append(entryIt->ivec);
        thisEntries.append(entryIt->keyAES);
        if (entryIt->startAtByte || entryIt->stopAtByte || entryIt->mapName.size()){
          thisEntries.append(entryIt->startAtByte);
          thisEntries.append(entryIt->stopAtByte);
          if (entryIt->mapName.size()){
            thisEntries.append(entryIt->mapName);
          }
        }
        thisPlaylist.append(thisEntries);
      }
      allEntries[JSON::Value(pListIt->first).asString()] = thisPlaylist;
      meta.inputLocalVars["parsedSegments"][JSON::Value(pListIt->first).asString()] = parsedSegments[pListIt->first];
    }
    meta.inputLocalVars["playlist_urls"] = playlist_urls;
    meta.inputLocalVars["playlistEntries"] = allEntries;
    meta.inputLocalVars["zUTC"] = zUTC;

    // Write packet ID mappings
    JSON::Value thisMappingsR;
    for (std::map<uint64_t, uint64_t>::iterator pidIt = pidMappingR.begin();
         pidIt != pidMappingR.end(); pidIt++){
      thisMappingsR[JSON::Value(pidIt->first).asString()] = pidIt->second;
    }
    meta.inputLocalVars["pidMappingR"] = thisMappingsR;
  }

  /// \brief Parses new segments added to playlist files as live data
  /// \param segmentIndex: the index of the segment in the current playlist
  /// \return True if the segment has been buffered successfully
  bool InputHLS::parseSegmentAsLive(uint64_t segmentIndex){
    allowRemap = true; //< New segment, so allow timestamp remap
    uint64_t bufferTime = config->getInteger("pagetimeout");
    if (config->hasOption("bufferTime")){
      bufferTime = config->getInteger("bufferTime") / 1000;
    }
    // Used to immediately mark pages for removal when we're bursting through segments on initial boot
    uint64_t curTimeout = Util::bootSecs() - bufferTime;
    // Keep our own variables to make sure buffering live data does not interfere with VoD pages loading
    TS::Packet packet;
    TS::Stream tsStream;
    char *data;
    size_t dataLen;
    // Get the updated list of entries. Safe access to listEntries is handled at a higher level
    std::deque<playListEntries> &curList = listEntries[currentPlaylist];
    if (curList.size() <= segmentIndex){
      FAIL_MSG("Tried to load segment with index '%" PRIu64 "', but the playlist only contains '%zu' entries!", segmentIndex, curList.size());
      return false;
    }

    playListEntries & ntry = curList.at(segmentIndex);
    if (ntry.mapName.size()){
      segDowner.setInit(playlistMapping[currentPlaylist]->maps[ntry.mapName]);
    }
    if (!loadSegment(segDowner, ntry)){
      FAIL_MSG("Failed to load segment");
      return false;
    }

    DTSC::Packet headerPack;
    while (config->is_active && readNext(segDowner, headerPack, curList.at(segmentIndex).bytePos)){
      if (!headerPack){
        continue;
      }
      size_t tmpTrackId = headerPack.getTrackId();
      uint64_t packetId = getPacketID(currentPlaylist, tmpTrackId);
      uint64_t packetTime = headerPack.getTime();

      size_t idx = M.trackIDToIndex(packetId, getpid());
      if (idx == INVALID_TRACK_ID || !M.getCodec(idx).size()){
        tsStream.initializeMetadata(meta, tmpTrackId, packetId);
        idx = M.trackIDToIndex(packetId, getpid());
      }
      if (ntry.timeOffset){
        packetTime += ntry.timeOffset;
      }else{
        packetTime = getPacketTime(packetTime, idx, currentPlaylist, ntry.mUTC);
      }
      // Mark which tracks need to be checked for removing expired metadata
      playlistMapping[currentPlaylist]->tracks[idx] = true;

      headerPack.getString("data", data, dataLen);
      // keyframe data exists, so always add 19 bytes keyframedata.
      uint32_t packOffset = headerPack.hasMember("offset") ? headerPack.getInt("offset") : 0;
      VERYHIGH_MSG("Adding packet (%zuB) at timestamp %" PRIu64 " -> %" PRIu64 " with an offset of %" PRIu32 " on track %zu", dataLen, headerPack.getTime(), packetTime, packOffset, idx);
      bufferLivePacket(packetTime, packOffset, idx, data, dataLen, curList.at(segmentIndex).bytePos, headerPack.hasMember("keyframe"));
      if (isInitialRun){
        pageCounter[idx][getCurrentLivePage(idx)] = curTimeout;
      }else{
        pageCounter[idx][getCurrentLivePage(idx)] = Util::bootSecs();
      }
      tsStream.getEarliestPacket(headerPack);
    }
    return true;
  }

  // Removes any metadata which is no longer and the playlist or buffered in memory
  void InputHLS::updateMeta(){
    // EVENT and VOD type playlists should never segments disappear from the start
    // Only LIVE (sliding-window) type playlists should execute updateMeta()
    if (streamIsVOD || !streamIsLive){
      return;
    }

    for (std::map<size_t, bool>::iterator trackIdx = playlistMapping[currentPlaylist]->tracks.begin();
      trackIdx != playlistMapping[currentPlaylist]->tracks.end(); trackIdx++){
      // Calc after how many MS segments are no longer part of the buffer window
      uint64_t bufferTime = config->getInteger("pagetimeout");
      if (config->hasOption("bufferTime")){
        bufferTime = config->getInteger("bufferTime") / 1000;
      }
      // Remove keys which are not requestable anymore
      while (true) {
        DTSC::Keys keys = M.getKeys(trackIdx->first);
        // Stop if the earliest key is still in the playlist. Safe access to listEntries is handled at a higher level
        if (listEntries[currentPlaylist].front().bytePos <= keys.getBpos(keys.getFirstValid())){
          break;
        }
        // Stop if earliest key is still in the buffer window
        if (listEntries[currentPlaylist].back().timestamp - listEntries[currentPlaylist].front().timestamp < bufferTime){
          break;
        }
        if (keys.getValidCount() <= 3){break;}
        // First key could still be in memory, but is no longer seekable: drop it
        HIGH_MSG("Removing key %zu @%" PRIu64 " ms on track %zu from metadata", M.getKeys(trackIdx->first).getFirstValid(), M.getFirstms(trackIdx->first), trackIdx->first);
        meta.removeFirstKey(trackIdx->first);
      }
    }
  }

  void InputHLS::parseLivePoint(){
    uint64_t maxTime = Util::bootMS() + 500;
    // Block playlist runners from updating listEntries while the main thread is accessing it
    std::lock_guard<std::mutex> guard(entryMutex);
    // Iterate over all playlists, parse new segments as they've appeared in listEntries and remove expired entries in listEntries
    for (std::map<uint64_t, Playlist*>::iterator pListIt = playlistMapping.begin();
         pListIt != playlistMapping.end(); pListIt++){
      currentPlaylist = pListIt->first;

      if (!listEntries[currentPlaylist].size()){
        continue;
      }

      // Remove segments from listEntries if they're no longer requestable
      while (listEntries[currentPlaylist].front().bytePos < playlistMapping[currentPlaylist]->firstIndex + 1){
        INFO_MSG("Playlist #%" PRIu64 ": Segment #%" PRIu64 " no longer in the input playlist", currentPlaylist, listEntries[currentPlaylist].front().bytePos);
        listEntries[currentPlaylist].pop_front();
      }

      uint64_t firstSegment = listEntries[currentPlaylist].front().bytePos;
      uint64_t lastParsedSegment = parsedSegments[currentPlaylist];
      uint64_t lastSegment = listEntries[currentPlaylist].back().bytePos;

      // Skip ahead if we've missed segments which are no longer in the playlist
      if (lastParsedSegment < firstSegment - 1){
        WARN_MSG("Playlist #%" PRIu64 ": Skipping from segment #%" PRIu64 " to segment #%" PRIu64 " since we've fallen behind", currentPlaylist, lastParsedSegment, firstSegment);
        parsedSegments[currentPlaylist] = firstSegment - 1;
        lastParsedSegment = parsedSegments[currentPlaylist];
      }

      // If the segment counter decreases, restart to reinit counters and metadata
      if (lastParsedSegment > lastSegment){
        WARN_MSG("Playlist #%" PRIu64 ": Segment counter has decreased from %" PRIu64 " to %" PRIu64 ". Exiting to reset stream", currentPlaylist, lastParsedSegment, firstSegment);
        config->is_active = false;
        Util::logExitReason(ER_FORMAT_SPECIFIC, "Segment counter decreased. Exiting to reset stream");
        return;
      }

      // Unload memory pages which are outside of the buffer window and not recently loaded
      removeUnused();

      // Remove meta info for expired keys
      updateMeta();

      // Parse new segments in listEntries
      if (lastParsedSegment < lastSegment){
        INFO_MSG("Playlist #%" PRIu64 ": Parsed %" PRIu64 "/%" PRIu64 " entries. Parsing new segments...", currentPlaylist, lastParsedSegment, lastSegment);
      }else if (isInitialRun){
        isInitialRun = false;
      }
      for(uint64_t entryIt = 1 + lastParsedSegment - firstSegment; entryIt < listEntries[currentPlaylist].size(); entryIt++){
        INFO_MSG("Playlist #%" PRIu64 ": Parsing segment #%" PRIu64 " as live data", currentPlaylist, firstSegment + entryIt);
        if (parseSegmentAsLive(entryIt)){parsedSegments[currentPlaylist] = firstSegment + entryIt;}
        // Rotate between playlists if there are lots of entries to parse
        if (Util::bootMS() > maxTime){break;}
      }
    }
  }

  /// \brief Override userLeadOut to buffer new data as live packets
  void InputHLS::userLeadOut(){
    Input::userLeadOut();
    if (streamIsLive){
      parseLivePoint();
    }
  }

  bool InputHLS::openStreamSource(){return true;}

  void InputHLS::getNext(size_t idx){
    uint32_t tid = 0;
    thisPacket.null();
    uint64_t segIdx = listEntries[currentPlaylist].at(currentIndex).bytePos;
    while (config->is_active && (needsLock() || keepAlive())){
      // Check if we have a packet
      if (readNext(segDowner, thisPacket, segIdx)){
        if (!thisPacket){continue;}
        tid = getOriginalTrackId(currentPlaylist, thisPacket.getTrackId());
        uint64_t packetTime = thisPacket.getTime();
        if (listEntries[currentPlaylist].at(currentIndex).timeOffset){
          packetTime += listEntries[currentPlaylist].at(currentIndex).timeOffset;
        }else{
          packetTime = getPacketTime(thisPacket.getTime(), tid, currentPlaylist, listEntries[currentPlaylist].at(currentIndex).mUTC);
        }
        // Is it one we want?
        if (idx == INVALID_TRACK_ID || getMappedTrackId(M.getID(idx)) == thisPacket.getTrackId()){
          INSANE_MSG("Packet %" PRIu32 "@%" PRIu64 "ms -> %" PRIu64 "ms", tid, thisPacket.getTime(), packetTime);
          // overwrite trackId on success
          Bit::htobl(thisPacket.getData() + 8, tid);
          Bit::htobll(thisPacket.getData() + 12, packetTime);
          thisTime = packetTime;
          return; // Success!
        }
        continue;
      }

      // No? Then we want to try reading the next file.
      // Now that we know our playlist is up-to-date, actually try to read the file.
      VERYHIGH_MSG("Moving on to next TS segment (variant %" PRIu64 ")", currentPlaylist);
      if (readNextFile()){
        allowRemap = true;
        MEDIUM_MSG("Next segment read successfully");
        segIdx = listEntries[currentPlaylist].at(currentIndex).bytePos;
        continue; // Success! Continue regular parsing.
      }

      // Reached the end of the playlist
      thisPacket.null();
      return;
    }
  }

  void InputHLS::seek(uint64_t seekTime, size_t idx){
    if (idx == INVALID_TRACK_ID){return;}
    plsTimeOffset.clear();
    plsLastTime.clear();
    plsInterval.clear();
    segDowner.reset();
    currentPlaylist = getMappedTrackPlaylist(M.getID(idx));

    unsigned long plistEntry = 0;
    DTSC::Keys keys = M.getKeys(idx);
    for (size_t i = keys.getFirstValid(); i < keys.getEndValid(); i++){
      if (keys.getTime(i) > seekTime){
        VERYHIGH_MSG("Found elapsed key with a time of %" PRIu64 " ms. Using playlist index %zu to match requested time %" PRIu64 "", keys.getTime(i), plistEntry, seekTime);
        break;
      }
      // Keys can still be accessible in memory. Skip any segments we cannot seek to in the playlist
      if (keys.getBpos(i) <= playlistMapping[currentPlaylist]->firstIndex){
        INSANE_MSG("Skipping segment #%lu (key %lu @ %" PRIu64 " ms) for seeking, as it is no longer available in the playlist", keys.getBpos(i) - 1, i, keys.getTime(i));
        continue;
      }
      plistEntry = keys.getBpos(i) - 1 - playlistMapping[currentPlaylist]->firstIndex;
      INSANE_MSG("Found valid key with a time of %" PRIu64 " ms at playlist index %zu while seeking", keys.getTime(i), plistEntry);
    }
    currentIndex = plistEntry;

    VERYHIGH_MSG("Seeking to index %zu on playlist %" PRIu64, currentIndex, currentPlaylist);

    {// Lock mutex for listEntries
      std::lock_guard<std::mutex> guard(entryMutex);
      if (!listEntries.count(currentPlaylist)){
        WARN_MSG("Playlist %" PRIu64 " not loaded, aborting seek", currentPlaylist);
        return;
      }
      std::deque<playListEntries> &curPlaylist = listEntries[currentPlaylist];
      if (curPlaylist.size() <= currentIndex){
        WARN_MSG("Playlist %" PRIu64 " has %zu <= %zu entries, aborting seek", currentPlaylist,
                 curPlaylist.size(), currentIndex);
        return;
      }
      playListEntries & e = curPlaylist.at(currentIndex);
      if (e.mapName.size()){
        segDowner.setInit(playlistMapping[currentPlaylist]->maps[e.mapName]);
      }
      loadSegment(segDowner, e);
      if (e.timeOffset){
        // If we have an offset, load it
        allowRemap = false;
        HIGH_MSG("Setting time offset of this TS segment to %" PRId64, e.timeOffset);
        plsTimeOffset[currentPlaylist] = e.timeOffset;
      }else{
        allowRemap = true;
      }
    }
  }

  bool InputHLS::keepRunning(bool updateActCtr){
    if (isAlwaysOn()){
      uint64_t currLastUpdate = M.getLastUpdated();
      if (currLastUpdate > activityCounter){activityCounter = currLastUpdate;}
      return Input::keepRunning(false);
    }else{
      return Input::keepRunning(true);
    }
  }

  /// \brief Applies any offset to the packets original timestamp
  /// \param packetTime: the original timestamp of the packet
  /// \param tid: the trackid corresponding to this track and playlist
  /// \param currentPlaylist: the ID of the playlist we are currently trying to parse
  /// \param nUTC: Defaults to 0. If larger than 0, sync the timestamp based on this value and zUTC
  /// \return the (modified) packetTime, used for meta.updates and buffering packets
  uint64_t InputHLS::getPacketTime(uint64_t packetTime, uint64_t tid, uint64_t currentPlaylist, uint64_t nUTC){
    INSANE_MSG("Calculating adjusted packet time for track %" PRIu64 " on playlist %" PRIu64 " with current timestamp %" PRIu64 ". UTC timestamp is %" PRIu64, tid, currentPlaylist, packetTime, nUTC);
    uint64_t newTime = packetTime;

    // UTC based timestamp offsets
    if (zUTC){
      // Overwrite offset if we have an UTC timestamp
      if (allowRemap && nUTC){
        allowRemap = false;
        int64_t prevOffset = plsTimeOffset[currentPlaylist];
        plsTimeOffset[currentPlaylist] = (nUTC - zUTC) - packetTime;
        newTime = packetTime + plsTimeOffset[currentPlaylist];
        MEDIUM_MSG("[UTC; New offset: %" PRId64 " -> %" PRId64 "] Packet %" PRIu64 "@%" PRIu64
                  "ms -> %" PRIu64 "ms", prevOffset, plsTimeOffset[currentPlaylist], tid, packetTime, newTime);
      }else if (plsTimeOffset.count(currentPlaylist)){
        // Prevent integer overflow for large negative offsets, which can happen
        // when the first time of another track is lower that the firsttime
        if (plsTimeOffset[currentPlaylist] + int64_t(newTime) < 0){
          newTime = 0;
          FAIL_MSG("Time offset is too negative causing an integer overflow. Setting current packet time to 0.");
        }else{
          VERYHIGH_MSG("Adjusting timestamp %" PRIu64 " -> %" PRIu64 " (offset is %" PRId64 ")", newTime, newTime + plsTimeOffset[currentPlaylist], plsTimeOffset[currentPlaylist]);
          newTime += plsTimeOffset[currentPlaylist];
        }
      }
    // Non-UTC based
    }else{
      // Apply offset if any was set
      if (plsTimeOffset.count(currentPlaylist)){
        VERYHIGH_MSG("Adjusting timestamp %" PRIu64 " -> %" PRIu64 " (offset is %" PRId64 ")", newTime, newTime + plsTimeOffset[currentPlaylist], plsTimeOffset[currentPlaylist]);
        newTime += plsTimeOffset[currentPlaylist];
      }
      if (plsLastTime.count(currentPlaylist)){
        if (plsInterval.count(currentPlaylist)){
          if (allowRemap && (newTime < plsLastTime[currentPlaylist] ||
                              newTime > plsLastTime[currentPlaylist] + plsInterval[currentPlaylist] * 60)){
            allowRemap = false;
            // time difference too great, change offset to correct for it
            int64_t prevOffset = plsTimeOffset[currentPlaylist];
            plsTimeOffset[currentPlaylist] +=
                (int64_t)(plsLastTime[currentPlaylist] + plsInterval[currentPlaylist]) - (int64_t)newTime;
            newTime = packetTime + plsTimeOffset[currentPlaylist];
            MEDIUM_MSG("[Guess; New offset: %" PRId64 " -> %" PRId64 "] Packet %" PRIu64 "@%" PRIu64
                      "ms -> %" PRIu64 "ms",
                      prevOffset, plsTimeOffset[currentPlaylist], tid, packetTime, newTime);
          }
        }
        // check if time increased, and no increase yet or is less than current, set new interval
        if (newTime > plsLastTime[currentPlaylist] &&
            (!plsInterval.count(currentPlaylist) ||
              newTime - plsLastTime[currentPlaylist] < plsInterval[currentPlaylist])){
          plsInterval[currentPlaylist] = newTime - plsLastTime[currentPlaylist];
        }
      }
      // store last time for interval/offset calculations
      plsLastTime[tid] = newTime;
    }
    return newTime;
  }

  /// \brief Returns the packet ID corresponding to this playlist and track
  /// \param trackId: the trackid corresponding to this track and playlist
  /// \param currentPlaylist: the ID of the playlist we are currently trying to parse
  uint64_t InputHLS::getPacketID(uint64_t currentPlaylist, uint64_t trackId){
    uint64_t packetId = pidMapping[(((uint64_t)currentPlaylist) << 32) + trackId];
    if (packetId == 0){
      pidMapping[(((uint64_t)currentPlaylist) << 32) + trackId] = pidCounter;
      pidMappingR[pidCounter] = (((uint64_t)currentPlaylist) << 32) + trackId;
      packetId = pidCounter;
      pidCounter++;
    }
    return packetId;
  }

  uint64_t InputHLS::getOriginalTrackId(uint32_t playlistId, uint32_t id){
    return pidMapping[(((uint64_t)playlistId) << 32) + id];
  }

  uint32_t InputHLS::getMappedTrackId(uint64_t id){
    static uint64_t lastIn = id;
    static uint32_t lastOut = (pidMappingR[id] & 0xFFFFFFFFull);
    if (lastIn != id){
      lastIn = id;
      lastOut = (pidMappingR[id] & 0xFFFFFFFFull);
    }
    return lastOut;
  }

  uint32_t InputHLS::getMappedTrackPlaylist(uint64_t id){
    if (!pidMappingR.count(id)){
      FAIL_MSG("No mapping found for track ID %" PRIu64, id);
      return 0;
    }
    static uint64_t lastIn = id;
    static uint32_t lastOut = (pidMappingR[id] >> 32);
    if (lastIn != id){
      lastIn = id;
      lastOut = (pidMappingR[id] >> 32);
    }
    return lastOut;
  }

  /// Parses the main playlist, possibly containing variants.
  bool InputHLS::initPlaylist(const std::string &uri, bool fullInit){
    // Used to set zUTC, in case the first EXT-X-PROGRAM-DATE-TIME does not appear before the first segment
    float timestampSum = 0;
    bool isRegularPls = false;
    plsInitCount = 0;
    plsTotalCount = 0;
    {
      std::lock_guard<std::mutex> guard(entryMutex);
      listEntries.clear();
    }
    std::string line;
    bool ret = false;
    startTime = Util::bootSecs();
    std::string playlistLocation = uri;

    HTTP::URL playlistRootPath(playlistLocation);
    // Convert custom http(s)-hls protocols into regular notation.
    if (playlistRootPath.protocol == "http-hls"){playlistRootPath.protocol = "http";}
    if (playlistRootPath.protocol == "https-hls"){playlistRootPath.protocol = "https";}
    if (playlistRootPath.protocol == "s3+http-hls"){playlistRootPath.protocol = "s3+http";}
    if (playlistRootPath.protocol == "s3+https-hls"){playlistRootPath.protocol = "s3+https";}

    std::istringstream urlSource;
    std::ifstream fileSource;

    bool isUrl = (playlistLocation.find("://") != std::string::npos);
    if (isUrl){
      INFO_MSG("Downloading main playlist file from '%s'", uri.c_str());
      HTTP::URIReader plsDL;
      if (!plsDL.open(playlistRootPath) || !plsDL){
        Util::logExitReason(ER_READ_START_FAILURE, "Could not open main playlist, aborting");
        return false;
      }
      char * dataPtr;
      size_t dataLen;
      plsDL.readAll(dataPtr, dataLen);
      if (!dataLen){
        Util::logExitReason(ER_READ_START_FAILURE, "Could not download main playlist, aborting.");
        return false;
      }
      urlSource.str(std::string(dataPtr, dataLen));
    }else{
      // If we're not a URL and there is no / at the start, ensure we get the full absolute path.
      if (playlistLocation[0] != '/'){
        char *rp = realpath(playlistLocation.c_str(), 0);
        if (rp){
          playlistRootPath = HTTP::URL((std::string)rp);
          free(rp);
        }
      }
      fileSource.open(playlistLocation.c_str());
      if (!fileSource.good()){
        Util::logExitReason(ER_READ_START_FAILURE, "Could not open playlist (%s): %s", strerror(errno), playlistLocation.c_str());
      }
    }

    std::istream &input = (isUrl ? (std::istream &)urlSource : (std::istream &)fileSource);
    std::getline(input, line);

    while (std::getline(input, line)){
      cleanLine(line);
      if (line.empty()){
        // skip empty lines in the playlist
        continue;
      }
      if (line.compare(0, 14, "#EXT-X-ENDLIST") == 0){
        streamIsLive = false;
        streamIsVOD = true;
        meta.setLive(false);
        meta.setVod(true);
      }
      if (line.compare(0, 17, "#EXT-X-STREAM-INF") == 0){
        // this is a variant playlist file.. next line is an uri to a playlist
        // file
        size_t pos;
        bool codecSupported = false;

        pos = line.find("CODECS=\"");
        if (pos != std::string::npos){
          std::string codecs = line.substr(pos + 8);
          transform(codecs.begin(), codecs.end(), codecs.begin(), ::tolower);

          pos = codecs.find("\"");

          if (pos != std::string::npos){
            codecs = codecs.substr(0, pos);
            codecs.append(",");

            std::string codec;
            while ((pos = codecs.find(",")) != std::string::npos){
              codec = codecs.substr(0, pos);
              codecs = codecs.substr(pos + 1);
              if ((codec.compare(0, 4, "mp4a") == 0) || (codec.compare(0, 4, "avc1") == 0) ||
                  (codec.compare(0, 4, "h264") == 0) || (codec.compare(0, 4, "mp3") == 0) ||
                  (codec.compare(0, 4, "aac") == 0) || (codec.compare(0, 4, "ac3") == 0)){
                codecSupported = true;
              }else{
                FAIL_MSG("codec: %s not supported!", codec.c_str());
              }
            }
          }else{
            codecSupported = true;
          }
        }else{
          codecSupported = true;
        }

        while (std::getline(input, line)){
          cleanLine(line);
          if (!line.empty()){break;}
        }

        if (codecSupported){

          ret |= readPlaylist(playlistRootPath.link(line), line, fullInit);
        }else{
          INFO_MSG("skipping variant playlist %s, none of the codecs are supported",
                   playlistRootPath.link(line).getUrl().c_str());
        }

      }else if (line.compare(0, 12, "#EXT-X-MEDIA") == 0){
        // this is also a variant playlist, but streams need to be processed
        // another way

        std::string mediafile;
        if (line.compare(18, 5, "AUDIO") == 0){
          // find URI attribute
          int pos = line.find("URI");
          if (pos != std::string::npos){
            mediafile = line.substr(pos + 5, line.length() - pos - 6);
            ret |= readPlaylist(playlistRootPath.link(mediafile), mediafile, fullInit);
          }
        }

      }else if (line.compare(0, 7, "#EXTINF") == 0){
        // Read as regular playlist after we are done checking for UTC timestamps
        isRegularPls = true;
        // Sum the duration to make sure we set zUTC time right
        float f = atof(line.c_str() + 8);
        timestampSum += f * 1000;
      }else if (line.compare(0, 24, "#EXT-X-PROGRAM-DATE-TIME") == 0 && !zUTC){
        // Init UTC variables used to rewrite packet timestamps
        size_t pos = line.find(":");
        std::string val = line.c_str() + pos + 1;
        zUTC = ISO8601toUnixmillis(val) - uint64_t(timestampSum);
        INFO_MSG("Setting program unix start time to '%s' (%" PRIu64 ")", line.substr(pos + 1).c_str(), zUTC);
        // store offset so that we can set it after reading the header
        streamOffset = zUTC - (Util::unixMS() - Util::bootMS());
        meta.setUTCOffset(zUTC);
        if (M.getLive()){meta.setBootMsOffset(streamOffset);}
      }else{
        // ignore wrong lines
        VERYHIGH_MSG("ignore wrong line: %s", line.c_str());
      }
    }

    if (isRegularPls){
      ret |= readPlaylist(playlistRootPath.getUrl(), "", fullInit);
    }

    if (!isUrl){fileSource.close();}

    uint32_t maxWait = 0;
    unsigned int lastCount = 9999;
    while (plsTotalCount != plsInitCount && ++maxWait < 1000){
      if (plsInitCount != lastCount){
        lastCount = plsInitCount;
        INFO_MSG("Waiting for variant playlists to load... %u/%u", lastCount, plsTotalCount);
      }
      Util::sleep(50);
    }
    if (maxWait >= 1000){
      WARN_MSG("Timeout waiting for variant playlists (%u/%u)", plsInitCount, plsTotalCount);
    }
    plsInitCount = 0;
    plsTotalCount = 0;

    return ret;
  }

  /// Function for reading every playlist.
  bool InputHLS::readPlaylist(const HTTP::URL &uri, const  std::string & relurl, bool fullInit){
    std::string urlBuffer;
    // Wildcard streams can have a ' ' in the name, which getUrl converts to a '+'
    if (uri.isLocalPath()){
      urlBuffer = uri.getFilePath() + "\n" + relurl;
    }else{
      urlBuffer = uri.getUrl() + "\n" + relurl;
    }
    VERYHIGH_MSG("Adding playlist(s): %s", urlBuffer.c_str());
    bool inited = false;
    std::thread runList([urlBuffer, fullInit, &inited](){
      if (!urlBuffer.size()){return;}// abort if we received a null pointer - something is seriously wrong
      Util::setStreamName(self->getStreamName());

      Playlist *pls = new Playlist(urlBuffer);
      plsTotalCount++;
      pls->id = plsTotalCount;
      playlistMapping[pls->id] = pls;

      if (!pls->uri.size()){
        FAIL_MSG("Variant playlist URL is empty, aborting update thread.");
        return;
      }

      pls->reload();
      inited = true;
      plsInitCount++;
      if (!fullInit){
        INFO_MSG("Thread for %s exiting", pls->uri.c_str());
        return;
      }// Exit because init-only mode

      while (self->config->is_active && streamIsLive){
        if (pls->reloadNext > Util::bootSecs()){
          Util::sleep(1000);
        }else{
          pls->reload();
        }
      }
      INFO_MSG("Downloader thread for '%s' exiting", pls->uri.c_str());
    });
    runList.detach(); // Abandon the thread, it's now running independently
    uint32_t timeout = 0;
    while (!inited && ++timeout < 100){Util::sleep(100);}
    if (timeout >= 100){WARN_MSG("Thread start timed out for: %s", urlBuffer.c_str());}
    return true;
  }

  /// Read next .ts file from the playlist. (from the list of entries which needs
  /// to be processed)
  bool InputHLS::readNextFile(){
    segDowner.reset();

    // This scope limiter prevents the recursion down below from deadlocking us
    {
      // Switch to next file
      currentIndex++;
      std::lock_guard<std::mutex> guard(entryMutex);
      std::deque<playListEntries> &curList = listEntries[currentPlaylist];
      HIGH_MSG("Current playlist contains %zu entries. Current index is %zu in playlist %" PRIu64, curList.size(), currentIndex, currentPlaylist);
      if (curList.size() <= currentIndex){
        return false;
      }
      playListEntries & ntry = curList.at(currentIndex);

      if (ntry.mapName.size()){
        segDowner.setInit(playlistMapping[currentPlaylist]->maps[ntry.mapName]);
      }
      if (!loadSegment(segDowner, ntry)){
        ERROR_MSG("Could not download segment: %s", ntry.filename.c_str());
        return false;
      }
      // If we have an offset, load it
      if (ntry.timeOffset){
        allowRemap = false;
        plsTimeOffset[currentPlaylist] = ntry.timeOffset;
      // Else allow of the offset to be set by getPacketTime
      }else{
        allowRemap = true;
      }
      return true;
    }
    return false;
  }

  /// return the playlist id from which we need to read the first upcoming segment
  /// by timestamp.
  /// this will keep the playlists in sync while reading segments.
  size_t InputHLS::firstSegment(){
    // Only one selected? Immediately return the right playlist.
    if (!streamIsLive){return getMappedTrackPlaylist(M.getID(userSelect.begin()->first));}
    uint64_t firstTimeStamp = 0;
    int tmpId = -1;
    int segCount = 0;

    std::lock_guard<std::mutex> guard(entryMutex);
    for (std::map<uint32_t, std::deque<playListEntries> >::iterator pListIt = listEntries.begin();
         pListIt != listEntries.end(); pListIt++){
      segCount += pListIt->second.size();
      if (pListIt->second.size()){
        INSANE_MSG("Playlist %u contains %zu segments, with the earliest segment starting @%" PRIu64 " ms", pListIt->first, pListIt->second.size(), firstTimeStamp);
        if (pListIt->second.front().timestamp < firstTimeStamp || tmpId < 0){
          firstTimeStamp = pListIt->second.front().timestamp;
          tmpId = pListIt->first;
        }
      }
    }
    MEDIUM_MSG("Active playlist: %d (%d segments total in queue)", tmpId, segCount);
    return tmpId;
  }

  void InputHLS::finish(){
    if (streamIsLive){ //< Already generated from readHeader
      INFO_MSG("Writing updated header to disk");
      injectLocalVars();
      M.toFile(HTTP::localURIResolver().link(config->getString("input") + ".dtsh").getUrl());
    }
    Input::finish();
  }

  void InputHLS::checkHeaderTimes(const HTTP::URL & streamFile){
    if (streamIsLive){return;} //< Since the playlist will likely be newer than the DTSH for live-dvr
    Input::checkHeaderTimes(streamFile);
  }


}// namespace Mist
