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
  // remove trailing \r for windows generated playlist files
  int cleanLine(std::string &s){
    if (s.length() > 0 && s.at(s.length() - 1) == '\r'){s.erase(s.size() - 1);}
  }

  Playlist::Playlist(const std::string &uriSrc){
    lastFileIndex = 0;
    entryCount = 0;
    waitTime = 2;
    playlistEnd = false;
    noChangeCount = 0;
    initDone = false;
    lastTimestamp = 0;
    uri = uriSrc;
    startTime = Util::bootSecs();

    if (uri.size()){
      std::string line;
      std::string key;
      std::string val;
      int count = 0;
      uint64_t totalBytes = 0;
      uri_root = uri.substr(0, uri.rfind("/") + 1);
      playlistType = LIVE; // Temporary value
      INFO_MSG("readplaylist: %s", uri.c_str());

      std::istringstream urlSource;
      std::ifstream fileSource;

      if (isUrl()){
        loadURL(uri);
        urlSource.str(source);
      }else{
        fileSource.open(uri.c_str());
      }

      std::istream &input = (isUrl() ? (std::istream &)urlSource : (std::istream &)fileSource);
      std::getline(input, line);

      while (std::getline(input, line)){
        cleanLine(line);

        if (!line.empty()){
          if (line.compare(0, 7, "#EXT-X-") == 0){
            size_t pos = line.find(":");
            key = line.substr(7, pos - 7);
            val = line.c_str() + pos + 1;

            if (key == "VERSION"){version = atoi(val.c_str());}

            if (key == "TARGETDURATION"){waitTime = atoi(val.c_str());}

            if (key == "MEDIA-SEQUENCE"){
              media_sequence = atoi(val.c_str());
              lastFileIndex = media_sequence;
            }

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
          }else if (line.compare(0, 7, "#EXTINF") != 0){
            VERYHIGH_MSG("ignoring wrong line: %s.", line.c_str());
            continue;
          }
          float f = atof(line.c_str() + 8);
          std::string filename;
          std::getline(input, filename);
          addEntry(filename, f, totalBytes);
          count++;
        }
      }

      if (isUrl()){
        playlistType = LIVE; // VOD over HTTP needs to be processed as LIVE.
        fileSource.close();
      }
    }
    initDone = true;
  }

  bool Playlist::atEnd() const{return (packetPtr - source.data() + 188) > source.size();}

  bool Playlist::isUrl() const{
    return (uri_root.size() ? uri_root.find("http://") == 0 : uri.find("http://") == 0);
  }

  bool Playlist::loadURL(const std::string &loadUrl){
    HIGH_MSG("opening URL: %s", loadUrl.c_str());

    HTTP::URL url(loadUrl);
    if (url.protocol != "http"){
      FAIL_MSG("Protocol %s is not supported", url.protocol.c_str());
      return false;
    }

    Socket::Connection conn(url.host, url.getPort(), false);
    if (!conn){
      FAIL_MSG("Failed to reach %s on port %lu", url.host.c_str(), url.getPort());
      return false;
    }

    HTTP::Parser http;
    http.url = "/" + url.path;
    http.method = "GET";
    http.SetHeader("Host", url.host);
    http.SetHeader("X-MistServer", PACKAGE_VERSION);

    conn.SendNow(http.BuildRequest());
    http.Clean();

    uint64_t startTime = Util::epoch();
    source.clear();
    packetPtr = 0;
    while ((Util::epoch() - startTime < 10) && (conn || conn.Received().size())){
      if (conn.spool() || conn.Received().size()){
        if (http.Read(conn)){
          source = http.body;
          packetPtr = source.data();
          conn.close();
          return true;
        }
      }
    }

    FAIL_MSG("Failed to load %s: %s", loadUrl.c_str(), conn ? "timeout" : "connection closed");
    if (conn){conn.close();}
    return false;
  }

  /// Function for reloading the playlist in case of live streams.
  bool Playlist::reload(){
    int skip = lastFileIndex - media_sequence;
    bool ret = false;
    std::string line;
    std::string key;
    std::string val;
    int count = 0;

    uint64_t totalBytes = 0;

    std::istringstream urlSource;
    std::ifstream fileSource;

    if (isUrl()){
      loadURL(uri.c_str()); // get size only!
      urlSource.str(source);
    }else{
      fileSource.open(uri.c_str());
    }

    std::istream &input = (isUrl() ? (std::istream &)urlSource : (std::istream &)fileSource);
    std::getline(input, line);

    while (std::getline(input, line)){
      cleanLine(line);
      if (line.compare(0, 21, "#EXT-X-MEDIA-SEQUENCE") == 0){
        media_sequence = atoi(line.c_str() + line.find(":") + 1);
        skip = (lastFileIndex - media_sequence);
        continue;
      }
      if (line.compare(0, 7, "#EXTINF") != 0){continue;}
      float f = atof(line.c_str() + 8);
      // next line belongs to this item
      std::string filename;
      std::getline(input, filename);

      // check for already added segments
      if (skip){
        skip--;
      }else{
        cleanLine(filename);
        addEntry(filename, f, totalBytes);
        count++;
      }
    }

    if (!isUrl()){fileSource.close();}

    ret = (count > 0);

    if (ret){
      noChangeCount = 0;
    }else{
      ++noChangeCount;
      if (noChangeCount > 3){VERYHIGH_MSG("enough!");}
    }

    return ret;
  }

  /// function for adding segments to the playlist to be processed. used for VOD and live
  void Playlist::addEntry(const std::string &filename, float duration, uint64_t &totalBytes){
    playListEntries entry;
    entry.filename = filename;
    cleanLine(entry.filename);
    std::string test = uri_root + entry.filename;

    std::istringstream urlSource;
    std::ifstream fileSource;

    if (isUrl()){
      urlSource.str(source);
    }else{
      fileSource.open(test.c_str(), std::ios::ate | std::ios::binary);
      if ((fileSource.rdstate() & std::ifstream::failbit) != 0){
        WARN_MSG("file: %s, error: %s", test.c_str(), strerror(errno));
      }
    }

    entry.bytePos = totalBytes;
    entry.duration = duration;
    if (!isUrl()){totalBytes += fileSource.tellg();}

    if (initDone){
      lastTimestamp += duration;
      entry.timestamp = lastTimestamp + startTime;
      entry.wait = entryCount * duration;
    }else{
      entry.timestamp = 0; // read all segments immediatly at the beginning, then use delays
    }
    ++entryCount;
    entries.push_back(entry);
    ++lastFileIndex;
  }

  /// Constructor of HLS Input
  inputHLS::inputHLS(Util::Config *cfg) : Input(cfg){
    currentPlaylist = 0;

    capa["name"] = "HLS";
    capa["decs"] = "Enables HLS Input";
    capa["source_match"].append("/*.m3u8");
    capa["source_match"].append("http://*.m3u8");
    //These two can/may be set to always-on mode
    capa["always_match"].append("/*.m3u8");
    capa["always_match"].append("http://*.m3u8");

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
    if (config->getString("input") == "-"){return false;}

    if (!initPlaylist(config->getString("input"))){return false;}

    if (Util::Config::printDebugLevel >= DLVL_HIGH){
      for (std::vector<Playlist>::iterator pListIt = playlists.begin(); pListIt != playlists.end();
           pListIt++){
        std::cout << pListIt->id << ": " << pListIt->uri << std::endl;
        int j = 0;
        for (std::deque<playListEntries>::iterator entryIt = pListIt->entries.begin();
             entryIt != pListIt->entries.end(); entryIt++){
          std::cout << "    " << j++ << ": " << entryIt->filename
                    << " bytePos: " << entryIt->bytePos << std::endl;
        }
      }
    }
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
        pListIt->loadURL(pListIt->uri_root + entryIt->filename);

        keepReading = packet.FromPointer(pListIt->packetPtr);
        pListIt->packetPtr += 188;
      }else{
        in.open((pListIt->uri_root + entryIt->filename).c_str());
        keepReading = packet.FromStream(in);
      }

      while (keepReading){
        tsStream.parse(packet, lastBpos);
        if (pListIt->isUrl()){
          lastBpos = entryIt->bytePos + pListIt->source.size();
          ///\todo get size...
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
                     (pListIt->uri_root + entryIt->filename).c_str(), headerPack.getTrackId(),
                     counter);
            counter++;
          }

          myMeta.live = (playlists.size() && playlists[0].playlistType == LIVE);
          myMeta.vod = !myMeta.live;

          //            myMeta.live = true;
          //            myMeta.vod = false;

          myMeta.live = false;
          myMeta.vod = true;

          if (!hasHeader &&
              (!myMeta.tracks.count(packetId) || !myMeta.tracks[packetId].codec.size())){
            tsStream.initializeMetadata(myMeta, tmpTrackId, packetId);
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

    INFO_MSG("end stream header tracks: %d", myMeta.tracks.size());
    if (hasHeader){return;}

    //    myMeta.live = true;
    //    myMeta.vod = false;
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
        // WORK
        tsStream.partialClear();
        endOfFile = false;

        if (pListIt->isUrl()){
          pListIt->loadURL(pListIt->uri_root + entryIt->filename);
          urlSource.str(pListIt->source);

          endOfFile = !pListIt->atEnd();
          if (!endOfFile){packet.FromPointer(pListIt->packetPtr);}
          pListIt->packetPtr += 188;
        }else{
          in.close();
          in.open((pListIt->uri_root + entryIt->filename).c_str());
          packet.FromStream(in);
          endOfFile = in.eof();
        }

        entId++;
        uint64_t lastBpos = entryIt->bytePos;
        while (!endOfFile){
          tsStream.parse(packet, lastBpos);

          if (pListIt->isUrl()){
            lastBpos = entryIt->bytePos + pListIt->source.size();
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
              INFO_MSG("Added file %s, trackid: %d, mapped to: %d",
                       (pListIt->uri_root + entryIt->filename).c_str(), headerPack.getTrackId(),
                       counter);
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
                     (pListIt->uri_root + entryIt->filename).c_str(), headerPack.getTrackId(),
                     counter);
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
    // at this point, we need to check which playlist we need to reload, and keep reading from that
    // playlist until EndOfPlaylist
    std::vector<int>::iterator result = std::min_element(reloadNext.begin(), reloadNext.end());
    return std::distance(reloadNext.begin(), result);
  }

  void inputHLS::getNext(bool smart){
    INSANE_MSG("Getting next");
    uint32_t tid;
    bool hasPacket = false;
    bool keepReading = false;
    bool endOfFile = false;
    bool doReload = false;

    thisPacket.null();

    while (!hasPacket && config->is_active && (needsLock() || nProxy.userClient.isAlive())){
      if (playlists[currentPlaylist].isUrl()){

        endOfFile = playlists[currentPlaylist].atEnd();
        if (!endOfFile){
          tsBuf.FromPointer(playlists[currentPlaylist].packetPtr);
          playlists[currentPlaylist].packetPtr += 188;
        }

      }else{
        tsBuf.FromStream(in);
        endOfFile = in.eof();
      }

      // eof flag is set after unsuccesful read, so check again
      if (endOfFile){tsStream.finish();}

      if (playlists[currentPlaylist].playlistType == LIVE){
        hasPacket = tsStream.hasPacketOnEachTrack() || (endOfFile && tsStream.hasPacket());
      }else{

        if (!selectedTracks.size()){return;}

        tid = *selectedTracks.begin();
        hasPacket = tsStream.hasPacket(getMappedTrackId(tid));
      }

      if (endOfFile && !hasPacket){
        if (playlists[currentPlaylist].playlistType == LIVE){

          int a = getFirstPlaylistToReload();
          int segmentTime = 30;
          HIGH_MSG("need to reload playlist %d, time: %d", a, reloadNext[a] - Util::bootSecs());

          int f = firstSegment();
          if (f >= 0){segmentTime = playlists[f].entries.front().timestamp - Util::bootSecs();}

          int playlistTime = reloadNext.at(currentPlaylist) - Util::bootSecs() - 1;

          if (playlistTime < segmentTime){
            while (playlistTime > 0 && (needsLock() || nProxy.userClient.isAlive())){
              Util::wait(900);
              nProxy.userClient.keepAlive();
              playlistTime--;
            }

            // update reloadTime before reading the playlist
            reloadNext.at(playlists[a].id) = Util::bootSecs() + playlists[a].waitTime;
            playlists[a].reload();
          }

          waitForNextSegment();
        }

        int b = Util::bootSecs();

        if (!readNextFile()){

          if (playlists[currentPlaylist].playlistType != LIVE){return;}
          // need to reload all available playlists. update the map with the amount of ms to wait
          // before the next check.

          // set specific elements with the correct bootsecs()
          reloadNext.at(currentPlaylist) = b + playlists[currentPlaylist].waitTime;

          int timeToWait = reloadNext.at(currentPlaylist) - Util::bootSecs();

          // at this point, we need to check which playlist we need to reload, and keep reading from
          // that playlist until EndOfPlaylist
          std::vector<int>::iterator result =
              std::min_element(reloadNext.begin(), reloadNext.end());
          int playlistToReload = std::distance(reloadNext.begin(), result);
          currentPlaylist = playlistToReload;

          // dont wait the first time.
          if (timeToWait > 0 && playlists[currentPlaylist].initDone &&
              playlists[currentPlaylist].noChangeCount > 0){
            if (timeToWait > playlists[currentPlaylist].waitTime){
              WARN_MSG("something is not right...");
              return;
            }

            if (playlists[currentPlaylist].noChangeCount < 2){
              timeToWait /= 2; // wait half of the segment size when no segments are found.
            }
          }

          if (playlists[currentPlaylist].playlistEnd){
            INFO_MSG("Playlist %d has reached his end!");
            thisPacket.null();
            return;
          }
        }

        if (playlists[currentPlaylist].isUrl()){
          endOfFile = playlists[currentPlaylist].atEnd();
          if (!endOfFile){
            tsBuf.FromPointer(playlists[currentPlaylist].packetPtr);
            playlists[currentPlaylist].packetPtr += 188;
          }
        }else{
          tsBuf.FromStream(in);
          endOfFile = in.eof();
        }
      }

      if (!endOfFile){
        tsStream.parse(tsBuf, 0);
        if (playlists[currentPlaylist].playlistType == LIVE){
          hasPacket = tsStream.hasPacketOnEachTrack() || (endOfFile && tsStream.hasPacket());
        }else{
          hasPacket = tsStream.hasPacket(getMappedTrackId(tid));
        }
      }
    }

    if (playlists[currentPlaylist].playlistType == LIVE){
      tsStream.getEarliestPacket(thisPacket);
      tid = getOriginalTrackId(currentPlaylist, thisPacket.getTrackId());
    }else{
      tsStream.getPacket(getMappedTrackId(tid), thisPacket);
    }

    if (!thisPacket){
      FAIL_MSG("Could not getNExt TS packet!");
      return;
    }

    // overwrite trackId
    Bit::htobl(thisPacket.getData() + 8, tid);
  }

  void inputHLS::readPMT(){
    if (playlists[currentPlaylist].isUrl()){
      size_t bpos;
      TS::Packet tsBuffer;
      const char *tmpPtr = playlists[currentPlaylist].source.data();

      while (!tsStream.hasPacketOnEachTrack() &&
             (tmpPtr - playlists[currentPlaylist].source.c_str() + 188 <=
              playlists[currentPlaylist].source.size())){
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

      // tsStream.clear();
      tsStream.partialClear(); //?? partialclear gebruiken?, input raakt hierdoor inconsistent..

      in.seekg(bpos, in.beg);
    }
  }

  // Note: bpos is overloaded here for playlist entry!
  void inputHLS::seek(int seekTime){
    INFO_MSG("SEEK");
    tsStream.clear();
    readPMT();
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
    currentPlaylist = getMappedTrackPlaylist(trackId);

    Playlist &curPlaylist = playlists[currentPlaylist];
    playListEntries &entry = curPlaylist.entries.at(currentIndex);
    if (curPlaylist.isUrl()){
      curPlaylist.loadURL(curPlaylist.uri_root + entry.filename);
    }else{
      in.close();
      in.open((curPlaylist.uri_root + entry.filename).c_str());
    }
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

  /// Very first function to be called on a regular playlist or variant playlist.
  bool inputHLS::initPlaylist(const std::string &uri){
    std::string line;
    bool ret = false;
    startTime = Util::bootSecs();
    std::string init_source;

    std::string playlistRootPath = uri.substr(0, uri.rfind("/") + 1);

    std::istringstream urlSource;
    std::ifstream fileSource;

    bool isUrl = false;
    if (uri.compare(0, 7, "http://") == 0){
      isUrl = true;
      Playlist p;
      p.loadURL(uri);
      init_source = p.source;
      urlSource.str(init_source);
    }else{
      fileSource.open(uri.c_str());
    }

    std::istream &input = (isUrl ? (std::istream &)urlSource : (std::istream &)fileSource);
    std::getline(input, line);

    while (std::getline(input, line)){
      if (!line.empty()){// skip empty lines in the playlist
        if (line.compare(0, 17, "#EXT-X-STREAM-INF") == 0){
          // this is a variant playlist file.. next line is an uri to a playlist file
          std::getline(input, line);
          ret = readPlaylist(playlistRootPath + line);
        }else if (line.compare(0, 12, "#EXT-X-MEDIA") == 0){
          // this is also a variant playlist, but streams need to be processed another way

          std::string mediafile;
          if (line.compare(18, 5, "AUDIO") == 0){
            // find URI attribute
            int pos = line.find("URI");
            if (pos != std::string::npos){
              mediafile = line.substr(pos + 5, line.length() - pos - 6);
              ret = readPlaylist(playlistRootPath + mediafile);
            }
          }

        }else if (line.compare(0, 7, "#EXTINF") == 0){
          // current file is not a variant playlist, but regular playlist.
          ret = readPlaylist(uri);
          break;
        }else{
          // ignore wrong lines
          WARN_MSG("ignore wrong line: %s", line.c_str());
        }
      }
    }

    if (!isUrl){fileSource.close();}

    return ret;
  }

  /// Function for reading every playlist.
  bool inputHLS::readPlaylist(const std::string &uri){
    Playlist p(uri);
    p.id = playlists.size();
    // set size of reloadNext to playlist count with default value 0
    playlists.push_back(p);

    if (reloadNext.size() < playlists.size()){reloadNext.resize(playlists.size());}

    reloadNext.at(p.id) = Util::bootSecs() + p.waitTime;
    return true;
  }

  /// Read next .ts file from the playlist. (from the list of entries which needs to be processed)
  bool inputHLS::readNextFile(){
    tsStream.clear();
    Playlist &curList = playlists[currentPlaylist];

    if (!curList.entries.size()){
      VERYHIGH_MSG("no entries found in playlist: %d!", currentPlaylist);
      return false;
    }

    std::string url = (curList.uri_root + curList.entries.front().filename).c_str();

    if (curList.isUrl() && curList.loadURL(url)){
      curList.entries.pop_front(); // remove the item which is opened for reading.
    }

    if (curList.playlistType == LIVE){
      in.close();
      in.open(url.c_str());

      if (in.good()){
        curList.entries.pop_front(); // remove the item which is opened for reading.
        return true;
      }
      return false;
    }
    ++currentIndex;
    if (curList.entries.size() <= currentIndex){
      INFO_MSG("end of playlist reached!");
      return false;
    }
    in.close();
    url = curList.uri_root + curList.entries.at(currentIndex).filename;

    in.open(url.c_str());
    return true;
  }

  /// return the playlist id from which we need to read the first upcoming segment by timestamp.
  /// this will keep the playlists in sync while reading segments.
  int inputHLS::firstSegment(){
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

  // read the next segment
  void inputHLS::waitForNextSegment(){
    uint32_t pListId = firstSegment();
    if (pListId == -1){
      VERYHIGH_MSG("no segments found!");
      return;
    }
    int segmentTime = playlists[pListId].entries.front().timestamp - Util::bootSecs();
    if (segmentTime){
      --segmentTime;
      while (segmentTime > 1 && (needsLock() || nProxy.userClient.isAlive())){
        Util::wait(1000);
        --segmentTime;
        continueNegotiate();
        nProxy.userClient.keepAlive();
      }
    }
  }
}

