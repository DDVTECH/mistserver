#include "input_hls.h"
#include "mbedtls/aes.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mist/bitfields.h>
#include <mist/defines.h>
#include <mist/flv_tag.h>
#include <mist/http_parser.h>
#include <mist/mp4_generic.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/tinythread.h>
#include <mist/ts_packet.h>
#include <string>
#include <sys/stat.h>
#include <vector>

#define SEM_TS_CLAIM "/MstTSIN%s"

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
  const std::string time = ts.substr(T + 1, Z - T - 1);
  const std::string zone = ts.substr(Z);
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
  static std::map<uint64_t, Playlist> playlistMapping;

  /// Local RAM buffer for recently accessed segments
  std::map<std::string, Util::ResizeablePointer> segBufs;

  /// Order of adding/accessing for local RAM buffer of segments
  std::deque<std::string> segBufAccs;

  /// Order of adding/accessing sizes for local RAM buffer of segments
  std::deque<size_t> segBufSize;

  size_t segBufTotalSize = 0;

  /// Track which segment numbers have been parsed
  std::map<uint64_t, uint64_t> parsedSegments;

  /// Mutex for accesses to listEntries
  tthread::mutex entryMutex;

  JSON::Value playlist_urls; ///< Relative URLs to the various playlists

  static unsigned int plsTotalCount = 0; /// Total playlists active
  static unsigned int plsInitCount = 0;  /// Count of playlists fully inited

  bool streamIsLive;
  uint32_t globalWaitTime;
  std::map<uint32_t, std::deque<playListEntries> > listEntries;

  // These are used in the HTTP::Downloader callback, to prevent timeouts when downloading
  // segments/playlists.
  inputHLS *self = 0;
  bool callbackFunc(uint8_t){return self->callback();}

  /// Called by the global callbackFunc, to prevent timeouts
  bool inputHLS::callback(){
    keepAlive();
    return config->is_active;
  }

  /// Helper function that removes trailing \r characters
  void cleanLine(std::string &s){
    if (s.length() > 0 && s.at(s.length() - 1) == '\r'){s.erase(s.size() - 1);}
  }

  /// Helper function that is used to run the playlist downloaders
  /// Expects character array with playlist URL as argument, sets the first byte of the pointer to zero when loaded.
  void playlistRunner(void *ptr){
    if (!ptr){return;}// abort if we received a null pointer - something is seriously wrong
    Util::setStreamName(self->getStreamName());
    bool initOnly = false;
    if (((char *)ptr)[0] == ';'){initOnly = true;}

    Playlist pls(initOnly ? ((char *)ptr) + 1 : (char *)ptr);
    plsTotalCount++;
    // signal that we have now copied the URL and no longer need it
    ((char *)ptr)[0] = 0;

    if (!pls.uri.size()){
      FAIL_MSG("Variant playlist URL is empty, aborting update thread.");
      return;
    }

    pls.reload();
    playlistMapping[plsTotalCount] = pls;
    plsInitCount++;
    if (initOnly){
      return;
    }// Exit because init-only mode

    while (self->config->is_active){
      // If the timer has not expired yet, sleep up to a second. Otherwise, reload.
      /// \TODO Sleep longer if that makes sense?
      if (pls.reloadNext > Util::bootSecs()){
        Util::sleep(1000);
      }else{
        pls.reload();
      }
    }
    MEDIUM_MSG("Downloader thread for '%s' exiting", pls.uri.c_str());
  }

  Playlist::Playlist(const std::string &uriSource){
    nextUTC = 0;
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
    lastFileIndex = 0;
    waitTime = 2;
    playlistEnd = false;
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

  void flipKey(char *d){
    for (size_t i = 0; i < 8; i++){
      char tmp = d[i];
      d[i] = d[15 - i];
      d[15 - i] = tmp;
    }
  }

  static std::string printhex(const char *data, size_t len){
    static const char *const lut = "0123456789ABCDEF";

    std::string output;
    output.reserve(2 * len);
    for (size_t i = 0; i < len; ++i){
      const unsigned char c = data[i];
      output.push_back(lut[c >> 4]);
      output.push_back(lut[c & 15]);
    }
    return output;
  }

  SegmentDownloader::SegmentDownloader(){
    isOpen = false;
    segDL.onProgress(callbackFunc);
    encrypted = false;
    currBuf = 0;
    packetPtr = 0;
  }

  /// Returns true if packetPtr is at the end of the current segment.
  bool SegmentDownloader::atEnd() const{
    if (!isOpen || !currBuf){return true;}
    if (buffered){return currBuf->size() <= offset + 188;}
    return !segDL && currBuf->size() <= offset + 188;
    // return (packetPtr - segDL.const_data().data() + 188) > segDL.const_data().size();
  }

  /// Attempts to read a single TS packet from the current segment, setting packetPtr on success
  bool SegmentDownloader::readNext(){
    if (encrypted){
      // Encrypted, need to decrypt
#ifdef SSL
      // Are we exactly at the end of the buffer? Truncate it entirely.
      if (encOffset == outData.size() || !outData.size()){
        outData.truncate(0);
        packetPtr = 0;
        encOffset = 0;
      }
      // Do we already have some data ready? Just serve it.
      if (encOffset + 188 <= outData.size()){
        packetPtr = outData + encOffset;
        encOffset += 188;
        if (packetPtr[0] != 0x47){
          FAIL_MSG("Not TS! Starts with byte %" PRIu8, (uint8_t)packetPtr[0]);
          return false;
        }
        return true;
      }
      // Alright, we need to read some more data.
      // We read 192 bytes at a time: a single TS packet is 188 bytes but AES-128-CBC encryption works in 16-byte blocks.
      size_t len = 0;
      segDL.readSome(packetPtr, len, 192);
      if (!len){return false;}
      if (len % 16 != 0){
        FAIL_MSG("Read a non-16-multiple of bytes (%zu), cannot decode!", len);
        return false;
      }
      outData.allocate(outData.size() + len);
      mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, len, tmpIvec, (const unsigned char *)packetPtr,
                            ((unsigned char *)(char *)outData) + outData.size());
      outData.append(0, len);
      // End of the segment? Remove padding data.
      if (segDL.isEOF()){
        // The padding consists of X bytes of padding, all containing the raw value X.
        // Since padding is mandatory, we can simply read the last byte and remove X bytes from the length.
        if (outData.size() <= outData[outData.size() - 1]){
          FAIL_MSG("Encryption padding is >= one TS packet. We probably returned some invalid TS "
                   "data earlier :-(");
          return false;
        }
        outData.truncate(outData.size() - outData[outData.size() - 1]);
      }
      // Okay, we have more data. Let's see if we can return it...
      if (encOffset + 188 <= outData.size()){
        packetPtr = outData + encOffset;
        encOffset += 188;
        if (packetPtr[0] != 0x47){
          FAIL_MSG("Not TS! Starts with byte %" PRIu8, (uint8_t)packetPtr[0]);
          return false;
        }
        return true;
      }
#endif
      // No? Then we've failed in our task :'(
      FAIL_MSG("Could not load encrypted packet :'(");
      return false;
    }else{
      // Plaintext
      if (buffered){
        if (atEnd()){return false;}
      }else{
        if (!currBuf){return false;}
        size_t retries = 0;
        while (segDL && currBuf->size() < offset + 188 + 188){
          size_t preSize = currBuf->size();
          segDL.readSome(offset + 188 + 188 - currBuf->size(), *this);
          if (currBuf->size() < offset + 188 + 188){
            if (!segDL){
              if (!segDL.isSeekable()){return false;}
              // Only retry/resume if seekable and allocated size greater than current size
              if (currBuf->rsize() > currBuf->size()){
                // Seek to current position to resume
                ++retries;
                if (retries > 5){
                  segDL.close();
                  return false;
                }
                segDL.seek(currBuf->size());
              }
            }
            if (currBuf->size() <= preSize){
              Util::sleep(5);
            }
          }
        }
        if (currBuf->size() < offset + 188 + 188){return false;}
      }
      // First packet is at offset 0, not 188. Skip increment for this one.
      if (!firstPacket){
        offset += 188;
      }else{
        firstPacket = false;
      }
      packetPtr = *currBuf + offset;
      if (!packetPtr || packetPtr[0] != 0x47){
        std::stringstream packData;
        if (packetPtr){
          for (uint64_t i = 0; i < 188; ++i){
            packData << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)packetPtr[i];
          }
        }
        FAIL_MSG("Not a valid TS packet: byte %zu is not 0x47: %s", offset, packData.str().c_str());
        return false;
      }
      return true;
    }
  }

  void SegmentDownloader::dataCallback(const char *ptr, size_t size){
    currBuf->append(ptr, size);
    //Overwrite the current segment size
    segBufTotalSize -= segBufSize.front();
    segBufSize.front() = currBuf->size();
    segBufTotalSize += segBufSize.front();
  }

  size_t SegmentDownloader::getDataCallbackPos() const{return currBuf->size();}

  /// Attempts to read a single TS packet from the current segment, setting packetPtr on success
  void SegmentDownloader::close(){
    packetPtr = 0;
    isOpen = false;
    segDL.close();
  }

  /// Loads the given segment URL into the segment buffer.
  bool SegmentDownloader::loadSegment(const playListEntries &entry){
    std::string hexKey = printhex(entry.keyAES, 16);
    std::string hexIvec = printhex(entry.ivec, 16);

    MEDIUM_MSG("Loading segment: %s, key: %s, ivec: %s", entry.filename.c_str(), hexKey.c_str(),
               hexIvec.c_str());

    offset = 0;
    firstPacket = true;
    buffered = segBufs.count(entry.filename);
    if (!buffered){
      HIGH_MSG("Reading non-cache: %s", entry.filename.c_str());
      if (!segDL.open(entry.filename)){
        FAIL_MSG("Could not open %s", entry.filename.c_str());
        return false;
      }
      if (!segDL){return false;}
      //Remove cache entries while above 16MiB in total size, unless we only have 1 entry (we keep two at least at all times)
      while (segBufTotalSize > 16 * 1024 * 1024 && segBufs.size() > 1){
        HIGH_MSG("Dropping from segment cache: %s", segBufAccs.back().c_str());
        segBufs.erase(segBufAccs.back());
        segBufTotalSize -= segBufSize.back();
        segBufAccs.pop_back();
        segBufSize.pop_back();
      }
      segBufAccs.push_front(entry.filename);
      segBufSize.push_front(0);
      currBuf = &(segBufs[entry.filename]);
    }else{
      HIGH_MSG("Reading from segment cache: %s", entry.filename.c_str());
      currBuf = &(segBufs[entry.filename]);
      if (currBuf->rsize() != currBuf->size()){
        MEDIUM_MSG("Cache was incomplete (%zu/%" PRIu32 "), resuming", currBuf->size(), currBuf->rsize());
        buffered = false;
        // We only re-open and seek if the opened URL doesn't match what we want already
        HTTP::URL A = segDL.getURI();
        HTTP::URL B = HTTP::localURIResolver().link(entry.filename);
        if (A != B){
          if (!segDL.open(entry.filename)){
            FAIL_MSG("Could not open %s", entry.filename.c_str());
            return false;
          }
          if (!segDL){return false;}
          //Seek to current position in segment for resuming
          currBuf->truncate(currBuf->size() / 188 * 188);
          MEDIUM_MSG("Seeking to %zu", currBuf->size());
          segDL.seek(currBuf->size());
        }
      }
    }
    if (!buffered){
      // Allocate full size if known
      if (segDL.getSize() != std::string::npos){currBuf->allocate(segDL.getSize());}
      // Download full segment if not seekable, pretend it was cached all along
      if (!segDL.isSeekable()){
        segDL.readAll(*this);
        buffered = true;
      }
    }

    encrypted = false;
    outData.truncate(0);
    // If we have a non-null key, decrypt
    if (entry.keyAES[0] != 0 || entry.keyAES[1] != 0 || entry.keyAES[2] != 0 || entry.keyAES[3] != 0 ||
        entry.keyAES[4] != 0 || entry.keyAES[5] != 0 || entry.keyAES[6] != 0 || entry.keyAES[7] != 0 ||
        entry.keyAES[8] != 0 || entry.keyAES[9] != 0 || entry.keyAES[10] != 0 || entry.keyAES[11] != 0 ||
        entry.keyAES[12] != 0 || entry.keyAES[13] != 0 || entry.keyAES[14] != 0 || entry.keyAES[15] != 0){
      encrypted = true;
#ifdef SSL
      // Load key
      mbedtls_aes_setkey_dec(&aes, (const unsigned char *)entry.keyAES, 128);
      // Load initialization vector
      memcpy(tmpIvec, entry.ivec, 16);
#endif
    }

    packetPtr = 0;
    isOpen = true;
    HIGH_MSG("Segment download complete and passed sanity checks");
    return true;
  }

  /// Handles both initial load and future reloads.
  /// Returns how many segments were added to the internal segment list.
  bool Playlist::reload(){
    uint64_t fileNo = 0;
    nextUTC = 0; // Make sure we don't use old timestamps
    std::string line;
    std::string key;
    std::string val;

    std::string keyMethod;
    std::string keyUri;
    std::string keyIV;

    int count = 0;
    uint64_t totalBytes = 0;

    playlistType = LIVE; // Temporary value

    std::istringstream urlSource;
    std::ifstream fileSource;

    if (isUrl()){
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
    }else{
      fileSource.open(uri.c_str());
      if (!fileSource.good()){
        FAIL_MSG("Could not open playlist (%s): %s", strerror(errno), uri.c_str());
        reloadNext = Util::bootSecs() + waitTime;
        return false;
      }
    }

    std::istream &input = (isUrl() ? (std::istream &)urlSource : (std::istream &)fileSource);
    std::getline(input, line);

    DONTEVEN_MSG("Reloading playlist '%s'", uri.c_str());
    while (std::getline(input, line)){
      DONTEVEN_MSG("Parsing line '%s'", line.c_str());
      cleanLine(line);
      if (line.empty()){continue;}// skip empty lines

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
        }

        if (key == "TARGETDURATION"){
          waitTime = atoi(val.c_str()) / 2;
          if (waitTime < 2){waitTime = 2;}
        }

        if (key == "MEDIA-SEQUENCE"){
          fileNo = atoll(val.c_str());
        }
        if (key == "PROGRAM-DATE-TIME"){nextUTC = ISO8601toUnixmillis(val);}

        if (key == "PLAYLIST-TYPE"){
          if (val == "VOD"){
            playlistType = VOD;
          }else if (val == "LIVE"){
            playlistType = LIVE;
          }else if (val == "EVENT"){
            playlistType = EVENT;
          }
        }

        if (key == "ENDLIST"){
          // end of playlist reached!
          playlistEnd = true;
          playlistType = VOD;
        }
        continue;
      }
      if (line.compare(0, 7, "#EXTINF") != 0){
        VERYHIGH_MSG("ignoring line: %s.", line.c_str());
        continue;
      }

      float f = atof(line.c_str() + 8);
      std::string filename;
      std::getline(input, filename);

      // check for already added segments
      DONTEVEN_MSG("Current file has index #%" PRIu64 ", last index was #%" PRIu64 "", fileNo, lastFileIndex);
      if (fileNo >= lastFileIndex){
        cleanLine(filename);
        char ivec[16];
        if (keyIV.size()){
          parseKey(keyIV, ivec, 16);
        }else{
          memset(ivec, 0, 16);
          Bit::htobll(ivec + 8, fileNo);
        }
        addEntry(root.link(filename).getUrl(), filename, f, totalBytes, keys[keyUri], std::string(ivec, 16));
        lastFileIndex = fileNo + 1;
        ++count;
      }
      nextUTC = 0;
      ++fileNo;
    }

    // VOD over HTTP needs to be processed as LIVE.
    if (!isUrl()){
      fileSource.close();
    }
    // Set the global live/vod bool to live if this playlist looks like a live playlist
    if (playlistType == LIVE){streamIsLive = true;}

    if (globalWaitTime < waitTime){globalWaitTime = waitTime;}

    reloadNext = Util::bootSecs() + waitTime;
    return (count > 0);
  }

  bool Playlist::isSupportedFile(const std::string filename){
    // only ts files
    if (filename.find_last_of(".") != std::string::npos){
      std::string ext = filename.substr(filename.find_last_of(".") + 1);

      if (ext.compare(0, 2, "ts") == 0){
        return true;
      }else{
        DEBUG_MSG(DLVL_HIGH, "Not supported extension: %s", ext.c_str());
        return false;
      }
    }
    // No extension. We assume it's fine.
    return true;
  }

  /// Adds playlist segments to be processed
  void Playlist::addEntry(const std::string &absolute_filename, const std::string &filename, float duration, uint64_t &totalBytes,
                          const std::string &key, const std::string &iv){
    // if (!isSupportedFile(filename)){
    //  WARN_MSG("Ignoring unsupported file: %s", filename.c_str());
    //  return;
    //}

    playListEntries entry;
    entry.filename = absolute_filename;
    entry.relative_filename = filename;
    cleanLine(entry.filename);
    entry.bytePos = totalBytes;
    entry.duration = duration;
    entry.mUTC = nextUTC;

    if (key.size() && iv.size()){
      memcpy(entry.ivec, iv.data(), 16);
      memcpy(entry.keyAES, key.data(), 16);
    }else{
      memset(entry.ivec, 0, 16);
      memset(entry.keyAES, 0, 16);
    }

      if (!isUrl()){
        std::ifstream fileSource;
        std::string test = root.link(entry.filename).getFilePath();
        fileSource.open(test.c_str(), std::ios::ate | std::ios::binary);
        if (!fileSource.good()){WARN_MSG("file: %s, error: %s", test.c_str(), strerror(errno));}
        totalBytes += fileSource.tellg();
    }

    entry.timestamp = lastTimestamp + startTime;
    lastTimestamp += duration;
    {
      tthread::lock_guard<tthread::mutex> guard(entryMutex);
      // Set a playlist ID if we haven't assigned one yet.
      // Note: This method requires never removing playlists, only adding.
      // The mutex assures we have a unique count/number.
      if (!id){id = listEntries.size() + 1;}
      HIGH_MSG("Adding entry '%s' to ID %u", filename.c_str(), id);
      playlist_urls[JSON::Value(id).asString()] = relurl;
      listEntries[id].push_back(entry);
    }
  }

  /// Constructor of HLS Input
  inputHLS::inputHLS(Util::Config *cfg) : Input(cfg){
    zUTC = nUTC = 0;
    self = this;
    streamIsLive = false;
    globalWaitTime = 0;
    currentPlaylist = 0;
    streamOffset = 0;

    pidCounter = 1;

    isLiveDVR = false;
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

    inFile = NULL;
  }

  inputHLS::~inputHLS(){
    if (inFile){fclose(inFile);}
  }

  bool inputHLS::checkArguments(){
    config->is_active = true;
    if (config->getString("input") == "-"){
      return false;
    }

    if (!initPlaylist(config->getString("input"), false)){
      Util::logExitReason(ER_UNKNOWN, "Failed to load HLS playlist, aborting");
      return false;
    }

    // If the playlist is of event type, init the amount of segments in the playlist
    if (isLiveDVR){
      // Set the previousSegmentIndex by quickly going through the existing PLS files
      setParsedSegments();
      meta.setLive(true);
      meta.setVod(true);
      streamIsLive = true;
    }

    return true;
  }

  void inputHLS::parseStreamHeader(){
    if (!initPlaylist(config->getString("input"))){
      Util::logExitReason(ER_UNKNOWN, "Failed to load HLS playlist, aborting");
      return;
    }
    uint64_t oldBootMsOffset = M.getBootMsOffset();
    meta.reInit(isSingular() ? streamName : "", false);
    meta.setUTCOffset(zUTC);
    meta.setBootMsOffset(oldBootMsOffset);
    INFO_MSG("Parsing live stream to create header...");
    TS::Packet packet; // to analyse and extract data
    int pidCounter = 1;

    tthread::lock_guard<tthread::mutex> guard(entryMutex);
    for (std::map<uint32_t, std::deque<playListEntries> >::iterator pListIt = listEntries.begin();
         pListIt != listEntries.end(); pListIt++){
      // Skip empty playlists
      if (!pListIt->second.size()){continue;}
      int prepidCounter = pidCounter;
      tsStream.clear();

      for (std::deque<playListEntries>::iterator entryIt = pListIt->second.begin();
           entryIt != pListIt->second.end(); ++entryIt){
        keepAlive();
        if (!segDowner.loadSegment(*entryIt)){
          WARN_MSG("Skipping segment that could not be loaded in an attempt to recover");
          tsStream.clear();
          continue;
        }

        do{
          if (!segDowner.readNext() || !packet.FromPointer(segDowner.packetPtr)){
            WARN_MSG("Could not load TS packet from %s, aborting segment parse", entryIt->filename.c_str());
            tsStream.clear();
            break; // Abort load
          }
          tsStream.parse(packet, entryIt->bytePos);

          if (tsStream.hasPacketOnEachTrack()){
            while (tsStream.hasPacket()){
              DTSC::Packet headerPack;
              tsStream.getEarliestPacket(headerPack);
              int tmpTrackId = headerPack.getTrackId();
              uint64_t packetId = getPacketID(pListIt->first, tmpTrackId);

              size_t idx = M.trackIDToIndex(packetId, getpid());
              if ((idx == INVALID_TRACK_ID || !M.getCodec(idx).size())){
                tsStream.initializeMetadata(meta, tmpTrackId, packetId);
                idx = M.trackIDToIndex(packetId, getpid());
                if (idx != INVALID_TRACK_ID){
                  meta.setMinKeepAway(idx, globalWaitTime * 2000);
                  VERYHIGH_MSG("setting minKeepAway = %" PRIu32 " for track: %zu", globalWaitTime * 2000, idx);
                }
              }
            }
            break; // we have all tracks discovered, next playlist!
          }
        }while (!segDowner.atEnd());
        if (!segDowner.atEnd()){
          segDowner.close();
          tsStream.clear();
        }

        if (prepidCounter < pidCounter){break;}// We're done reading this playlist!
      }
    }
    tsStream.clear();
    currentPlaylist = 0;
    segDowner.close(); // make sure we have nothing left over
    INFO_MSG("header complete, beginning live ingest of %d tracks", pidCounter - 1);
  }

  bool inputHLS::readExistingHeader(){
    if (!Input::readExistingHeader()){
      INFO_MSG("Could not read existing header, regenerating");
      return false;
    }
    if (!M.inputLocalVars.isMember("version") || M.inputLocalVars["version"].asInt() < 4){
      INFO_MSG("Header needs update, regenerating");
      return false;
    }
    // Check if the DTSH file contains all expected data
    if (!M.inputLocalVars.isMember("streamoffset")){
      INFO_MSG("Header needs update as it contains no streamoffset, regenerating");
      return false;
    }
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
    tthread::lock_guard<tthread::mutex> guard(entryMutex);
    HTTP::URL root(config->getString("input"));
    jsonForEachConst(M.inputLocalVars["playlistEntries"], i){
      uint64_t plNum = JSON::Value(i.key()).asInt();
      std::deque<playListEntries> newList;
      jsonForEachConst(*i, j){
        const JSON::Value & thisEntry = *j;
        playListEntries newEntry;
        newEntry.relative_filename = thisEntry[0u].asString();
        newEntry.filename = root.link(M.inputLocalVars["playlist_urls"][i.key()]).link(thisEntry[0u].asString()).getUrl();
        newEntry.bytePos = thisEntry[1u].asInt();
        newEntry.mUTC = thisEntry[2u].asInt();
        newEntry.duration = thisEntry[3u].asDouble();
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
    // Set bootMsOffset in order to display the program time correctly in the player
    zUTC = M.inputLocalVars["zUTC"].asInt();
    meta.setUTCOffset(zUTC);
    if (M.getLive()){meta.setBootMsOffset(streamOffset);}
    return true;
  }

  bool inputHLS::readHeader(){
    if (streamIsLive && !isLiveDVR){return true;}
    // to analyse and extract data
    TS::Packet packet; 
    char *data;
    size_t dataLen;
    bool hasPacket = false;
    meta.reInit(isSingular() ? streamName : "");

    tthread::lock_guard<tthread::mutex> guard(entryMutex);

    size_t totalSegments = 0, currentSegment = 0;
    for (std::map<uint32_t, std::deque<playListEntries> >::iterator pListIt = listEntries.begin();
         pListIt != listEntries.end(); pListIt++){
      totalSegments += pListIt->second.size();
    }

    for (std::map<uint32_t, std::deque<playListEntries> >::iterator pListIt = listEntries.begin();
         pListIt != listEntries.end() && config->is_active; pListIt++){
      tsStream.clear();
      uint32_t entId = 0;

      for (std::deque<playListEntries>::iterator entryIt = pListIt->second.begin();
           entryIt != pListIt->second.end() && config->is_active; entryIt++){
        ++currentSegment;
        tsStream.partialClear();

        if (!segDowner.loadSegment(*entryIt)){
          FAIL_MSG("Failed to load segment - skipping to next");
          continue;
        }
        entId++;
        allowRemap = true;
        while ((!segDowner.atEnd() || tsStream.hasPacket()) && config->is_active){
          // Wait for packets on each track to make sure the offset is set based on the earliest packet
          hasPacket = tsStream.hasPacketOnEachTrack() || (segDowner.atEnd() && tsStream.hasPacket());
          if (hasPacket){
            DTSC::Packet headerPack;
            tsStream.getEarliestPacket(headerPack);
            while (headerPack){
              size_t tmpTrackId = headerPack.getTrackId();
              uint64_t packetId = getPacketID(pListIt->first, tmpTrackId);
              uint64_t packetTime = getPacketTime(headerPack.getTime(), tmpTrackId, pListIt->first, entryIt->mUTC);
              size_t idx = M.trackIDToIndex(packetId, getpid());
              if (idx == INVALID_TRACK_ID || !M.getCodec(idx).size()){
                tsStream.initializeMetadata(meta, tmpTrackId, packetId);
                idx = M.trackIDToIndex(packetId, getpid());
              }
              headerPack.getString("data", data, dataLen);

              // keyframe data exists, so always add 19 bytes keyframedata.
              uint32_t packOffset = headerPack.hasMember("offset") ? headerPack.getInt("offset") : 0;
              size_t packSendSize = 24 + (packOffset ? 17 : 0) + (entId >= 0 ? 15 : 0) + 19 + dataLen + 11;
              DONTEVEN_MSG("Adding packet (%zuB) at %" PRIu64 " with an offset of %" PRIu32 " on track %zu", dataLen, packetTime, packOffset, idx);
              meta.update(packetTime, packOffset, idx, dataLen, entId, headerPack.hasMember("keyframe"), packSendSize);
              tsStream.getEarliestPacket(headerPack);
            }
          }
          // No packets available, so read the next TS packet if available
          if (segDowner.readNext()){
            packet.FromPointer(segDowner.packetPtr);
            tsStream.parse(packet, entId);
          }
        }
        // get last packets
        tsStream.finish();
        DTSC::Packet headerPack;
        tsStream.getEarliestPacket(headerPack);
        while (headerPack){
          size_t tmpTrackId = headerPack.getTrackId();
          uint64_t packetId = getPacketID(pListIt->first, tmpTrackId);
          uint64_t packetTime = getPacketTime(headerPack.getTime(), tmpTrackId, pListIt->first, entryIt->mUTC);
          size_t idx = M.trackIDToIndex(packetId, getpid());
          if (idx == INVALID_TRACK_ID || !M.getCodec(idx).size()){
            tsStream.initializeMetadata(meta, tmpTrackId, packetId);
            idx = M.trackIDToIndex(packetId, getpid());
          }

          headerPack.getString("data", data, dataLen);
          // keyframe data exists, so always add 19 bytes keyframedata.
          uint32_t packOffset = headerPack.hasMember("offset") ? headerPack.getInt("offset") : 0;
          size_t packSendSize = 24 + (packOffset ? 17 : 0) + (entId >= 0 ? 15 : 0) + 19 + dataLen + 11;
          DONTEVEN_MSG("Adding packet (%zuB) at %" PRIu64 " with an offset of %" PRIu32 " on track %zu", dataLen, packetTime, packOffset, idx);
          meta.update(packetTime, packOffset, idx, dataLen, entId, headerPack.hasMember("keyframe"), packSendSize);
          tsStream.getEarliestPacket(headerPack);
        }
        // Finally save the offset as part of the TS segment. This is required for bufferframe
        // to work correctly, since not every segment might have an UTC timestamp tag
        if (plsTimeOffset.count(pListIt->first)){
          std::deque<playListEntries> &curList = listEntries[pListIt->first];
          curList.at(entId-1).timeOffset = plsTimeOffset[pListIt->first];
        }else{
          std::deque<playListEntries> &curList = listEntries[pListIt->first];
          curList.at(entId-1).timeOffset = 0;
        }

        //Set progress counter
        if (streamStatus && streamStatus.len > 1){
          streamStatus.mapped[1] = (255 * currentSegment) / totalSegments;
        }
      }
    }
    if (!config->is_active){return false;}

    // set bootMsOffset in order to display the program time correctly in the player
    meta.setUTCOffset(zUTC);
    if (M.getLive()){meta.setBootMsOffset(streamOffset);}
    if (streamIsLive || isLiveDVR){return true;}

    // Set local vars used for parsing existing headers
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
        thisPlaylist.append(thisEntries);
      }
      allEntries[JSON::Value(pListIt->first).asString()] = thisPlaylist;
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
    return true;
  }

  bool inputHLS::needsLock(){
    if (config->getBool("realtime")){return false;}
    if (isLiveDVR){
      return true;
    }
    return !streamIsLive;
  }

  /// \brief Parses new segments added to playlist files as live data
  /// \param segmentIndex: the index of the segment in the current playlist
  /// \return True if the segment has been buffered successfully
  bool inputHLS::parseSegmentAsLive(uint64_t segmentIndex){
    bool hasOffset = false;
    bool hasPacket = false;
    // Keep our own variables to make sure buffering live data does not interfere with VoD pages loading
    TS::Packet packet;
    TS::Stream tsStream;
    char *data;
    size_t dataLen;
    // Get the updated list of entries
    std::deque<playListEntries> &curList = listEntries[currentPlaylist];
    if (curList.size() <= segmentIndex){
      FAIL_MSG("Tried to load segment with index '%" PRIu64 "', but the playlist only contains '%zu' entries!", segmentIndex, curList.size());
      return false;
    }
    if (!segDowner.loadSegment(curList.at(segmentIndex))){
      FAIL_MSG("Failed to load segment");
      return false;
    }

    while (!segDowner.atEnd()){
      // Wait for packets on each track to make sure the offset is set based on the earliest packet
      hasPacket = tsStream.hasPacketOnEachTrack() || (segDowner.atEnd() && tsStream.hasPacket());
      if (hasPacket){
        DTSC::Packet headerPack;
        tsStream.getEarliestPacket(headerPack);
        while (headerPack){
          size_t tmpTrackId = headerPack.getTrackId();
          uint64_t packetId = getPacketID(currentPlaylist, tmpTrackId);
          uint64_t packetTime = headerPack.getTime();
          // Set segment offset and save it
          if (!hasOffset && curList.at(segmentIndex).mUTC){
            hasOffset = true;
            DVRTimeOffsets[currentPlaylist] = (curList.at(segmentIndex).mUTC - zUTC) - packetTime;
            MEDIUM_MSG("Setting current live segment time offset to %" PRId64, DVRTimeOffsets[currentPlaylist]);
            curList.at(segmentIndex).timeOffset = DVRTimeOffsets[currentPlaylist];
          }
          if (hasOffset || DVRTimeOffsets.count(currentPlaylist)){
            hasOffset = true;
            packetTime += DVRTimeOffsets[currentPlaylist];
            HIGH_MSG("Adjusting current packet timestamp %" PRIu64 " -> %" PRIu64, headerPack.getTime(), packetTime);
          }
          size_t idx = M.trackIDToIndex(packetId, getpid());
          if (idx == INVALID_TRACK_ID || !M.getCodec(idx).size()){
            tsStream.initializeMetadata(meta, tmpTrackId, packetId);
            idx = M.trackIDToIndex(packetId, getpid());
          }

          headerPack.getString("data", data, dataLen);
          // keyframe data exists, so always add 19 bytes keyframedata.
          uint32_t packOffset = headerPack.hasMember("offset") ? headerPack.getInt("offset") : 0;
          VERYHIGH_MSG("Adding packet (%zuB) at %" PRIu64 " with an offset of %" PRIu32 " on track %zu", dataLen, packetTime, packOffset, idx);
          bufferLivePacket(packetTime, packOffset, idx, data, dataLen, segmentIndex + 1, headerPack.hasMember("keyframe"));
          tsStream.getEarliestPacket(headerPack);
        }
      }
      // No packets available, so read the next TS packet if available
      if (segDowner.readNext()){
        packet.FromPointer(segDowner.packetPtr);
        tsStream.parse(packet, segmentIndex + 1);
      }
    }
    // get last packets
    tsStream.finish();
    DTSC::Packet headerPack;
    tsStream.getEarliestPacket(headerPack);
    while (headerPack){
      int tmpTrackId = headerPack.getTrackId();
      uint64_t packetId = getPacketID(currentPlaylist, tmpTrackId);
      uint64_t packetTime = headerPack.getTime();
      if (DVRTimeOffsets.count(currentPlaylist)){
        packetTime += DVRTimeOffsets[currentPlaylist];
        VERYHIGH_MSG("Adjusting current packet timestamp %" PRIu64 " -> %" PRIu64, headerPack.getTime(), packetTime);
      }
      size_t idx = M.trackIDToIndex(packetId, getpid());
      if (idx == INVALID_TRACK_ID || !M.getCodec(idx).size()){
        tsStream.initializeMetadata(meta, tmpTrackId, packetId);
        idx = M.trackIDToIndex(packetId, getpid());
      }

      headerPack.getString("data", data, dataLen);
      // keyframe data exists, so always add 19 bytes keyframedata.
      uint32_t packOffset = headerPack.hasMember("offset") ? headerPack.getInt("offset") : 0;
      VERYHIGH_MSG("Adding packet (%zuB) at %" PRIu64 " with an offset of %" PRIu32 " on track %zu", dataLen, packetTime, packOffset, idx);
      bufferLivePacket(packetTime, packOffset, idx, data, dataLen, segmentIndex + 1, headerPack.hasMember("keyframe"));
      tsStream.getEarliestPacket(headerPack);
    }
    return true;
  }

  /// \brief Override userLeadOut to buffer new data as live packets
  void inputHLS::userLeadOut(){
    Input::userLeadOut();
    if (!isLiveDVR){
      return;
    }

    // Update all playlists to make sure listEntries contains all live segments
    for (std::map<uint64_t, Playlist>::iterator pListIt = playlistMapping.begin();
         pListIt != playlistMapping.end(); pListIt++){
      if (pListIt->second.reloadNext < Util::bootSecs()){
        pListIt->second.reload();
      }
    }

    HIGH_MSG("Current playlist has parsed %zu/%" PRIu64 " entries", listEntries[currentPlaylist].size(), parsedSegments[currentPlaylist]);
    for(uint64_t entryIt = parsedSegments[currentPlaylist]; entryIt < listEntries[currentPlaylist].size(); entryIt++){
      MEDIUM_MSG("Adding entry #%" PRIu64 " as live data", entryIt);
      if (parseSegmentAsLive(entryIt)){
        parsedSegments[currentPlaylist]++;
      }else{
        break;
      }
    }
  }

  bool inputHLS::openStreamSource(){return true;}

  void inputHLS::getNext(size_t idx){
    INSANE_MSG("Getting next");
    uint32_t tid = 0;
    bool finished = false;
    thisPacket.null();
    while (config->is_active && (needsLock() || keepAlive())){
      // Check if we have a packet
      bool hasPacket = false;
      if (idx == INVALID_TRACK_ID){
        hasPacket = tsStream.hasPacketOnEachTrack() || (segDowner.atEnd() && tsStream.hasPacket());
      }else{
        hasPacket = tsStream.hasPacket(getMappedTrackId(M.getID(idx)));
      }

      // Yes? Excellent! Read and return it.
      if (hasPacket){
        // Read
        if (idx == INVALID_TRACK_ID){
          tsStream.getEarliestPacket(thisPacket);
          tid = getOriginalTrackId(currentPlaylist, thisPacket.getTrackId());
          if (!tid){
            INSANE_MSG("Track %zu on PLS %" PRIu64 " -> %" PRIu32, thisPacket.getTrackId(), currentPlaylist, tid);
            continue;
          }
        }else{
          tid = getMappedTrackId(M.getID(idx));
          tsStream.getPacket(tid, thisPacket);
        }
        if (!thisPacket){
          Util::logExitReason(ER_FORMAT_SPECIFIC, "Could not getNext TS packet!");
          return;
        }

        uint64_t packetTime = getPacketTime(thisPacket.getTime(), tid, currentPlaylist, nUTC);
        INSANE_MSG("Packet %" PRIu32 "@%" PRIu64 "ms -> %" PRIu64 "ms", tid, thisPacket.getTime(), packetTime);
        // overwrite trackId on success
        Bit::htobl(thisPacket.getData() + 8, tid);
        Bit::htobll(thisPacket.getData() + 12, packetTime);
        thisTime = packetTime;
        thisIdx = tid;
        return; // Success!
      }

      // No? Let's read some more data and check again.
      if (!segDowner.atEnd() && segDowner.readNext()){
        tsBuf.FromPointer(segDowner.packetPtr);
        tsStream.parse(tsBuf, streamIsLive && !isLiveDVR ? 0 : currentIndex + 1);
        continue; // check again
      }

      // Okay, reading more is not possible. Let's call finish() and check again.
      if (!finished && segDowner.atEnd()){
        tsStream.finish();
        finished = true;
        VERYHIGH_MSG("Finishing reading TS segment");
        continue; // Check again!
      }

      // No? Then we want to try reading the next file.

      // No segments? Wait until next playlist reloading time.
      if (idx != INVALID_TRACK_ID){
        currentPlaylist = getMappedTrackPlaylist(M.getID(idx));
      }else{
        currentPlaylist = firstSegment();
      }
      if (currentPlaylist == 0){
        VERYHIGH_MSG("Waiting for segments...");
        Util::wait(500);
        continue;
      }

      // Now that we know our playlist is up-to-date, actually try to read the file.
      VERYHIGH_MSG("Moving on to next TS segment (variant %" PRIu64 ")", currentPlaylist);
      if (readNextFile()){
        MEDIUM_MSG("Next segment read successfully");
        finished = false;
        continue; // Success! Continue regular parsing.
      }else{
        if (userSelect.size() > 1){
          // failed to read segment for playlist, dropping it
          WARN_MSG("Dropping variant %" PRIu64 " because we couldn't read anything from it", currentPlaylist);
          tthread::lock_guard<tthread::mutex> guard(entryMutex);
          listEntries.erase(currentPlaylist);
          if (listEntries.size()){continue;}
        }
      }

      // Nothing works!
      // HLS input will now quit trying to prevent severe mental depression.
      Util::logExitReason(ER_CLEAN_EOF, "No packets can be read - exhausted all playlists");
      thisPacket.null();
      return;
    }
  }

  // Note: bpos is overloaded here for playlist entry!
  void inputHLS::seek(uint64_t seekTime, size_t idx){
    if (idx == INVALID_TRACK_ID){return;}
    plsTimeOffset.clear();
    plsLastTime.clear();
    plsInterval.clear();
    tsStream.clear();
    uint64_t trackId = M.getID(idx);

    unsigned long plistEntry = 0;

    DTSC::Keys keys(M.keys(idx));
    for (size_t i = keys.getFirstValid(); i < keys.getEndValid(); i++){
      if (keys.getTime(i) > seekTime){
        VERYHIGH_MSG("Found elapsed key with a time of %" PRIu64 " ms at playlist index %zu while seeking", keys.getTime(i), keys.getBpos(i)-1);
        break;
      }
      VERYHIGH_MSG("Found valid key with a time of %" PRIu64 " ms at playlist index %zu while seeking", keys.getTime(i), keys.getBpos(i)-1);
      plistEntry = keys.getBpos(i);
    }

    if (plistEntry < 1){
      WARN_MSG("attempted to seek outside the file");
      return;
    }

    currentIndex = plistEntry - 1;
    currentPlaylist = getMappedTrackPlaylist(trackId);
    VERYHIGH_MSG("Seeking to index %zu on playlist %" PRIu64, currentIndex, currentPlaylist);

    {// Lock mutex for listEntries
      tthread::lock_guard<tthread::mutex> guard(entryMutex);
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
      playListEntries &entry = curPlaylist.at(currentIndex);
      segDowner.loadSegment(entry);
      // If we have an offset, load it
      if (entry.timeOffset){
        HIGH_MSG("Setting time offset of this TS segment to %" PRId64, entry.timeOffset);
        plsTimeOffset[currentPlaylist] = entry.timeOffset;
        allowRemap = false;
      }
    }

    HIGH_MSG("readPMT()");
    TS::Packet tsBuffer;
    while (!tsStream.hasPacketOnEachTrack() && !segDowner.atEnd()){
      if (!segDowner.readNext()){break;}
      tsBuffer.FromPointer(segDowner.packetPtr);
      tsStream.parse(tsBuffer, streamIsLive && !isLiveDVR ? 0 : plistEntry);
    } 
  }

  /// \brief Applies any offset to the packets original timestamp
  /// \param packetTime: the original timestamp of the packet
  /// \param tid: the trackid corresponding to this track and playlist
  /// \param currentPlaylist: the ID of the playlist we are currently trying to parse
  /// \param nUTC: Defaults to 0. If larger than 0, sync the timestamp based on this value and zUTC
  /// \return the (modified) packetTime, used for meta.updates and buffering packets
  uint64_t inputHLS::getPacketTime(uint64_t packetTime, uint64_t tid, uint64_t currentPlaylist, uint64_t nUTC){
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
  uint64_t inputHLS::getPacketID(uint64_t currentPlaylist, uint64_t trackId){
    uint64_t packetId = pidMapping[(((uint64_t)currentPlaylist) << 32) + trackId];
    if (packetId == 0){
      pidMapping[(((uint64_t)currentPlaylist) << 32) + trackId] = pidCounter;
      pidMappingR[pidCounter] = (((uint64_t)currentPlaylist) << 32) + trackId;
      packetId = pidCounter;
      pidCounter++;
    }
    return packetId;
  }

  size_t inputHLS::getEntryId(uint32_t playlistId, uint64_t bytePos){
    if (bytePos == 0){return 0;}
    tthread::lock_guard<tthread::mutex> guard(entryMutex);
    for (int i = 0; i < listEntries[playlistId].size(); i++){
      if (listEntries[playlistId].at(i).bytePos > bytePos){return i - 1;}
    }
    return listEntries[playlistId].size() - 1;
  }

  uint64_t inputHLS::getOriginalTrackId(uint32_t playlistId, uint32_t id){
    return pidMapping[(((uint64_t)playlistId) << 32) + id];
  }

  uint32_t inputHLS::getMappedTrackId(uint64_t id){
    static uint64_t lastIn = id;
    static uint32_t lastOut = (pidMappingR[id] & 0xFFFFFFFFull);
    if (lastIn != id){
      lastIn = id;
      lastOut = (pidMappingR[id] & 0xFFFFFFFFull);
    }
    return lastOut;
  }

  uint32_t inputHLS::getMappedTrackPlaylist(uint64_t id){
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

  /// \brief Sets parsedSegments for all playlists, specifying how many segments
  ///        have already been parsed. Additional segments can then be parsed as live data
  void inputHLS::setParsedSegments(){
     for (std::map<uint32_t, std::deque<playListEntries> >::iterator pListIt = listEntries.begin();
         pListIt != listEntries.end(); pListIt++){
      parsedSegments[pListIt->first] = pListIt->second.size();
      INFO_MSG("Playlist %" PRIu32 " already contains %" PRIu64 " VOD segments", pListIt->first, parsedSegments[pListIt->first]);
    }
  }

  /// Parses the main playlist, possibly containing variants.
  bool inputHLS::initPlaylist(const std::string &uri, bool fullInit){
    // Used to set zUTC, in case the first EXT-X-PROGRAM-DATE-TIME does not appear before the first segment
    float timestampSum = 0;
    bool isRegularPls = false;
    plsInitCount = 0;
    plsTotalCount = 0;
    {
      tthread::lock_guard<tthread::mutex> guard(entryMutex);
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
      if (line.compare(0, 26, "#EXT-X-PLAYLIST-TYPE:EVENT") == 0){isLiveDVR = true;}
      if (line.compare(0, 14, "#EXT-X-ENDLIST") == 0){isLiveDVR = false;}
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

          ret = readPlaylist(playlistRootPath.link(line), line, fullInit);
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
            ret = readPlaylist(playlistRootPath.link(mediafile), mediafile, fullInit);
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
        nUTC = zUTC;
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
      ret = readPlaylist(playlistRootPath.getUrl(), "", fullInit);
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
  bool inputHLS::readPlaylist(const HTTP::URL &uri, const  std::string & relurl, bool fullInit){
    std::string urlBuffer;
    // Wildcard streams can have a ' ' in the name, which getUrl converts to a '+'
    if (uri.isLocalPath()){
      urlBuffer = (fullInit ? "" : ";") + uri.getFilePath() + "\n" + relurl;
    }
    else{
      urlBuffer = (fullInit ? "" : ";") + uri.getUrl() + "\n" + relurl;
    }
    INFO_MSG("Adding playlist(s): %s", urlBuffer.c_str());
    tthread::thread runList(playlistRunner, (void *)urlBuffer.data());
    runList.detach(); // Abandon the thread, it's now running independently
    uint32_t timeout = 0;
    while (urlBuffer.data()[0] && ++timeout < 100){Util::sleep(100);}
    if (timeout >= 100){WARN_MSG("Thread start timed out for: %s", urlBuffer.c_str());}
    return true;
  }

  /// Read next .ts file from the playlist. (from the list of entries which needs
  /// to be processed)
  bool inputHLS::readNextFile(){
    tsStream.clear();

    playListEntries ntry;
    // This scope limiter prevents the recursion down below from deadlocking us
    {
      tthread::lock_guard<tthread::mutex> guard(entryMutex);
      std::deque<playListEntries> &curList = listEntries[currentPlaylist];
      INSANE_MSG("Current playlist contains %zu entries. Current index is %zu in playlist %" PRIu64, curList.size(), currentIndex, currentPlaylist);
      if (!curList.size()){
        INFO_MSG("Reached last entry in playlist %" PRIu64 "; waiting for more segments", currentPlaylist);
        if (streamIsLive || isLiveDVR){Util::wait(500);}
        return false;
      }
      if (!streamIsLive || isLiveDVR){
        // VoD advances the index by one and attempts to read
        // The playlist is not altered in this case, since we may need to seek back later
        currentIndex++;
        if (curList.size() - 1 < currentIndex){
          INFO_MSG("Reached last entry");
          return false;
        }
        ntry = curList[currentIndex];
      }else{
        // Live does not use the currentIndex, but simply takes the first segment
        // That segment is then removed from the playlist so we don't read it again - live streams can't seek anyway
        ntry = *curList.begin();
        curList.pop_front();

        if (Util::bootSecs() < ntry.timestamp){
          VERYHIGH_MSG("Slowing down to realtime...");
          while (Util::bootSecs() < ntry.timestamp){
            keepAlive();
            Util::wait(250);
          }
        }
      }
    }

    if (!segDowner.loadSegment(ntry)){
      ERROR_MSG("Could not download segment: %s", ntry.filename.c_str());
      return readNextFile(); // Attempt to read another, if possible.
    }
    // If we have an offset, load it
    if (ntry.timeOffset){
      plsTimeOffset[currentPlaylist] = ntry.timeOffset;
      allowRemap = false;
    // Else allow of the offset to be set by getPacketTime
    }else{
      nUTC = ntry.mUTC;
      allowRemap = true;
    }
    return true;
  }

  /// return the playlist id from which we need to read the first upcoming segment
  /// by timestamp.
  /// this will keep the playlists in sync while reading segments.
  size_t inputHLS::firstSegment(){
    // Only one selected? Immediately return the right playlist.
    if (!streamIsLive){return getMappedTrackPlaylist(M.getID(userSelect.begin()->first));}
    uint64_t firstTimeStamp = 0;
    int tmpId = -1;
    int segCount = 0;

    tthread::lock_guard<tthread::mutex> guard(entryMutex);
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

}// namespace Mist
