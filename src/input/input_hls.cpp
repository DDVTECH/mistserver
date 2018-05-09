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

#define SEM_TS_CLAIM "/MstTSIN%s"

namespace Mist{

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

  Playlist::Playlist(const std::string &uriSrc){
    INFO_MSG("Adding variant playlist: %s", uriSrc.c_str());
    segDL.progressCallback = callbackFunc;
    segDL.dataTimeout = 5;
    segDL.retryCount = 5;
    plsDL.progressCallback = callbackFunc;
    plsDL.dataTimeout = 15;
    plsDL.retryCount = 8;
    lastFileIndex = 0;
    waitTime = 2;
    playlistEnd = false;
    noChangeCount = 0;
    lastTimestamp = 0;
    uri = uriSrc;
    root = HTTP::URL(uri);
    startTime = Util::bootSecs();
    if (uri.size()){reload();}
  }

  /// Returns true if packetPtr is at the end of the current segment.
  bool Playlist::atEnd() const{
    return (packetPtr - segDL.const_data().data() + 188) > segDL.const_data().size();
  }

  /// Returns true if there is no protocol defined in the playlist root URL.
  bool Playlist::isUrl() const{return root.protocol.size();}

  /// Loads the given segment URL into the segment buffer.
  bool Playlist::loadSegment(const HTTP::URL &uri){
    if (!segDL.get(uri)){
      FAIL_MSG("failed download: %s", uri.getUrl().c_str());
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

    // check first byte = 0x47. begin of ts file, then check if it is a multiple of 188bytes
    if (segDL.data().data()[0] == 0x47){
      if (segDL.data().size() % 188){
        FAIL_MSG("Expected a multiple of 188 bytes, received %d bytes. url: %s",
                 segDL.data().size(), uri.getUrl().c_str());
        return false;
      }
    }else if (segDL.data().data()[5] == 0x47){
      if (segDL.data().size() % 192){
        FAIL_MSG("Expected a multiple of 192 bytes, received %d bytes. url: %s",
                 segDL.data().size(), uri.getUrl().c_str());
        return false;
      }
    }

    packetPtr = segDL.data().data();
    return true;
  }

  /// Handles both initial load and future reloads.
  /// Returns how many segments were added to the internal segment list.
  bool Playlist::reload(){
    uint64_t fileNo = 0;
    std::string line;
    std::string key;
    std::string val;
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

        if (key == "TARGETDURATION"){
          waitTime = atoi(val.c_str()) / 2;
          if (waitTime < 2){waitTime = 2;}
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
        addEntry(filename, f, totalBytes);
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
  void Playlist::addEntry(const std::string &filename, float duration, uint64_t &totalBytes){
    if (!isSupportedFile(filename)){
      WARN_MSG("Ignoring unsupported file: %s", filename.c_str());
      return;
    }

    MEDIUM_MSG("Adding segment (%d): %s", lastFileIndex, filename.c_str());

    playListEntries entry;
    entry.filename = filename;
    cleanLine(entry.filename);
    entry.bytePos = totalBytes;
    entry.duration = duration;

    if (!isUrl()){
      std::ifstream fileSource;
      std::string test = root.link(entry.filename).getFilePath();
      fileSource.open(test.c_str(), std::ios::ate | std::ios::binary);
      if (!fileSource.good()){WARN_MSG("file: %s, error: %s", test.c_str(), strerror(errno));}
      totalBytes += fileSource.tellg();
    }

    entry.timestamp = lastTimestamp + startTime;
    lastTimestamp += duration;
    entries.push_back(entry);
  }

  /// Constructor of HLS Input
  inputHLS::inputHLS(Util::Config *cfg) : Input(cfg){
    self = this;
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

    capa["priority"] = 9ll;
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
    if (!initPlaylist(config->getString("input"))){return false;}
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
    bool hasHeader = false;
    if (!hasHeader){myMeta = DTSC::Meta();}
    VERYHIGH_MSG("parsestream");
    TS::Packet packet; // to analyse and extract data
    int counter = 1;
    int packetId = 0;

    char *data;
    unsigned int dataLen;
    bool keepReading = false;

    for (std::vector<Playlist>::iterator pListIt = playlists.begin(); pListIt != playlists.end();
         pListIt++){
      if (!pListIt->entries.size()){continue;}
      std::deque<playListEntries>::iterator entryIt = pListIt->entries.begin();

      tsStream.clear();
      uint64_t lastBpos = entryIt->bytePos;

      if (pListIt->isUrl()){
        bool ret = false;
        continueNegotiate();
        nProxy.userClient.keepAlive();
        ret = pListIt->loadSegment(pListIt->root.link(entryIt->filename));
        keepReading = packet.FromPointer(pListIt->packetPtr);
        pListIt->packetPtr += 188;
      }else{
        in.open(pListIt->root.link(entryIt->filename).getUrl().c_str());
        if (!in.good()){
          FAIL_MSG("Could not open segment (%s): %s", strerror(errno),
                   pListIt->root.link(entryIt->filename).getFilePath().c_str());
          continue; // skip to the next one
        }
        keepReading = packet.FromStream(in);
      }

      while (keepReading){
        tsStream.parse(packet, lastBpos);
        if (pListIt->isUrl()){
          lastBpos = entryIt->bytePos + pListIt->segDL.data().size();
        }else{
          lastBpos = entryIt->bytePos + in.tellg();
        }

        while (tsStream.hasPacketOnEachTrack()){
          DTSC::Packet headerPack;
          tsStream.getEarliestPacket(headerPack);
          int tmpTrackId = headerPack.getTrackId();
          packetId = pidMapping[(pListIt->id << 16) + tmpTrackId];

          if (packetId == 0){
            pidMapping[(pListIt->id << 16) + headerPack.getTrackId()] = counter;
            pidMappingR[counter] = (pListIt->id << 16) + headerPack.getTrackId();
            packetId = counter;
            HIGH_MSG("Added file %s, trackid: %d, mapped to: %d",
                     pListIt->root.link(entryIt->filename).getUrl().c_str(),
                     headerPack.getTrackId(), counter);
            counter++;
          }

          myMeta.live = false;
          myMeta.vod = true;

          if (!hasHeader &&
              (!myMeta.tracks.count(packetId) || !myMeta.tracks[packetId].codec.size())){
            tsStream.initializeMetadata(myMeta, tmpTrackId, packetId);
            myMeta.tracks[packetId].minKeepAway = pListIt->waitTime * 2000;
            VERYHIGH_MSG("setting minKeepAway = %d for track: %d",
                         myMeta.tracks[packetId].minKeepAway, packetId);
          }
        }

        if (pListIt->isUrl()){
          keepReading = !pListIt->atEnd();
          if (keepReading){
            packet.FromPointer(pListIt->packetPtr);
            pListIt->packetPtr += 188;
          }
        }else{
          keepReading = packet.FromStream(in);
        }
      }

      in.close();
    }
    tsStream.clear();

    if (hasHeader){return;}
    in.close();
  }

  bool inputHLS::readHeader(){
    if (playlists.size() && playlists[0].playlistType == LIVE){return true;}

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
    int packetId = 0;

    char *data;
    unsigned int dataLen;

    for (std::vector<Playlist>::iterator pListIt = playlists.begin(); pListIt != playlists.end();
         pListIt++){
      tsStream.clear();
      uint32_t entId = 0;

      for (std::deque<playListEntries>::iterator entryIt = pListIt->entries.begin();
           entryIt != pListIt->entries.end(); entryIt++){
        tsStream.partialClear();
        endOfFile = false;

        if (pListIt->isUrl()){
          pListIt->loadSegment(pListIt->root.link(entryIt->filename));
          endOfFile = !pListIt->atEnd();
          if (!endOfFile){packet.FromPointer(pListIt->packetPtr);}
          pListIt->packetPtr += 188;
        }else{
          in.close();
          in.open(pListIt->root.link(entryIt->filename).getFilePath().c_str());
          if (!in.good()){
            FAIL_MSG("Could not open segment (%s): %s", strerror(errno),
                     pListIt->root.link(entryIt->filename).getFilePath().c_str());
            continue; // skip to the next one
          }
          packet.FromStream(in);
          endOfFile = in.eof();
        }

        entId++;
        uint64_t lastBpos = entryIt->bytePos;
        while (!endOfFile){
          tsStream.parse(packet, lastBpos);

          if (pListIt->isUrl()){
            lastBpos = entryIt->bytePos + pListIt->segDL.data().size();
          }else{
            lastBpos = entryIt->bytePos + in.tellg();
          }

          while (tsStream.hasPacketOnEachTrack()){
            DTSC::Packet headerPack;
            tsStream.getEarliestPacket(headerPack);

            int tmpTrackId = headerPack.getTrackId();
            packetId = pidMapping[(pListIt->id << 16) + tmpTrackId];

            if (packetId == 0){
              pidMapping[(pListIt->id << 16) + headerPack.getTrackId()] = counter;
              pidMappingR[counter] = (pListIt->id << 16) + headerPack.getTrackId();
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

          if (pListIt->isUrl()){
            endOfFile = pListIt->atEnd();
            if (!endOfFile){
              packet.FromPointer(pListIt->packetPtr);
              pListIt->packetPtr += 188;
            }
          }else{
            packet.FromStream(in);
            endOfFile = in.eof();
          }
        }
        // get last packets
        tsStream.finish();
        DTSC::Packet headerPack;
        tsStream.getEarliestPacket(headerPack);
        while (headerPack){
          int tmpTrackId = headerPack.getTrackId();
          packetId = pidMapping[(pListIt->id << 16) + tmpTrackId];

          if (packetId == 0){
            pidMapping[(pListIt->id << 16) + headerPack.getTrackId()] = counter;
            pidMappingR[counter] = (pListIt->id << 16) + headerPack.getTrackId();
            packetId = counter;
            INFO_MSG("Added file %s, trackid: %d, mapped to: %d",
                     pListIt->root.link(entryIt->filename).getUrl().c_str(),
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

        if (!pListIt->isUrl()){in.close();}

        if (hasHeader){break;}
      }
    }

    if (hasHeader || (playlists.size() && playlists[0].isUrl())){return true;}

    INFO_MSG("write header file...");
    std::ofstream oFile((config->getString("input") + ".dtsh").c_str());

    oFile << myMeta.toJSON().toNetPacked();
    oFile.close();
    in.close();

    return true;
  }

  bool inputHLS::needsLock(){
    if (playlists.size() && playlists[0].isUrl()){return false;}
    return (playlists.size() <= currentPlaylist) ||
           !(playlists[currentPlaylist].playlistType == LIVE);
  }

  bool inputHLS::openStreamSource(){return true;}

  int inputHLS::getFirstPlaylistToReload(){
    int plsNum = 0;
    int bestPls = 0;
    uint64_t earRld = 0;
    for (std::vector<Playlist>::iterator it = playlists.begin(); it != playlists.end(); ++it){
      if (!plsNum || it->reloadNext < earRld){
        bestPls = plsNum;
        earRld = it->reloadNext;
      }
      ++plsNum;
    }
    return bestPls;
  }

  void inputHLS::getNext(bool smart){
    currentPlaylist = firstSegment();
    INSANE_MSG("Getting next");
    uint32_t tid = 0;
    bool endOfFile = false;
    if (selectedTracks.size()){tid = *selectedTracks.begin();}
    thisPacket.null();
    while (config->is_active && (needsLock() || nProxy.userClient.isAlive())){
      int oldPlaylist = currentPlaylist;
      currentPlaylist = firstSegment();

      // If we have a new playlist, print that info.
      if (currentPlaylist >= 0 && oldPlaylist != currentPlaylist){
        MEDIUM_MSG("Switched to playlist %d", currentPlaylist);
      }

      // No segments? Wait until next playlist reloading time.
      if (currentPlaylist < 0){
        int a = getFirstPlaylistToReload();
        MEDIUM_MSG("Waiting for %d seconds until next playlist reload...",
                   playlists[a].reloadNext - Util::bootSecs());
        while (Util::bootSecs() < playlists[a].reloadNext &&
               (needsLock() || nProxy.userClient.isAlive())){
          Util::wait(1000);
          continueNegotiate();
          nProxy.userClient.keepAlive();
        }
        MEDIUM_MSG("Reloading playlist %d", a);
        playlists[a].reload();
        currentPlaylist = firstSegment();
        // Continue regular parsing, in case we need another reload.
        currentPlaylist = oldPlaylist;
        continue;
      }

      // Check if we have a packet
      bool hasPacket = false;
      if (playlists[currentPlaylist].playlistType == LIVE){
        hasPacket = tsStream.hasPacketOnEachTrack() || (endOfFile && tsStream.hasPacket());
      }else{
        hasPacket = tsStream.hasPacket(getMappedTrackId(tid));
      }

      // Yes? Excellent! Read and return it.
      if (hasPacket){
        // Read
        if (playlists[currentPlaylist].playlistType == LIVE){
          tsStream.getEarliestPacket(thisPacket);
          tid = getOriginalTrackId(currentPlaylist, thisPacket.getTrackId());
        }else{
          tsStream.getPacket(getMappedTrackId(tid), thisPacket);
        }
        if (!thisPacket){
          FAIL_MSG("Could not getNext TS packet!");
        }else{
          // overwrite trackId on success
          Bit::htobl(thisPacket.getData() + 8, tid);
        }
        return; // Success!
      }

      // No? Let's read some more data and check again.
      if (playlists[currentPlaylist].isUrl()){
        if (!playlists[currentPlaylist].atEnd()){
          tsBuf.FromPointer(playlists[currentPlaylist].packetPtr);
          playlists[currentPlaylist].packetPtr += 188;
          tsStream.parse(tsBuf, 0);
          continue; // check again
        }
      }else{
        if (in.good()){
          tsBuf.FromStream(in);
          tsStream.parse(tsBuf, 0);
          continue; // check again
        }
      }

      // Okay, reading more is not possible. Let's call finish() and check again.
      if (!endOfFile){
        endOfFile = true; // we reached the end of file
        tsStream.finish();
        MEDIUM_MSG("Finishing reading TS segment");
        continue; // Check again!
      }

      // No? Then we try to read the next file.

      // First we handle live playlist reloads, if needed
      if (playlists[currentPlaylist].playlistType == LIVE){
        // Reload the first playlist that needs it, if the time is right
        int a = getFirstPlaylistToReload();
        if (playlists[a].reloadNext <= Util::bootSecs()){
          MEDIUM_MSG("Reloading playlist %d", a);
          playlists[a].reload();
          continue;
        }
      }

      // Now that we know our playlist is up-to-date, actually try to read the file.
      MEDIUM_MSG("Moving on to next TS segment");
      if (readNextFile()){
        MEDIUM_MSG("Next segment read successfully");
        endOfFile = false; // no longer at end of file
        // Prevent timeouts, we may have just finished a download after all.
        if (playlists[currentPlaylist].playlistType == LIVE){
          continueNegotiate();
          nProxy.userClient.keepAlive();
        }
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
    if (playlists[currentPlaylist].isUrl()){
      size_t bpos;
      TS::Packet tsBuffer;
      const char *tmpPtr = playlists[currentPlaylist].segDL.data().data();

      while (!tsStream.hasPacketOnEachTrack() &&
             (tmpPtr - playlists[currentPlaylist].segDL.data().data() + 188 <=
              playlists[currentPlaylist].segDL.data().size())){
        tsBuffer.FromPointer(tmpPtr);
        tsStream.parse(tsBuffer, 0);
        tmpPtr += 188;
      }
      tsStream.partialClear();

    }else{
      size_t bpos = in.tellg();
      in.seekg(0, in.beg);
      TS::Packet tsBuffer;
      while (!tsStream.hasPacketOnEachTrack() && tsBuffer.FromStream(in)){
        tsStream.parse(tsBuffer, 0);
      }
      tsStream.partialClear();
      in.clear();
      in.seekg(bpos, in.beg);
    }
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

    Playlist &curPlaylist = playlists[currentPlaylist];
    playListEntries &entry = curPlaylist.entries.at(currentIndex);
    if (curPlaylist.isUrl()){
      curPlaylist.loadSegment(curPlaylist.root.link(entry.filename));
    }else{
      in.close();
      in.open(curPlaylist.root.link(entry.filename).getFilePath().c_str());
      MEDIUM_MSG("Opening segment: %s",
                 curPlaylist.root.link(entry.filename).getFilePath().c_str());
      if (!in.good()){
        FAIL_MSG("Could not open segment (%s): %s", strerror(errno),
                 curPlaylist.root.link(entry.filename).getUrl().c_str());
      }
    }
    readPMT();
  }

  int inputHLS::getEntryId(int playlistId, uint64_t bytePos){
    if (bytePos == 0){return 0;}

    for (int i = 0; i < playlists[playlistId].entries.size(); i++){
      if (playlists[playlistId].entries.at(i).bytePos > bytePos){return i - 1;}
    }

    return playlists[playlistId].entries.size() - 1;
  }

  int inputHLS::getOriginalTrackId(int playlistId, int id){
    return pidMapping[(playlistId << 16) + id];
  }

  int inputHLS::getMappedTrackId(int id){return (pidMappingR[id] & 0xFFFF);}

  int inputHLS::getMappedTrackPlaylist(int id){return (pidMappingR[id] >> 16);}

  /// Parses the main playlist, possibly containing variants.
  bool inputHLS::initPlaylist(const std::string &uri){
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

          ret = readPlaylist(playlistRootPath.link(line));
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
            ret = readPlaylist(playlistRootPath.link(mediafile));
          }
        }

      }else if (line.compare(0, 7, "#EXTINF") == 0){
        // current file is not a variant playlist, but regular playlist.
        ret = readPlaylist(uri);
        break;
      }else{
        // ignore wrong lines
        VERYHIGH_MSG("ignore wrong line: %s", line.c_str());
      }
    }

    if (!isUrl){fileSource.close();}

    return ret;
  }

  /// Function for reading every playlist.
  bool inputHLS::readPlaylist(const HTTP::URL &uri){
    Playlist p(uri.protocol.size() ? uri.getUrl() : uri.getFilePath());
    p.id = playlists.size();
    // set size of reloadNext to playlist count with default value 0
    playlists.push_back(p);
    return true;
  }

  /// Read next .ts file from the playlist. (from the list of entries which needs
  /// to be processed)
  bool inputHLS::readNextFile(){
    tsStream.clear();
    Playlist &curList = playlists[currentPlaylist];

    if (!curList.entries.size()){
      WARN_MSG("no entries found in playlist: %d!", currentPlaylist);
      return false;
    }

    // URL-based
    if (curList.isUrl()){
      if (!curList.loadSegment(curList.root.link(curList.entries.front().filename))){
        ERROR_MSG("Could not download segment: %s",
                  curList.root.link(curList.entries.front().filename).getUrl().c_str());
        curList.entries.pop_front();
        return readNextFile(); // Attempt to read another, if possible.
      }
      curList.entries.pop_front();
      return true;
    }

    // file-based, live
    if (curList.playlistType == LIVE){
      in.close();
      std::string filepath =
          curList.root.link(curList.entries.at(currentIndex).filename).getFilePath();
      curList.entries.pop_front(); // remove the item from the playlist
      in.open(filepath.c_str());
      if (in.good()){return true;}
      FAIL_MSG("Could not open segment (%s): %s", strerror(errno), filepath.c_str());
      return readNextFile(); // Attempt to read another, if possible.
    }

    // file-based, VoD
    ++currentIndex;
    if (curList.entries.size() <= currentIndex){
      HIGH_MSG("end of playlist reached (%u of %u)!", currentIndex, curList.entries.size());
      return false;
    }
    in.close();
    std::string filepath =
        curList.root.link(curList.entries.at(currentIndex).filename).getFilePath();
    in.open(filepath.c_str());
    if (in.good()){
      readPMT();
      return true;
    }
    FAIL_MSG("Could not open segment (%s): %s", strerror(errno), filepath.c_str());
    return readNextFile();
  }

  /// return the playlist id from which we need to read the first upcoming segment
  /// by timestamp.
  /// this will keep the playlists in sync while reading segments.
  int inputHLS::firstSegment(){
    // Only one selected? Immediately return the right playlist.
    if (selectedTracks.size() == 1){return getMappedTrackPlaylist(*selectedTracks.begin());}
    uint64_t firstTimeStamp = 0;
    int tmpId = -1;

    for (std::vector<Playlist>::iterator pListIt = playlists.begin(); pListIt != playlists.end();
         pListIt++){
      if (pListIt->entries.size()){
        if (pListIt->entries.front().timestamp < firstTimeStamp || tmpId < 0){
          firstTimeStamp = pListIt->entries.front().timestamp;
          tmpId = pListIt->id;
        }
      }
    }
    return tmpId;
  }

}// namespace Mist

