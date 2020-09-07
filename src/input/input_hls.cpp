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

  /// Mutex for accesses to listEntries
  tthread::mutex entryMutex;

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
    if (nProxy.userClient.isAlive()){nProxy.userClient.keepAlive();}
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
    plsInitCount++;
    if (initOnly){return;}// Exit because init-only mode

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

  Playlist::Playlist(const std::string &uriSrc){
    nextUTC = 0;
    id = 0; // to be set later
    INFO_MSG("Adding variant playlist: %s", uriSrc.c_str());
    plsDL.dataTimeout = 15;
    plsDL.retryCount = 8;
    lastFileIndex = 0;
    waitTime = 2;
    playlistEnd = false;
    noChangeCount = 0;
    lastTimestamp = 0;
    root = HTTP::URL(uriSrc);
    uri = root.getUrl();
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
    segDL.onProgress(callbackFunc);
    encrypted = false;
  }

  /// Returns true if packetPtr is at the end of the current segment.
  bool SegmentDownloader::atEnd() const{
    return segDL.isEOF();
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
      size_t len = 0;
      segDL.readSome(packetPtr, len, 188);
      if (len != 188 || packetPtr[0] != 0x47){
        FAIL_MSG("Not a valid TS packet: len %zu, first byte %" PRIu8, len, (uint8_t)packetPtr[0]);
        return false;
      }
      return true;
    }
  }

  /// Attempts to read a single TS packet from the current segment, setting packetPtr on success
  void SegmentDownloader::close(){
    packetPtr = 0;
    segDL.close();
  }

  /// Loads the given segment URL into the segment buffer.
  bool SegmentDownloader::loadSegment(const playListEntries &entry){
    std::string hexKey = printhex(entry.keyAES, 16);
    std::string hexIvec = printhex(entry.ivec, 16);

    MEDIUM_MSG("Loading segment: %s, key: %s, ivec: %s", entry.filename.c_str(), hexKey.c_str(),
               hexIvec.c_str());
    if (!segDL.open(entry.filename)){
      FAIL_MSG("Could not open %s", entry.filename.c_str());
      return false;
    }

    if (!segDL){return false;}

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
      if (!plsDL.get(uri) || !plsDL.isOk()){
        FAIL_MSG("Could not download playlist '%s', aborting: %" PRIu32 " %s", uri.c_str(),
                 plsDL.getStatusCode(), plsDL.getStatusText().c_str());
        reloadNext = Util::bootSecs() + waitTime;
        return false;
      }
      urlSource.str(plsDL.data());
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

    while (std::getline(input, line)){
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
          if (waitTime < 5){waitTime = 5;}
        }

        if (key == "MEDIA-SEQUENCE"){fileNo = atoll(val.c_str());}
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
      if (fileNo >= lastFileIndex){
        cleanLine(filename);
        filename = root.link(filename).getUrl();
        char ivec[16];
        if (keyIV.size()){
          parseKey(keyIV, ivec, 16);
        }else{
          memset(ivec, 0, 16);
          Bit::htobll(ivec + 8, fileNo);
        }
        addEntry(filename, f, totalBytes, keys[keyUri], std::string(ivec, 16));
        lastFileIndex = fileNo + 1;
        ++count;
      }
      nextUTC = 0;
      ++fileNo;
    }

    // VOD over HTTP needs to be processed as LIVE.
    if (isUrl()){
      playlistType = LIVE;
    }else{
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
  void Playlist::addEntry(const std::string &filename, float duration, uint64_t &totalBytes,
                          const std::string &key, const std::string &iv){
    // if (!isSupportedFile(filename)){
    //  WARN_MSG("Ignoring unsupported file: %s", filename.c_str());
    //  return;
    //}

    playListEntries entry;
    entry.filename = filename;
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
      listEntries[id].push_back(entry);
      DONTEVEN_MSG("Added segment to variant %" PRIu32 " (#%" PRIu64 ", now %zu queued): %s", id,
                   lastFileIndex, listEntries[id].size(), filename.c_str());
    }
  }

  /// Constructor of HLS Input
  inputHLS::inputHLS(Util::Config *cfg) : Input(cfg){
    zUTC = nUTC = 0;
    self = this;
    streamIsLive = false;
    globalWaitTime = 0;
    currentPlaylist = 0;

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
    // All URLs can be set to always-on mode.
    capa["always_match"] = capa["source_match"];

    capa["priority"] = 9;
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("AC3");
    capa["codecs"][0u][1u].append("MP3");

    inFile = NULL;
  }

  inputHLS::~inputHLS(){
    if (inFile){fclose(inFile);}
  }

  bool inputHLS::checkArguments(){
    config->is_active = true;
    if (config->getString("input") == "-"){return false;}
    HTTP::URL mainPls(config->getString("input"));
    if (mainPls.getExt().substr(0, 3) != "m3u" && mainPls.protocol.find("hls") == std::string::npos){
      return false;
    }
    if (!initPlaylist(config->getString("input"), false)){return false;}
    return true;
  }

  void inputHLS::trackSelect(std::string trackSpec){
    selectedTracks.clear();
    size_t index;
    while (trackSpec != ""){
      index = trackSpec.find(' ');
      selectedTracks.insert(atoi(trackSpec.substr(0, index).c_str()));
      if (index != std::string::npos){
        trackSpec.erase(0, index + 1);
      }else{
        trackSpec = "";
      }
    }
  }

  void inputHLS::parseStreamHeader(){
    if (!initPlaylist(config->getString("input"))){
      FAIL_MSG("Failed to load HLS playlist, aborting");
      myMeta = DTSC::Meta();
      return;
    }
    myMeta = DTSC::Meta();
    myMeta.live = true;
    myMeta.vod = false;
    INFO_MSG("Parsing live stream to create header...");
    TS::Packet packet; // to analyse and extract data
    int counter = 1;

    tthread::lock_guard<tthread::mutex> guard(entryMutex);
    for (std::map<uint32_t, std::deque<playListEntries> >::iterator pListIt = listEntries.begin();
         pListIt != listEntries.end(); pListIt++){
      // Skip empty playlists
      if (!pListIt->second.size()){continue;}
      int preCounter = counter;
      tsStream.clear();

      for (std::deque<playListEntries>::iterator entryIt = pListIt->second.begin();
           entryIt != pListIt->second.end(); ++entryIt){
        nProxy.userClient.keepAlive();
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
              uint64_t packetId = pidMapping[(((uint64_t)pListIt->first) << 32) + tmpTrackId];

              if (packetId == 0){
                pidMapping[(((uint64_t)pListIt->first) << 32) + headerPack.getTrackId()] = counter;
                pidMappingR[counter] = (((uint64_t)pListIt->first) << 32) + headerPack.getTrackId();
                packetId = counter;
                VERYHIGH_MSG("Added file %s, trackid: %zu, mapped to: %d",
                             entryIt->filename.c_str(), headerPack.getTrackId(), counter);
                counter++;
              }

              if ((!myMeta.tracks.count(packetId) || !myMeta.tracks[packetId].codec.size())){
                tsStream.initializeMetadata(myMeta, tmpTrackId, packetId);
                myMeta.tracks[packetId].minKeepAway = globalWaitTime * 2000;
                VERYHIGH_MSG("setting minKeepAway = %d for track: %" PRIu64,
                             myMeta.tracks[packetId].minKeepAway, packetId);
              }
            }
            break; // we have all tracks discovered, next playlist!
          }
        }while (!segDowner.atEnd());
        if (preCounter < counter){break;}// We're done reading this playlist!
      }
    }
    tsStream.clear();
    currentPlaylist = 0;
    segDowner.close(); // make sure we have nothing left over
    INFO_MSG("header complete, beginning live ingest of %d tracks", counter - 1);
  }

  bool inputHLS::readHeader(){
    if (streamIsLive){return true;}

    bool hasHeader = false;

    // See whether a separate header file exists.
    DTSC::File tmp(config->getString("input") + ".dtsh");
    if (tmp){
      myMeta = tmp.getMeta();
      if (myMeta){hasHeader = true;}
    }

    if (!hasHeader){myMeta = DTSC::Meta();}

    TS::Packet packet; // to analyse and extract data

    char *data;
    size_t dataLen;
    int counter = 1;

    tthread::lock_guard<tthread::mutex> guard(entryMutex);
    for (std::map<uint32_t, std::deque<playListEntries> >::iterator pListIt = listEntries.begin();
         pListIt != listEntries.end(); pListIt++){
      tsStream.clear();
      uint32_t entId = 0;

      for (std::deque<playListEntries>::iterator entryIt = pListIt->second.begin();
           entryIt != pListIt->second.end(); entryIt++){
        tsStream.partialClear();

        if (!segDowner.loadSegment(*entryIt)){
          FAIL_MSG("Failed to load segment - skipping to next");
          continue;
        }
        entId++;
        while (!segDowner.atEnd()){

          while (tsStream.hasPacketOnEachTrack()){
            DTSC::Packet headerPack;
            tsStream.getEarliestPacket(headerPack);

            int tmpTrackId = headerPack.getTrackId();
            uint64_t packetId = pidMapping[(((uint64_t)pListIt->first) << 32) + tmpTrackId];

            if (packetId == 0){
              pidMapping[(((uint64_t)pListIt->first) << 32) + headerPack.getTrackId()] = counter;
              pidMappingR[counter] = (((uint64_t)pListIt->first) << 32) + headerPack.getTrackId();
              packetId = counter;
              INFO_MSG("Added file %s, trackid: %zu, mapped to: %d", entryIt->filename.c_str(),
                       headerPack.getTrackId(), counter);
              counter++;
            }

            if (!hasHeader && (!myMeta.tracks.count(packetId) || !myMeta.tracks[packetId].codec.size())){
              tsStream.initializeMetadata(myMeta, tmpTrackId, packetId);
            }

            if (!hasHeader){
              headerPack.getString("data", data, dataLen);
              uint64_t pBPos = headerPack.getInt("bpos");

              // keyframe data exists, so always add 19 bytes keyframedata.
              long long packOffset = headerPack.hasMember("offset") ? headerPack.getInt("offset") : 0;
              long long packSendSize = 24 + (packOffset ? 17 : 0) + (entId >= 0 ? 15 : 0) + 19 + dataLen + 11;
              myMeta.update(headerPack.getTime(), packOffset, packetId, dataLen, entId,
                            headerPack.hasMember("keyframe"), packSendSize);
            }
          }

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
          int tmpTrackId = headerPack.getTrackId();
          uint64_t packetId = pidMapping[(((uint64_t)pListIt->first) << 32) + tmpTrackId];

          if (packetId == 0){
            pidMapping[(((uint64_t)pListIt->first) << 32) + headerPack.getTrackId()] = counter;
            pidMappingR[counter] = (((uint64_t)pListIt->first) << 32) + headerPack.getTrackId();
            packetId = counter;
            INFO_MSG("Added file %s, trackid: %zu, mapped to: %d", entryIt->filename.c_str(),
                     headerPack.getTrackId(), counter);
            counter++;
          }

          if (!hasHeader && (!myMeta.tracks.count(packetId) || !myMeta.tracks[packetId].codec.size())){
            tsStream.initializeMetadata(myMeta, tmpTrackId, packetId);
          }

          if (!hasHeader){
            headerPack.getString("data", data, dataLen);
            uint64_t pBPos = headerPack.getInt("bpos");

            // keyframe data exists, so always add 19 bytes keyframedata.
            long long packOffset = headerPack.hasMember("offset") ? headerPack.getInt("offset") : 0;
            long long packSendSize = 24 + (packOffset ? 17 : 0) + (entId >= 0 ? 15 : 0) + 19 + dataLen + 11;
            myMeta.update(headerPack.getTime(), packOffset, packetId, dataLen, entId,
                          headerPack.hasMember("keyframe"), packSendSize);
          }
          tsStream.getEarliestPacket(headerPack);
        }

        if (hasHeader){break;}
      }
    }

    if (streamIsLive){return true;}

    INFO_MSG("write header file...");
    std::ofstream oFile((config->getString("input") + ".dtsh").c_str());

    oFile << myMeta.toJSON().toNetPacked();
    oFile.close();

    return true;
  }

  bool inputHLS::needsLock(){return !streamIsLive;}

  bool inputHLS::openStreamSource(){return true;}

  void inputHLS::getNext(bool smart){
    INSANE_MSG("Getting next");
    uint32_t tid = 0;
    bool finished = false;
    if (selectedTracks.size()){tid = *selectedTracks.begin();}
    thisPacket.null();
    while (config->is_active && (needsLock() || nProxy.userClient.isAlive())){

      // Check if we have a packet
      bool hasPacket = false;
      if (streamIsLive){
        hasPacket = tsStream.hasPacketOnEachTrack() || (segDowner.atEnd() && tsStream.hasPacket());
      }else{
        hasPacket = tsStream.hasPacket(getMappedTrackId(tid));
      }

      // Yes? Excellent! Read and return it.
      if (hasPacket){
        // Read
        if (myMeta.live){
          tsStream.getEarliestPacket(thisPacket);
          tid = getOriginalTrackId(currentPlaylist, thisPacket.getTrackId());
          if (!tid){
            INFO_MSG("Track %" PRIu64 " on PLS %u -> %" PRIu32, thisPacket.getTrackId(), currentPlaylist, tid);
            continue;
          }
        }else{
          tsStream.getPacket(getMappedTrackId(tid), thisPacket);
        }
        if (!thisPacket){
          FAIL_MSG("Could not getNext TS packet!");
          return;
        }

        uint64_t newTime = thisPacket.getTime();

        // Apply offset if any was set
        if (plsTimeOffset.count(currentPlaylist)){newTime += plsTimeOffset[currentPlaylist];}

        if (zUTC){
          if (allowSoftRemap && thisPacket.getTime() < 1000){allowSoftRemap = false;}
          // UTC based timestamp offsets
          if ((allowRemap || allowSoftRemap) && nUTC){
            allowRemap = false;
            allowSoftRemap = !thisPacket.getTime();
            int64_t prevOffset = plsTimeOffset[currentPlaylist];
            plsTimeOffset[currentPlaylist] = (nUTC - zUTC) - thisPacket.getTime();
            newTime = thisPacket.getTime() + plsTimeOffset[currentPlaylist];
            INFO_MSG("[UTC; New offset: %" PRId64 " -> %" PRId64 "] Packet %" PRIu32 "@%" PRIu64
                     "ms -> %" PRIu64 "ms",
                     prevOffset, plsTimeOffset[currentPlaylist], tid, thisPacket.getTime(), newTime);
          }
        }else{
          // Non-UTC based
          if (plsLastTime.count(currentPlaylist)){
            if (plsInterval.count(currentPlaylist)){
              if (allowRemap && (newTime < plsLastTime[currentPlaylist] ||
                                 newTime > plsLastTime[currentPlaylist] + plsInterval[currentPlaylist] * 60)){
                allowRemap = false;
                // time difference too great, change offset to correct for it
                int64_t prevOffset = plsTimeOffset[currentPlaylist];
                plsTimeOffset[currentPlaylist] +=
                    (int64_t)(plsLastTime[currentPlaylist] + plsInterval[currentPlaylist]) - (int64_t)newTime;
                newTime = thisPacket.getTime() + plsTimeOffset[currentPlaylist];
                INFO_MSG("[Guess; New offset: %" PRId64 " -> %" PRId64 "] Packet %" PRIu32
                         "@%" PRIu64 "ms -> %" PRIu64 "ms",
                         prevOffset, plsTimeOffset[currentPlaylist], tid, thisPacket.getTime(), newTime);
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

        DONTEVEN_MSG("Packet %" PRIu32 "@%" PRIu64 "ms -> %" PRIu64 "ms", tid, thisPacket.getTime(), newTime);
        // overwrite trackId on success
        Bit::htobl(thisPacket.getData() + 8, tid);
        Bit::htobll(thisPacket.getData() + 12, newTime);
        return; // Success!
      }

      // No? Let's read some more data and check again.
      if (!segDowner.atEnd() && segDowner.readNext()){
        tsBuf.FromPointer(segDowner.packetPtr);
        tsStream.parse(tsBuf, streamIsLive ? 0 : currentIndex);
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
      currentPlaylist = firstSegment();
      if (currentPlaylist < 0){
        VERYHIGH_MSG("Waiting for segments...");
        if (nProxy.userClient.isAlive()){nProxy.userClient.keepAlive();}
        Util::wait(500);
        continue;
      }

      // Now that we know our playlist is up-to-date, actually try to read the file.
      VERYHIGH_MSG("Moving on to next TS segment (variant %" PRIu32 ")", currentPlaylist);
      if (readNextFile()){
        MEDIUM_MSG("Next segment read successfully");
        finished = false;
        continue; // Success! Continue regular parsing.
      }else{
        if (selectedTracks.size() > 1){
          // failed to read segment for playlist, dropping it
          WARN_MSG("Dropping variant %" PRIu32 " because we couldn't read anything from it", currentPlaylist);
          tthread::lock_guard<tthread::mutex> guard(entryMutex);
          listEntries.erase(currentPlaylist);
          if (listEntries.size()){continue;}
        }
      }

      // Nothing works!
      // HLS input will now quit trying to prevent severe mental depression.
      INFO_MSG("No packets can be read - exhausted all playlists");
      thisPacket.null();
      return;
    }
  }

  // Note: bpos is overloaded here for playlist entry!
  void inputHLS::seek(int seekTime){
    plsTimeOffset.clear();
    plsLastTime.clear();
    plsInterval.clear();
    tsStream.clear();
    int trackId = 0;

    unsigned long plistEntry = 0xFFFFFFFFull;
    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      unsigned long thisBPos = 0;
      for (std::deque<DTSC::Key>::iterator keyIt = myMeta.tracks[*it].keys.begin();
           keyIt != myMeta.tracks[*it].keys.end(); keyIt++){
        if (keyIt->getTime() > seekTime){break;}
        thisBPos = keyIt->getBpos();
      }
      if (thisBPos < plistEntry){
        plistEntry = thisBPos;
        trackId = *it;
      }
    }

    if (plistEntry < 1){
      WARN_MSG("attempted to seek outside the file");
      return;
    }

    currentIndex = plistEntry - 1;
    currentPlaylist = getMappedTrackPlaylist(trackId);
    INFO_MSG("Seeking to index %d on playlist %d", currentIndex, currentPlaylist);

    {// Lock mutex for listEntries
      tthread::lock_guard<tthread::mutex> guard(entryMutex);
      if (!listEntries.count(currentPlaylist)){
        WARN_MSG("Playlist %d not loaded, aborting seek", currentPlaylist);
        return;
      }
      std::deque<playListEntries> &curPlaylist = listEntries[currentPlaylist];
      if (curPlaylist.size() <= currentIndex){
        WARN_MSG("Playlist %d has %zu <= %d entries, aborting seek", currentPlaylist,
                 curPlaylist.size(), currentIndex);
        return;
      }
      playListEntries &entry = curPlaylist.at(currentIndex);
      segDowner.loadSegment(entry);
    }

    HIGH_MSG("readPMT()");
    TS::Packet tsBuffer;
    while (!tsStream.hasPacketOnEachTrack() && !segDowner.atEnd()){
      if (!segDowner.readNext()){break;}
      tsBuffer.FromPointer(segDowner.packetPtr);
      tsStream.parse(tsBuffer, 0);
    }
  }

  int inputHLS::getEntryId(int playlistId, uint64_t bytePos){
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

  /// Parses the main playlist, possibly containing variants.
  bool inputHLS::initPlaylist(const std::string &uri, bool fullInit){
    plsInitCount = 0;
    plsTotalCount = 0;
    {
      tthread::lock_guard<tthread::mutex> guard(entryMutex);
      listEntries.clear();
    }
    std::string line;
    bool ret = false;
    startTime = Util::bootSecs();

    HTTP::URL playlistRootPath(uri);
    // Convert custom http(s)-hls protocols into regular notation.
    if (playlistRootPath.protocol == "http-hls"){playlistRootPath.protocol = "http";}
    if (playlistRootPath.protocol == "https-hls"){playlistRootPath.protocol = "https";}

    std::istringstream urlSource;
    std::ifstream fileSource;

    bool isUrl = (uri.find("://") != std::string::npos);
    if (isUrl){
      HTTP::Downloader plsDL;
      plsDL.dataTimeout = 15;
      plsDL.retryCount = 8;
      if (!plsDL.get(playlistRootPath) || !plsDL.isOk()){
        FAIL_MSG("Could not download main playlist, aborting.");
        return false;
      }
      urlSource.str(plsDL.data());
    }else{
      // If we're not a URL and there is no / at the start, ensure we get the full absolute path.
      if (uri[0] != '/'){
        char *rp = realpath(uri.c_str(), 0);
        if (rp){
          playlistRootPath = HTTP::URL((std::string)rp);
          free(rp);
        }
      }
      fileSource.open(uri.c_str());
      if (!fileSource.good()){
        FAIL_MSG("Could not open playlist (%s): %s", strerror(errno), uri.c_str());
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

          ret = readPlaylist(playlistRootPath.link(line), fullInit);
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
            ret = readPlaylist(playlistRootPath.link(mediafile), fullInit);
          }
        }

      }else if (line.compare(0, 7, "#EXTINF") == 0){
        // current file is not a variant playlist, but regular playlist.
        ret = readPlaylist(playlistRootPath.getUrl(), fullInit);
        break;
      }else{
        // ignore wrong lines
        VERYHIGH_MSG("ignore wrong line: %s", line.c_str());
      }
    }

    if (!isUrl){fileSource.close();}

    uint32_t maxWait = 0;
    unsigned int lastCount = 9999;
    while (plsTotalCount != plsInitCount && ++maxWait < 50){
      if (plsInitCount != lastCount){
        lastCount = plsInitCount;
        INFO_MSG("Waiting for variant playlists to load... %u/%u", lastCount, plsTotalCount);
      }
      Util::sleep(1000);
    }
    if (maxWait >= 50){
      WARN_MSG("Timeout waiting for variant playlists (%u/%u)", plsInitCount, plsTotalCount);
    }
    plsInitCount = 0;
    plsTotalCount = 0;

    return ret;
  }

  /// Function for reading every playlist.
  bool inputHLS::readPlaylist(const HTTP::URL &uri, bool fullInit){
    std::string urlBuffer = (fullInit ? "" : ";") + uri.getUrl();
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
      if (!curList.size()){
        WARN_MSG("no entries found in playlist: %d!", currentPlaylist);
        return false;
      }
      if (!streamIsLive){
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
        if (!curList.size()){
          INFO_MSG("Reached last entry");
          return false;
        }
        ntry = *curList.begin();
        curList.pop_front();

        if (Util::bootSecs() < ntry.timestamp){
          VERYHIGH_MSG("Slowing down to realtime...");
          while (Util::bootSecs() < ntry.timestamp){
            if (nProxy.userClient.isAlive()){nProxy.userClient.keepAlive();}
            Util::wait(250);
          }
        }
      }
    }

    if (!segDowner.loadSegment(ntry)){
      ERROR_MSG("Could not download segment: %s", ntry.filename.c_str());
      return readNextFile(); // Attempt to read another, if possible.
    }
    nUTC = ntry.mUTC;
    // If we don't have a zero-time yet, guess an hour before this UTC time is probably fine
    if (nUTC && !zUTC){zUTC = nUTC - 3600000;}
    allowRemap = true;
    allowSoftRemap = false;
    return true;
  }

  /// return the playlist id from which we need to read the first upcoming segment
  /// by timestamp.
  /// this will keep the playlists in sync while reading segments.
  int inputHLS::firstSegment(){
    // Only one selected? Immediately return the right playlist.
    if (selectedTracks.size() == 1){return getMappedTrackPlaylist(*selectedTracks.begin());}
    uint64_t firstTimeStamp = 0;
    int tmpId = -1;
    int segCount = 0;

    tthread::lock_guard<tthread::mutex> guard(entryMutex);
    for (std::map<uint32_t, std::deque<playListEntries> >::iterator pListIt = listEntries.begin();
         pListIt != listEntries.end(); pListIt++){
      segCount += pListIt->second.size();
      if (pListIt->second.size()){
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
