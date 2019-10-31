#include "input_hls.h"
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
#include "mbedtls/aes.h"

#define SEM_TS_CLAIM "/MstTSIN%s"

namespace Mist{

  ///Mutex for accesses to listEntries
  tthread::mutex entryMutex;

  static unsigned int plsTotalCount = 0;///Total playlists active
  static unsigned int plsInitCount = 0;///Count of playlists fully inited

  bool streamIsLive;
  uint32_t globalWaitTime;
  std::map<uint32_t, std::deque<playListEntries> > listEntries;

  // These are used in the HTTP::Downloader callback, to prevent timeouts when downloading
  // segments/playlists.
  inputHLS *self = 0;
  bool callbackFunc(){return self->callback();}

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
  void playlistRunner(void * ptr){
    if (!ptr){return;}//abort if we received a null pointer - something is seriously wrong
    bool initOnly = false;
    if (((char*)ptr)[0] == ';'){initOnly = true;}

    Playlist pls(initOnly?((char*)ptr)+1:(char*)ptr);
    plsTotalCount++;
    //signal that we have now copied the URL and no longer need it
    ((char*)ptr)[0] = 0;

    if (!pls.uri.size()){
      FAIL_MSG("Variant playlist URL is empty, aborting update thread.");
      return;
    }

    pls.reload();
    plsInitCount++;
    if (initOnly){return;}//Exit because init-only mode

    while (self->config->is_active){
      //If the timer has not expired yet, sleep up to a second. Otherwise, reload.
      /// \TODO Sleep longer if that makes sense?
      if (pls.reloadNext > Util::bootSecs()){
        Util::sleep(1000);
      }else{
        pls.reload();
      }
    }
    INFO_MSG("Downloader thread for '%s' exiting", pls.uri.c_str());
  }

  SegmentDownloader::SegmentDownloader(){
    segDL.progressCallback = callbackFunc;
    segDL.dataTimeout = 5;
    segDL.retryCount = 5;
  }

  Playlist::Playlist(const std::string &uriSrc){
    id = 0;//to be set later
    INFO_MSG("Adding variant playlist: %s", uriSrc.c_str());
    plsDL.dataTimeout = 15;
    plsDL.retryCount = 8;
    lastFileIndex = 0;
    waitTime = 2;
    playlistEnd = false;
    noChangeCount = 0;
    lastTimestamp = 0;
    uri = uriSrc;
    root = HTTP::URL(uri);
    memset(keyAES, 0, 16);
    startTime = Util::bootSecs();
    reloadNext = 0;
  }
  
 void parseKey(std::string key, char * newKey, unsigned int len){
    memset(newKey, 0, len);
    for (size_t i = 0; i < key.size() && i < (len << 1); ++i){
      char c = key[i];
      newKey[i>>1] |= ((c&15) + (((c&64)>>6) | ((c&64)>>3))) << ((~i&1) << 2);
    }
  }

  void flipKey(char * d){
    for(size_t i = 0; i< 8; i++){
      char tmp = d[i];
      d[i] = d[15-i];
      d[15-i]=tmp;
    }

  }

static std::string printhex(const char * data, size_t len)
{
    static const char* const lut = "0123456789ABCDEF";

    std::string output;
    output.reserve(2 * len);
    for (size_t i = 0; i < len; ++i)
    {
        const unsigned char c = data[i];
        output.push_back(lut[c >> 4]);
        output.push_back(lut[c & 15]);
    }
    return output;
}
  /// Returns true if packetPtr is at the end of the current segment.
  bool SegmentDownloader::atEnd() const{
    return (packetPtr - segDL.const_data().data() + 188) > segDL.const_data().size();
  }

  /// Returns true if there is no protocol defined in the playlist root URL.
  bool Playlist::isUrl() const{return root.protocol.size();}

  /// Loads the given segment URL into the segment buffer.
  bool SegmentDownloader::loadSegment(const playListEntries & entry){
    std::string hexKey = printhex(entry.keyAES,16);
    std::string hexIvec = printhex(entry.ivec, 16);

    MEDIUM_MSG("Loading segment: %s, key: %s, ivec: %s", entry.filename.c_str(), hexKey.c_str(), hexIvec.c_str());
    if (!segDL.get(entry.filename)){
      FAIL_MSG("failed download: %s", entry.filename.c_str());
      return false;
    }

    if (!segDL.isOk()){
      FAIL_MSG("HTTP response not OK!. statuscode: %d, statustext: %s", segDL.getStatusCode(),
               segDL.getStatusText().c_str());
      return false;
    }

    if (segDL.getHeader("Content-Length") != ""){
      if (segDL.data().size() != atoi(segDL.getHeader("Content-Length").c_str())){
        FAIL_MSG("Expected %s bytes of data, but only received %lu.",
                 segDL.getHeader("Content-Length").c_str(), segDL.data().size());
        return false;
      }
    }

    //If we have a non-null key, decrypt
    if (entry.keyAES[ 0] != 0 || entry.keyAES[ 1] != 0 || entry.keyAES[ 2] != 0 || entry.keyAES[ 3] != 0 || \
        entry.keyAES[ 4] != 0 || entry.keyAES[ 5] != 0 || entry.keyAES[ 6] != 0 || entry.keyAES[ 7] != 0 || \
        entry.keyAES[ 8] != 0 || entry.keyAES[ 9] != 0 || entry.keyAES[10] != 0 || entry.keyAES[11] != 0 || \
        entry.keyAES[12] != 0 || entry.keyAES[13] != 0 || entry.keyAES[14] != 0 || entry.keyAES[15] != 0){
      //Setup AES context
      mbedtls_aes_context aes;
      //Load key for decryption
      mbedtls_aes_setkey_dec(&aes, (const unsigned char*)entry.keyAES, 128);
      //Allocate a pointer for writing the decrypted data to
      static Util::ResizeablePointer outdata;
      outdata.allocate(segDL.data().size());
      //Actually decrypt the data
      unsigned char tmpIvec[16];
      memcpy(tmpIvec, entry.ivec, 16);


      mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, segDL.data().size(), tmpIvec, (const unsigned char*)segDL.data().data(), (unsigned char*)(char*)outdata);
      //Data is now still padded, the padding consists of X bytes of padding, all containing the raw value X.
      //Since padding is mandatory, we can simply read the last byte and remove X bytes from the length.
      if (segDL.data().size() <= outdata[segDL.data().size()-1]){
        FAIL_MSG("Encryption padding is >= entire segment. Considering download failed.");
        return false;
      }
      size_t newSize = segDL.data().size() - outdata[segDL.data().size()-1];
      //Finally, overwrite the original data buffer with the new one
      segDL.data().assign(outdata, newSize);
    }

    // check first byte = 0x47. begin of ts file, then check if it is a multiple of 188bytes
    if (segDL.data().data()[0] == 0x47){
      if (segDL.data().size() % 188){
        FAIL_MSG("Expected a multiple of 188 bytes, received %d bytes. url: %s",
                 segDL.data().size(), entry.filename.c_str());
        return false;
      }
    }else if (segDL.data().data()[5] == 0x47){
      if (segDL.data().size() % 192){
        FAIL_MSG("Expected a multiple of 192 bytes, received %d bytes. url: %s",
                 segDL.data().size(), entry.filename.c_str());
        return false;
      }
    }else{
      FAIL_MSG("Segment does not appear to contain TS data. Considering download failed.");
      return false;
    }

    packetPtr = segDL.data().data();
    HIGH_MSG("Segment download complete and passed sanity checks");
    return true;
  }


  /// Handles both initial load and future reloads.
  /// Returns how many segments were added to the internal segment list.
  bool Playlist::reload(){
    uint64_t fileNo = 0;
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
        FAIL_MSG("Could not download playlist, aborting.");
        return false;
      }
      urlSource.str(plsDL.data());
    }else{
      fileSource.open(uri.c_str());
      if (!fileSource.good()){
        FAIL_MSG("Could not open playlist (%s): %s", strerror(errno), uri.c_str());
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

        if(key == "KEY" ){
          size_t tmpPos = val.find("METHOD=");
          size_t tmpPos2 = val.substr(tmpPos).find(",");
          keyMethod = val.substr(tmpPos +7, tmpPos2-tmpPos-7);

          tmpPos = val.find("URI=\"");
          tmpPos2 = val.substr(tmpPos+5).find("\"");
          keyUri = val.substr(tmpPos + 5, tmpPos2);

          tmpPos = val.find("IV=");
          keyIV = val.substr(tmpPos+5, 32);

          //when key not found, download and store it in the map
          if (!keys.count(keyUri)) {
            HTTP::Downloader keyDL;
            if (!keyDL.get(root.link(keyUri)) || !keyDL.isOk()){
              FAIL_MSG("Could not retrieve decryption key from '%s'", root.link(keyUri).getUrl().c_str());
              continue;
            }
            keys.insert(std::pair<std::string, std::string>(keyUri, keyDL.data()));
          }
        }

        if (key == "TARGETDURATION"){
          waitTime = atoi(val.c_str()) / 2;
          if (waitTime < 5){waitTime = 5;}
        }

        if (key == "MEDIA-SEQUENCE"){fileNo = atoll(val.c_str());}

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
        parseKey(keyIV, ivec, 16);
        addEntry(filename, f, totalBytes,keys[keyUri],std::string(ivec,16));
        lastFileIndex = fileNo + 1;
        ++count;
      }
      ++fileNo;
    }

    // VOD over HTTP needs to be processed as LIVE.
    if (isUrl()){
      playlistType = LIVE;
    }else{
      fileSource.close();
    }
    //Set the global live/vod bool to live if this playlist looks like a live playlist
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
  void Playlist::addEntry(const std::string &filename, float duration, uint64_t &totalBytes, const std::string &key, const std::string &iv){
    if (!isSupportedFile(filename)){
      WARN_MSG("Ignoring unsupported file: %s", filename.c_str());
      return;
    }


    playListEntries entry;
    entry.filename = filename;
    cleanLine(entry.filename);
    entry.bytePos = totalBytes;
    entry.duration = duration;

    if(key.size() && iv.size()){
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
      //Set a playlist ID if we haven't assigned one yet.
      //Note: This method requires never removing playlists, only adding.
      //The mutex assures we have a unique count/number.
      if (!id){id = listEntries.size()+1;}
      listEntries[id].push_back(entry);
      MEDIUM_MSG("Added segment to variant %" PRIu32 " (#%d, now %d queued): %s", id, lastFileIndex, listEntries[id].size(), filename.c_str());
    }
  }

  /// Constructor of HLS Input
  inputHLS::inputHLS(Util::Config *cfg) : Input(cfg){
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
    if (mainPls.getExt().substr(0, 3) != "m3u" && mainPls.protocol.find("hls") == std::string::npos){return false;}
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
    myMeta.live = false;
    myMeta.vod = true;
    INFO_MSG("Parsing live stream to create header...");
    TS::Packet packet; // to analyse and extract data
    int counter = 1;

    char *data;
    unsigned int dataLen;
    bool keepReading = false;

    tthread::lock_guard<tthread::mutex> guard(entryMutex);
    for (std::map<uint32_t, std::deque<playListEntries> >::iterator pListIt = listEntries.begin(); pListIt != listEntries.end();
         pListIt++){
      //Skip empty playlists
      if (!pListIt->second.size()){continue;}
      int preCounter = counter;
      tsStream.clear();


      for (std::deque<playListEntries>::iterator entryIt = pListIt->second.begin(); entryIt != pListIt->second.end(); ++entryIt){
        uint64_t lastBpos = entryIt->bytePos;
        nProxy.userClient.keepAlive();
        if (!segDowner.loadSegment(*entryIt)){
          WARN_MSG("Skipping segment that could not be loaded in an attempt to recover");
          tsStream.clear();
          continue;
        }

        do{
          if (!packet.FromPointer(segDowner.packetPtr)){
            WARN_MSG("Could not load TS packet, aborting segment parse");
            tsStream.clear();
            break;//Abort load
          }
          tsStream.parse(packet, lastBpos);
          segDowner.packetPtr += 188;

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
                VERYHIGH_MSG("Added file %s, trackid: %d, mapped to: %d", entryIt->filename.c_str(), headerPack.getTrackId(), counter);
                counter++;
              }

              if ((!myMeta.tracks.count(packetId) || !myMeta.tracks[packetId].codec.size())){
                tsStream.initializeMetadata(myMeta, tmpTrackId, packetId);
                myMeta.tracks[packetId].minKeepAway = globalWaitTime * 2000;
                VERYHIGH_MSG("setting minKeepAway = %d for track: %d",
                             myMeta.tracks[packetId].minKeepAway, packetId);
              }
            }
            break;//we have all tracks discovered, next playlist!
          }
        }while(!segDowner.atEnd());
        if (preCounter < counter){break;}//We're done reading this playlist!
      }
    }
    tsStream.clear();
    currentPlaylist = 0;
    segDowner.segDL.data().clear();//make sure we have nothing left over
    INFO_MSG("header complete, beginning live ingest of %d tracks", counter-1);
  }

  bool inputHLS::readHeader(){
    if (streamIsLive){return true;}

    std::istringstream urlSource;
    std::ifstream fileSource;

    bool endOfFile = false;
    bool hasHeader = false;

    // See whether a separate header file exists.
    DTSC::File tmp(config->getString("input") + ".dtsh");
    if (tmp){
      myMeta = tmp.getMeta();
      if (myMeta){hasHeader = true;}
    }

    if (!hasHeader){myMeta = DTSC::Meta();}

    TS::Packet packet; // to analyse and extract data

    int counter = 1;

    char *data;
    size_t dataLen;

    tthread::lock_guard<tthread::mutex> guard(entryMutex);
    for (std::map<uint32_t, std::deque<playListEntries> >::iterator pListIt = listEntries.begin(); pListIt != listEntries.end();
         pListIt++){
      tsStream.clear();
      uint32_t entId = 0;

      for (std::deque<playListEntries>::iterator entryIt = pListIt->second.begin();
           entryIt != pListIt->second.end(); entryIt++){
        tsStream.partialClear();
        endOfFile = false;

        segDowner.loadSegment(*entryIt);
        endOfFile = !segDowner.atEnd();
        if (!endOfFile){packet.FromPointer(segDowner.packetPtr);}
        segDowner.packetPtr += 188;

        entId++;
        uint64_t lastBpos = entryIt->bytePos;
        while (!endOfFile){
          tsStream.parse(packet, lastBpos);

          //if (pListIt->isUrl()){
            lastBpos = entryIt->bytePos + segDowner.segDL.data().size();
          //}else{
          //  lastBpos = entryIt->bytePos + in.tellg();
          //}

          while (tsStream.hasPacketOnEachTrack()){
            DTSC::Packet headerPack;
            tsStream.getEarliestPacket(headerPack);

            int tmpTrackId = headerPack.getTrackId();
            uint64_t packetId = pidMapping[(((uint64_t)pListIt->first) << 32) + tmpTrackId];

            if (packetId == 0){
              pidMapping[(((uint64_t)pListIt->first) << 32) + headerPack.getTrackId()] = counter;
              pidMappingR[counter] = (((uint64_t)pListIt->first) << 32) + headerPack.getTrackId();
              packetId = counter;
              counter++;
            }

            if (!hasHeader &&
                (!myMeta.tracks.count(packetId) || !myMeta.tracks[packetId].codec.size())){
              tsStream.initializeMetadata(myMeta, tmpTrackId, packetId);
            }

            if (!hasHeader){
              headerPack.getString("data", data, dataLen);
              uint64_t pBPos = headerPack.getInt("bpos");

              // keyframe data exists, so always add 19 bytes keyframedata.
              long long packOffset =
                  headerPack.hasMember("offset") ? headerPack.getInt("offset") : 0;
              long long packSendSize =
                  24 + (packOffset ? 17 : 0) + (entId >= 0 ? 15 : 0) + 19 + dataLen + 11;
              myMeta.update(headerPack.getTime(), packOffset, packetId, dataLen, entId,
                            headerPack.hasMember("keyframe"), packSendSize);
            }
          }

          //if (pListIt->isUrl()){
            endOfFile = segDowner.atEnd();
            if (!endOfFile){
              packet.FromPointer(segDowner.packetPtr);
              segDowner.packetPtr += 188;
            }
          //}else{
          //  packet.FromStream(in);
          //  endOfFile = in.eof();
          //}
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
            INFO_MSG("Added file %s, trackid: %d, mapped to: %d",
                     entryIt->filename.c_str(),
                     headerPack.getTrackId(), counter);
            counter++;
          }

          if (!hasHeader &&
              (!myMeta.tracks.count(packetId) || !myMeta.tracks[packetId].codec.size())){
            tsStream.initializeMetadata(myMeta, tmpTrackId, packetId);
          }

          if (!hasHeader){
            headerPack.getString("data", data, dataLen);
            uint64_t pBPos = headerPack.getInt("bpos");

            // keyframe data exists, so always add 19 bytes keyframedata.
            long long packOffset = headerPack.hasMember("offset") ? headerPack.getInt("offset") : 0;
            long long packSendSize =
                24 + (packOffset ? 17 : 0) + (entId >= 0 ? 15 : 0) + 19 + dataLen + 11;
            myMeta.update(headerPack.getTime(), packOffset, packetId, dataLen, entId,
                          headerPack.hasMember("keyframe"), packSendSize);
          }
          tsStream.getEarliestPacket(headerPack);
        }

        //if (!pListIt->isUrl()){in.close();}

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

  bool inputHLS::needsLock(){
    return !streamIsLive;
  }

  bool inputHLS::openStreamSource(){return true;}

  void inputHLS::getNext(bool smart){
    INSANE_MSG("Getting next");
    uint32_t tid = 0;
    static bool endOfFile = false;
    if (selectedTracks.size()){tid = *selectedTracks.begin();}
    thisPacket.null();
    while (config->is_active && (needsLock() || nProxy.userClient.isAlive())){

      // Check if we have a packet
      bool hasPacket = false;
      if (streamIsLive){
        hasPacket = tsStream.hasPacketOnEachTrack() || (endOfFile && tsStream.hasPacket());
      }else{
        hasPacket = tsStream.hasPacket(getMappedTrackId(tid));
      }

      // Yes? Excellent! Read and return it.
      if (hasPacket){
        // Read
        if (myMeta.live){
          tsStream.getEarliestPacket(thisPacket);
          tid = getOriginalTrackId(currentPlaylist, thisPacket.getTrackId());
        }else{
          tsStream.getPacket(getMappedTrackId(tid), thisPacket);
        }
        if (!thisPacket){
          FAIL_MSG("Could not getNext TS packet!");
        }else{
          DONTEVEN_MSG("Packet track %lu @ time %" PRIu64 " ms", tid, thisPacket.getTime());
          // overwrite trackId on success
          Bit::htobl(thisPacket.getData() + 8, tid);
        }
        return; // Success!
      }

      // No? Let's read some more data and check again.
      if (!segDowner.atEnd()){
        tsBuf.FromPointer(segDowner.packetPtr);
        segDowner.packetPtr += 188;
        tsStream.parse(tsBuf, 0);
        continue; // check again
      }

      // Okay, reading more is not possible. Let's call finish() and check again.
      if (!endOfFile){
        endOfFile = true; // we reached the end of file
        tsStream.finish();
        VERYHIGH_MSG("Finishing reading TS segment");
        continue; // Check again!
      }

      // No? Then we try to read the next file.
      //
      currentPlaylist = firstSegment();
      // No segments? Wait until next playlist reloading time.
      if (currentPlaylist < 0){
        VERYHIGH_MSG("Waiting for segments...");
        if (nProxy.userClient.isAlive()){nProxy.userClient.keepAlive();}
        Util::wait(500);
        continue;
      }

      // Now that we know our playlist is up-to-date, actually try to read the file.
      VERYHIGH_MSG("Moving on to next TS segment (variant %u)", currentPlaylist);
      if (readNextFile()){
        MEDIUM_MSG("Next segment read successfully");
        endOfFile = false; // no longer at end of file
        continue; // Success! Continue regular parsing.
      }

      // Nothing works!
      // HLS input will now quit trying to prevent severe mental depression.
      INFO_MSG("No packets can be read - exhausted all playlists");
      thisPacket.null();
      return;
    }
  }

  void inputHLS::readPMT(){
    HIGH_MSG("readPMT()");
    size_t bpos;
    TS::Packet tsBuffer;
    const char *tmpPtr = segDowner.segDL.data().data();

    while (!tsStream.hasPacketOnEachTrack() &&
           (tmpPtr - segDowner.segDL.data().data() + 188 <=
            segDowner.segDL.data().size())){
      tsBuffer.FromPointer(tmpPtr);
      tsStream.parse(tsBuffer, 0);
      tmpPtr += 188;
    }
    tsStream.partialClear();
  }

  // Note: bpos is overloaded here for playlist entry!
  void inputHLS::seek(int seekTime){
    tsStream.clear();
    int trackId = 0;

    unsigned long plistEntry = 0xFFFFFFFFull;
    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end();
         it++){
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
    INFO_MSG("Current index = %d", currentIndex);

    currentPlaylist = getMappedTrackPlaylist(trackId);

    {//Lock mutex for listEntries
      tthread::lock_guard<tthread::mutex> guard(entryMutex);
      std::deque<playListEntries> &curPlaylist = listEntries[currentPlaylist];
      playListEntries &entry = curPlaylist.at(currentIndex);
      segDowner.loadSegment(entry);
    }
    readPMT();
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
        ret = readPlaylist(uri, fullInit);
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
    std::string urlBuffer = (fullInit?"":";")+uri.getUrl();
    tthread::thread runList(playlistRunner, (void *)urlBuffer.data());
    runList.detach(); //Abandon the thread, it's now running independently
    uint32_t timeout = 0;
    while (urlBuffer.data()[0] && ++timeout < 100){
      Util::sleep(100);
    }
    if (timeout >= 100){
      WARN_MSG("Thread start timed out for: %s", urlBuffer.c_str());
    }
    return true;
  }

  /// Read next .ts file from the playlist. (from the list of entries which needs
  /// to be processed)
  bool inputHLS::readNextFile(){
    tsStream.clear();

    playListEntries ntry;
    //This scope limiter prevents the recursion down below from deadlocking us
    {
      tthread::lock_guard<tthread::mutex> guard(entryMutex);
      std::deque<playListEntries> &curList = listEntries[currentPlaylist];
      if (!curList.size()){
        WARN_MSG("no entries found in playlist: %d!", currentPlaylist);
        return false;
      }
      ntry = curList.front();
      curList.pop_front();
    }

    if (!segDowner.loadSegment(ntry)){
      ERROR_MSG("Could not download segment: %s", ntry.filename.c_str());
      return readNextFile(); // Attempt to read another, if possible.
    }
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
    for (std::map<uint32_t, std::deque<playListEntries> >::iterator pListIt = listEntries.begin(); pListIt != listEntries.end();
         pListIt++){
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

