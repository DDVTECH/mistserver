#include <iostream>
#include <iomanip>
#include <fstream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <string>
#include <mist/stream.h>
#include <mist/flv_tag.h>
#include <mist/defines.h>
#include <mist/ts_packet.h>
#include <mist/timing.h>
#include <mist/mp4_generic.h>
#include "input_hls.h"
#include <mist/bitfields.h>
#include <mist/tinythread.h>
#include <sys/stat.h>
#include <mist/http_parser.h>
#include <algorithm>

#define SEM_TS_CLAIM "/MstTSIN%s"


namespace Mist {

  Playlist::Playlist(){
    lastFileIndex = 0;
    entryCount = 0;
    waitTime = 2;
    playlistEnd = false;
    noChangeCount = 0;
    vodLive = false;
  }

  /// Constructor of HLS Input
  inputHLS::inputHLS(Util::Config * cfg) : Input(cfg) {
    currentPlaylist = 0;

    capa["name"] = "HLS";
    capa["decs"] = "Enables HLS Input";
    capa["source_match"].append("/*.m3u8");
    capa["source_match"].append("http://*.m3u8");

    capa["priority"] = 9ll;
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("AC3");
    capa["codecs"][0u][1u].append("MP3");

    isUrl = false;
   
    initDone = false;
    inFile = NULL;
  }

  inputHLS::~inputHLS() {
    if (inFile) {
      fclose(inFile);
    }
  }

  void inputHLS::printContent() {
    for(int i=0;i< playlists.size();i++) {
      std::cout << i << ": " << playlists[i].uri << std::endl;
        for(int j = 0;j < playlists[i].entries.size(); j++){
          std::cout << "    " << j << ": " <<  playlists[i].entries.at(j).filename << " bytePos: " << playlists[i].entries[j].bytePos << std::endl;
        }
    }
  }

  bool inputHLS::setup() {
    if (config->getString("input") != "-") {
      playlistFile = config->getString("input");
      INFO_MSG("opening playlist file... %s" , playlistFile.c_str());
      playlistRootPath = playlistFile.substr(0,playlistFile.rfind("/")+1);

      if(initPlaylist(playlistFile)) {
//        printContent();
        return true;
      }
    }

    return false;
  }

  void inputHLS::trackSelect(std::string trackSpec) {
    selectedTracks.clear();
    long long int index;
    while (trackSpec != "") {
      index = trackSpec.find(' ');
      selectedTracks.insert(atoi(trackSpec.substr(0, index).c_str()));
      if (index != std::string::npos) {
        trackSpec.erase(0, index + 1);
      } else {
        trackSpec = "";
      }
    }
  }

  void inputHLS::parseStreamHeader() {
    bool hasHeader = false;
    if(!hasHeader){
      myMeta = DTSC::Meta();
    }

    TS::Packet packet;//to analyse and extract data
    int thisEntry = 0;
    int thisPlaylist =0;
    int counter = 1;
    int packetId = 0;

    char * data;
    unsigned int dataLen;
    bool keepReading = false;

    for(int i = 0;i<playlists.size();i++) {
      if(!playlists[i].entries.size()){ continue; }

        std::deque<playListEntries>::iterator entryIt = playlists[i].entries.begin();
        //INFO_MSG("opening...: %s",(playlists[i].uri_root + entryIt->filename).c_str());

        tsStream.clear();
        uint64_t lastBpos = entryIt->bytePos; 

        if(isUrl){
          openURL((playlists[i].uri_root + entryIt->filename).c_str(),playlists[i]);
//          packetPtr = source.c_str();

          keepReading = packet.FromPointer(playlists[i].packetPtr); 
          playlists[i].packetPtr += 188;
        }else{
          in.open((playlists[i].uri_root + entryIt->filename).c_str());
          keepReading = packet.FromStream(in);
        }

        //while(packet.FromStream(in)) {
        while(keepReading) {
          tsStream.parse(packet, lastBpos);
//          INFO_MSG("keepreading"); 
          if(isUrl){
            lastBpos = entryIt->bytePos + playlists[i].source.size();
           //get size... TODO 
          }else{
            lastBpos = entryIt->bytePos + in.tellg(); 
          }

          while (tsStream.hasPacketOnEachTrack()) {
            DTSC::Packet headerPack;
            tsStream.getEarliestPacket(headerPack);
            int tmpTrackId = headerPack.getTrackId();
            packetId = pidMapping[(i<<16)+tmpTrackId];
            
            if(packetId == 0) {          
              pidMapping[(i<<16)+headerPack.getTrackId()] = counter;
              pidMappingR[counter] = (i<<16)+headerPack.getTrackId();
              packetId = counter;
              HIGH_MSG("Added file %s, trackid: %d, mapped to: %d",(playlists[i].uri_root + entryIt->filename).c_str(),headerPack.getTrackId(),counter);
              counter++;
            }

myMeta.live = (playlists[currentPlaylist].playlistType == LIVE);
myMeta.vod = !myMeta.live;

//            myMeta.live = true;
//            myMeta.vod = false;

            myMeta.live = false;
            myMeta.vod = true;

            if (!hasHeader && (!myMeta.tracks.count(packetId) || !myMeta.tracks[packetId].codec.size())) {
              tsStream.initializeMetadata(myMeta, tmpTrackId, packetId);
            }
          }
        
          if(isUrl){
            if((playlists[i].packetPtr - playlists[i].source.c_str()) +188 < playlists[i].source.size()){           
              keepReading = packet.FromPointer(playlists[i].packetPtr); 
              playlists[i].packetPtr += 188;
            }else{
              keepReading = false;
            }

          }else{
            keepReading = packet.FromStream(in);
          }
        }

        in.close();
      }
      tsStream.clear();

INFO_MSG("end stream header tracks: %d",myMeta.tracks.size());
    if(hasHeader) {
      return;
    }

//    myMeta.live = true;
//    myMeta.vod = false;
    in.close();
  }

  bool inputHLS::readHeader() {
    //if(playlists[currentPlaylist].playlistType == LIVE || isUrl){
    if(playlists[currentPlaylist].playlistType == LIVE){
      return true;
    }

    std::istringstream urlSource;
    std::ifstream fileSource;
    
    bool endOfFile = false;
    bool hasHeader = false;

    //See whether a separate header file exists.
    DTSC::File tmp(config->getString("input") + ".dtsh");
    if (tmp) {
      myMeta = tmp.getMeta();
      if (myMeta) {
        hasHeader = true;
      } 
    }

    if(!hasHeader){
      myMeta = DTSC::Meta();
    }

    TS::Packet packet;//to analyse and extract data

    int thisEntry = 0;
    int thisPlaylist =0;
    int counter = 1;
    int packetId = 0;

    char * data;
    unsigned int dataLen;

    for(int i = 0;i<playlists.size();i++) {
      tsStream.clear();
      uint32_t entId = 0;
      
      INFO_MSG("reading new playlist...%d",i);
      for(std::deque<playListEntries>::iterator entryIt = playlists[i].entries.begin(); entryIt != playlists[i].entries.end();entryIt++) {
      //WORK
      tsStream.partialClear();
      endOfFile = false;

    if(isUrl){
      openURL((playlists[i].uri_root + entryIt->filename).c_str(),playlists[i]);
      urlSource.str(playlists[i].source);
      
      if ((playlists[i].packetPtr - playlists[i].source.data() +188) < playlists[i].source.size())
        {
          packet.FromPointer(playlists[i].packetPtr); 
          endOfFile = false;
        }else{
          endOfFile = true;
        }
        playlists[i].packetPtr += 188;
    }else{
     // fileSource.open(uri.c_str())
     in.close();
      in.open((playlists[i].uri_root + entryIt->filename).c_str());
      packet.FromStream(in);
      endOfFile = in.eof();
    }

        entId++;
       uint64_t lastBpos = entryIt->bytePos; 
        while(!endOfFile) {
          tsStream.parse(packet, lastBpos);

          if(isUrl){
            lastBpos = entryIt->bytePos + playlists[currentPlaylist].source.size(); 
          }else{
            lastBpos = entryIt->bytePos + in.tellg(); 
          }

          while (tsStream.hasPacketOnEachTrack()) {
            DTSC::Packet headerPack;
            tsStream.getEarliestPacket(headerPack);

            int tmpTrackId = headerPack.getTrackId();
            packetId = pidMapping[(i<<16)+tmpTrackId];
           
            if(packetId == 0) {          
              pidMapping[(i<<16)+headerPack.getTrackId()] = counter;
              pidMappingR[counter] = (i<<16)+headerPack.getTrackId();
              packetId = counter;
              INFO_MSG("Added file %s, trackid: %d, mapped to: %d",(playlists[i].uri_root + entryIt->filename).c_str(),headerPack.getTrackId(),counter);
              counter++;
            }

            if (!hasHeader && (!myMeta.tracks.count(packetId) || !myMeta.tracks[packetId].codec.size())) {
              tsStream.initializeMetadata(myMeta, tmpTrackId, packetId);
            }

            if(!hasHeader){
              headerPack.getString("data", data, dataLen);
              uint64_t  pBPos = headerPack.getInt("bpos");
              
              //keyframe data exists, so always add 19 bytes keyframedata.              
              long long packOffset = headerPack.hasMember("offset")?headerPack.getInt("offset"):0;
              long long packSendSize = 24 + (packOffset?17:0) + (entId>=0?15:0) + 19 + dataLen+11;
              myMeta.update(headerPack.getTime(), packOffset, packetId, dataLen, entId, headerPack.hasMember("keyframe"),packSendSize);
            }
          }

          if(isUrl){
            if ((playlists[i].packetPtr - playlists[i].source.data() +188) < playlists[i].source.size())
            {
              packet.FromPointer(playlists[i].packetPtr); 
              endOfFile = false;
            }else{
              endOfFile = true;
            }
            playlists[i].packetPtr += 188;
          }else{
             // fileSource.open(uri.c_str())
              packet.FromStream(in);
              endOfFile = in.eof();
          }

        }
//get last packets
          tsStream.finish();
          DTSC::Packet headerPack;
          tsStream.getEarliestPacket(headerPack);
         while (headerPack) {

            int tmpTrackId = headerPack.getTrackId();
            packetId = pidMapping[(i<<16)+tmpTrackId];
           
            if(packetId == 0) {          
              pidMapping[(i<<16)+headerPack.getTrackId()] = counter;
              pidMappingR[counter] = (i<<16)+headerPack.getTrackId();
              packetId = counter;
              INFO_MSG("Added file %s, trackid: %d, mapped to: %d",(playlists[i].uri_root + entryIt->filename).c_str(),headerPack.getTrackId(),counter);
              counter++;
            }

            if (!hasHeader && (!myMeta.tracks.count(packetId) || !myMeta.tracks[packetId].codec.size())) {
              tsStream.initializeMetadata(myMeta, tmpTrackId, packetId);
            }

            if(!hasHeader){
              headerPack.getString("data", data, dataLen);
              uint64_t  pBPos = headerPack.getInt("bpos");
              
              //keyframe data exists, so always add 19 bytes keyframedata.              
              long long packOffset = headerPack.hasMember("offset")?headerPack.getInt("offset"):0;
              long long packSendSize = 24 + (packOffset?17:0) + (entId>=0?15:0) + 19 + dataLen+11;
              myMeta.update(headerPack.getTime(), packOffset, packetId, dataLen, entId, headerPack.hasMember("keyframe"),packSendSize);
            }
            tsStream.getEarliestPacket(headerPack);
          }


        if(isUrl){
          in.close();
        }

        if(hasHeader) {
          break;
        }

      }
      
    }

    if(hasHeader || isUrl) {
      return true;
    }


    INFO_MSG("write header file...");
    std::ofstream oFile((config->getString("input") + ".dtsh").c_str());

    oFile << myMeta.toJSON().toNetPacked();
    oFile.close();
    in.close();

    return true;
  }

  bool inputHLS::needsLock() {
    if(isUrl){
      return false;
    }
    return (playlists.size() <= currentPlaylist) || !(playlists[currentPlaylist].playlistType == LIVE);
  }

  bool inputHLS::openStreamSource(){
    return true;
  }

  int inputHLS::getFirstPlaylistToReload(){
    //at this point, we need to check which playlist we need to reload, and keep reading from that playlist until EndOfPlaylist 
    std::vector<int>::iterator result = std::min_element(reloadNext.begin(), reloadNext.end());
    int playlistToReload = std::distance(reloadNext.begin(), result); 
    // std::cout << "min element at: " << std::distance(std::begin(reloadNext), result); 
    // currentPlaylist = playlistToReload;
    return playlistToReload;
  }

  void inputHLS::getNext(bool smart) {
    INSANE_MSG("Getting next");
    uint32_t tid;
    bool hasPacket = false;
    bool keepReading = false;
    bool endOfFile = false;
    bool doReload = false;

    thisPacket.null();

    while (!hasPacket && config->is_active) {
      //tsBuf.FromStream(in);
    
      if(isUrl){

        if ((playlists[currentPlaylist].packetPtr - playlists[currentPlaylist].source.data() +188) <= playlists[currentPlaylist].source.size())
        {
          tsBuf.FromPointer(playlists[currentPlaylist].packetPtr); 
          endOfFile = false;
        }else{
          endOfFile = true;
        }

        playlists[currentPlaylist].packetPtr += 188;
      }else{
        tsBuf.FromStream(in);
        endOfFile = in.eof();
      }


      //eof flag is set after unsuccesful read, so check again
      //if(in.eof()) {

      if(endOfFile){
        tsStream.finish();
      }

      if(playlists[currentPlaylist].playlistType == LIVE){
        hasPacket = tsStream.hasPacketOnEachTrack() || (endOfFile && tsStream.hasPacket());
      }else{

        if(!selectedTracks.size()) { 
          return;
        }

        tid = *selectedTracks.begin();
        hasPacket = tsStream.hasPacket(getMappedTrackId(tid));
      }

        if(endOfFile && !hasPacket) {
          INFO_MSG("end of file: bootsecs: %d",Util::bootSecs());

          if(playlists[currentPlaylist].playlistType == LIVE){
         
            int a = getFirstPlaylistToReload();
            int segmentTime = 30;
            HIGH_MSG("need to reload playlist %d, time: %d",a,reloadNext[a]- Util::bootSecs());

            int f = firstSegment();
            if(f >= 0){  
              segmentTime = playlists[f].entries.front().timestamp - Util::bootSecs();
            }

            int playlistTime = reloadNext.at(currentPlaylist) - Util::bootSecs() -1;

            if(playlistTime < segmentTime){
//                printBuffer();
                INFO_MSG("playlist waiting... %d ms",playlistTime * 900);
                
                while(playlistTime > 0){
                  Util::wait(900);
                  nProxy.userClient.keepAlive();
                  playlistTime--;
                }

              //on eof, first reload playlist.
              if(reloadPlaylist(playlists[a])){
//                INFO_MSG("playlist %d reloaded!",a); 
//                playlists[currentPlaylist].noChangeCount = 0;
              }else{
                  
  //            INFO_MSG("playlist %d reloaded without changes!, checked %d times...",currentPlaylist,playlists[currentPlaylist].noChangeCount); 
  //            playlists[currentPlaylist].noChangeCount++;

//                if(playlists[currentPlaylist].noChangeCount > 3){ 
  //                INFO_MSG("enough!");
//                  return; 
    //            }
              }
            }

            //check if other playlists have to be reloaded, and do so.
//            printBuffer();
            getNextSegment();
          }

          int b = Util::bootSecs();

          if(!readNextFile()) {

            if(playlists[currentPlaylist].playlistType == LIVE){
             //need to reload all available playlists. update the map with the amount of ms to wait before the next check.
             
              if(reloadNext.size() < playlists.size())
              {
                reloadNext.push_back(Util::bootSecs() + playlists[currentPlaylist].waitTime);
                currentPlaylist++;
              }else{
                //set specific elements with the correct bootsecs()
                //reloadNext.at(currentPlaylist) = Util::bootSecs() + playlists[currentPlaylist].waitTime;
                reloadNext.at(currentPlaylist) = b + playlists[currentPlaylist].waitTime;
                //for(int i = 0; i < reloadNext.size(); i++)
                //{
                  //INFO_MSG("Reload vector index %d, time: %d", i,reloadNext[i]- Util::bootSecs());
                //}
                initDone = true;
              }
            
              int timeToWait = reloadNext.at(currentPlaylist) - Util::bootSecs();

              if(playlists[currentPlaylist].vodLive){
                //if(currentPlaylist == playlists.size()-1)//if last playlist, put a delay
                if(currentPlaylist == 0)
                {
                  timeToWait = 0; //playlists[currentPlaylist].waitTime;
                }else{
                  timeToWait = 0;
                }
              }else{
                //at this point, we need to check which playlist we need to reload, and keep reading from that playlist until EndOfPlaylist 
                std::vector<int>::iterator result = std::min_element(reloadNext.begin(), reloadNext.end());
                int playlistToReload = std::distance(reloadNext.begin(), result); 
                currentPlaylist = playlistToReload;
              }
                //dont wait the first time.
                if(timeToWait > 0 && initDone && playlists[currentPlaylist].noChangeCount > 0)
                {
                  if(timeToWait > playlists[currentPlaylist].waitTime){
                    WARN_MSG("something is not right...");
                    return;
                  }

                  if(playlists[currentPlaylist].noChangeCount < 2){
                    timeToWait /= 2;//wait half of the segment size when no segments are found.
                  }
                }else{
//                  INFO_MSG("no need to delay, update time already past");
                }
               
                if(playlists[currentPlaylist].playlistEnd){
                  INFO_MSG("Playlist %d has reached his end!");
                  thisPacket.null();
                  return;
                }

              if(playlists[currentPlaylist].vodLive){
                currentPlaylist++;
//                INFO_MSG("currentplaylist: %d, playlistsize: %d",currentPlaylist, playlists.size());
                if(currentPlaylist >= playlists.size())
                {
                  currentPlaylist = 0;
                  for(int i = 0;i < playlists.size();i++)
                  {
                    INFO_MSG("p %d entry 0: %s",i, playlists[i].entries[0].filename.c_str());
                  }

                  Util::wait(1000);
                }
              }
            }else{
              return;
            }
          }

          if(isUrl){
            if (playlists[currentPlaylist].packetPtr - playlists[currentPlaylist].source.c_str() +188 <= playlists[currentPlaylist].source.size())
            {
              tsBuf.FromPointer(playlists[currentPlaylist].packetPtr); 
              endOfFile = false;
            }else{
              endOfFile = true;
            }

            playlists[currentPlaylist].packetPtr += 188;

          }else{
            tsBuf.FromStream(in);
            endOfFile = in.eof();
          }
        }else{
//          INFO_MSG("not eof, read: %d, total: %d", packetPtr-source.data(), source.size());
        }

        if(!endOfFile){
          tsStream.parse(tsBuf, 0);
          if(playlists[currentPlaylist].playlistType == LIVE){
            hasPacket = tsStream.hasPacketOnEachTrack() || (endOfFile && tsStream.hasPacket());
          }else{
            hasPacket = tsStream.hasPacket(getMappedTrackId(tid));
          }
        }
    }
    
    if(playlists[currentPlaylist].playlistType == LIVE){
      tsStream.getEarliestPacket(thisPacket);
      tid = getOriginalTrackId(currentPlaylist,thisPacket.getTrackId());
    }else{
      tsStream.getPacket(getMappedTrackId( tid), thisPacket);
    }

    if(!thisPacket) {
      FAIL_MSG("Could not getNExt TS packet!");
      return;
    }

    //overwrite trackId
    Bit::htobl(thisPacket.getData()+8,tid);
  }


  void inputHLS::readPMT(){
    if(isUrl){

      size_t bpos;
      TS::Packet tsBuffer;
      const char *tmpPtr = playlists[currentPlaylist].source.data();
      
      while (!tsStream.hasPacketOnEachTrack() && (tmpPtr - playlists[currentPlaylist].source.c_str() +188 <= playlists[currentPlaylist].source.size()))
      {
        tsBuffer.FromPointer(tmpPtr);
        tsStream.parse(tsBuffer, 0);
        tmpPtr += 188;
      }
      tsStream.partialClear();

    }else{
      size_t bpos = in.tellg();
      in.seekg(0, in.beg);
      TS::Packet tsBuffer;
      while (!tsStream.hasPacketOnEachTrack() && tsBuffer.FromStream(in)) {
        tsStream.parse(tsBuffer, 0);
      }

      //tsStream.clear();
      tsStream.partialClear();  //?? partialclear gebruiken?, input raakt hierdoor inconsistent..

      in.seekg(bpos,in.beg);
    }
  }

  void inputHLS::seek(int seekTime) {
    INFO_MSG("SEEK");
    tsStream.clear();
    readPMT();
    int trackId = 0;
    
    unsigned long seekPos = 0xFFFFFFFFull;
    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
      unsigned long thisBPos = 0;
      for (std::deque<DTSC::Key>::iterator keyIt = myMeta.tracks[*it].keys.begin(); keyIt != myMeta.tracks[*it].keys.end(); keyIt++) {
        if (keyIt->getTime() > seekTime) {
          break;
        }
        thisBPos = keyIt->getBpos();
      }
      if (thisBPos < seekPos) {
        seekPos = thisBPos;
        trackId = *it;
      }
    }

    int playlistId = getMappedTrackPlaylist(trackId);
    int entryId = seekPos-1;

    if(entryId < 0) {
      WARN_MSG("attempted to seek outside the file");
      return;
    }

    currentIndex = entryId;
    currentPlaylist = playlistId;
  
    if(isUrl){
      openURL((playlists[currentPlaylist].uri_root+ playlists[currentPlaylist].entries.at(entryId).filename).c_str(), playlists[currentPlaylist]);

    }else{
      in.close();
      in.open((playlists[currentPlaylist].uri_root+ playlists[currentPlaylist].entries.at(entryId).filename).c_str());
    }
  }

  int inputHLS::getEntryId(int playlistId, uint64_t bytePos) {
    if(bytePos == 0) { return 0;}

    for(int i = 0;i<playlists[playlistId].entries.size();i++) {
      if(playlists[playlistId].entries.at(i).bytePos > bytePos) {
        return i-1;
      }
    }

    return playlists[playlistId].entries.size()-1;
  }

  int inputHLS::getOriginalTrackId(int playlistId,int id) {
    return pidMapping[(playlistId << 16) + id]; 
  }

  int inputHLS::getMappedTrackId(int id) {
    return (pidMappingR[id] & 0xFFFF);
  }

  int inputHLS::getMappedTrackPlaylist(int id) {
    return (pidMappingR[id] >> 16);
  }

  ///Very first function to be called on a regular playlist or variant playlist.
  bool inputHLS::initPlaylist(std::string uri) {
    std::string line;
    bool ret = false;
    startTime = Util::bootSecs();

    playlistRootPath = uri.substr(0,uri.rfind("/")+1);
    
    std::istringstream urlSource;
    std::ifstream fileSource;

    if(uri.compare(0,7,"http://") == 0){
      isUrl = true;
      Playlist p;
      openURL(uri,p);
      init_source = p.source;
      urlSource.str(init_source);
    }else{
      fileSource.open(uri.c_str());
    }

    std::istream & input = (isUrl ? (std::istream&)urlSource : (std::istream&)fileSource);
    std::getline(input,line);

    while(std::getline(input, line)) {
      if(!line.empty()){    //skip empty lines in the playlist
        if (line.compare(0,17,"#EXT-X-STREAM-INF") == 0) {
          //this is a variant playlist file.. next line is an uri to a playlist file
          std::getline(input, line);
          ret = readPlaylist(playlistRootPath + line);
        }else if(line.compare(0,12,"#EXT-X-MEDIA") == 0){
          //this is also a variant playlist, but streams need to be processed another way

          std::string mediafile;
          if(line.compare(18,5,"AUDIO") == 0) {
             //find URI attribute
             int pos = line.find("URI");
             if (pos != std::string::npos) {
               mediafile = line.substr(pos+5,line.length()-pos-6);
               ret = readPlaylist(playlistRootPath + mediafile);
             }
          }

        }else if(line.compare(0,7,"#EXTINF") ==0) {
          //current file is not a variant playlist, but regular playlist.
          ret = readPlaylist(uri);
          break;
        }else{
          //ignore wrong lines
          WARN_MSG("ignore wrong line: %s",line.c_str());
        }
      }
    }

    if(!isUrl){
      fileSource.close();
    }
   
    initDone = true;
    return ret;
  }

  ///Function for reading every playlist.
  bool inputHLS::readPlaylist(std::string uri) {
    std::string line;
    std::string key;
    std::string val;
    Playlist p;
    int count = 0;
    p.lastTimestamp = 0;
    p.uri = uri;
    uint64_t totalBytes = 0;
    p.uri_root = uri.substr(0,uri.rfind("/")+1);
    p.playlistType = LIVE;  //TMP 
    INFO_MSG("readplaylist: %s",uri.c_str());

    std::istringstream urlSource;
    std::ifstream fileSource;

    p.id = playlists.size();

    if(uri.compare(0,7,"http://") == 0){
      isUrl = true;
      openURL(uri,p);
      urlSource.str(p.source);
    }else{
      fileSource.open(uri.c_str());
      isUrl = false;
    }

    std::istream & input = (isUrl ? (std::istream&)urlSource : (std::istream&)fileSource);
    std::getline(input,line);

    while(std::getline(input, line)) {
      cleanLine(line);
      
      if(!line.empty()){
        if (line.compare(0,7,"#EXT-X-") == 0) {
          size_t pos = line.find(":");
          key = line.substr(7,pos-7);
          val = line.c_str() + pos + 1;

          if(key == "VERSION") {
            p.version = atoi(line.c_str()+pos+1); 
          }
          
          if(key == "TARGETDURATION") {
            p.targetDuration = atoi(line.c_str()+pos+1);
            p.waitTime = p.targetDuration;
          }

          if(key == "MEDIA-SEQUENCE") {
            p.media_sequence = atoi(line.c_str()+pos+1);
            p.lastFileIndex = p.media_sequence;
          }

          if(key == "PLAYLIST-TYPE") {
             if(val == "VOD") {
                p.playlistType = VOD;
             }else if(val == "LIVE") {
                p.playlistType = LIVE;
             }else if(val == "EVENT") {
                p.playlistType = EVENT;
             }
          }
          
          if(key == "ENDLIST"){
            //end of playlist reached!
            p.playlistEnd = true;
            p.playlistType = VOD;
          }

        }
        else if(line.compare(0,7,"#EXTINF") == 0) {
          float f = atof(line.c_str()+8);
          std::string filename;
          std::getline(input,filename);
          addEntryToPlaylist(p,filename,f,totalBytes);
          count++;
        }
        else {
          VERYHIGH_MSG("ignoring wrong line: %s.", line.c_str());
          continue;
        }
      }
    }

    if(isUrl)
    {
      p.playlistType = LIVE;//VOD over HTTP needs to be processed as LIVE.
      //p.vodLive= true;

      p.playlistEnd = false;
      fileSource.close();
    }

    //set size of reloadNext to playlist count with default value 0
    playlists.push_back(p);
    
    if(reloadNext.size() < playlists.size()){
      reloadNext.resize(playlists.size());
    }

    reloadNext.at(p.id) = Util::bootSecs() + p.waitTime;
    return true;
  }

  ///For debugging purposes only. prints the entries for every playlist which needs to be processed.
  void inputHLS::printBuffer()
  {
    INFO_MSG("--------------------------- printbuffer---------------------#######");
    for(int i = 0;i < playlists.size();i++){
      for(int j = 0;j<playlists[i].entries.size();j++){
        INFO_MSG("playlist: %d, entry: %d, segment: %s, timestamp: %d"
          ,i
          ,j
          ,playlists[i].entries[j].filename.c_str()
          ,playlists[i].entries[j].timestamp - Util::bootSecs()
            )
      }
    }

    for(int i = 0; i < reloadNext.size(); i++)
    {
      INFO_MSG("Reload vector index %d, \t\t\t\t\ttime: %d", i,reloadNext[i]- Util::bootSecs());
    }

    INFO_MSG("--------------------------------------------------------------#######################################");
  }

  ///function for adding segments to the playlist to be processed. used for VOD and live
  void inputHLS::addEntryToPlaylist(Playlist &p, std::string filename, float duration, uint64_t &totalBytes){
     playListEntries entry;
     cleanLine(filename);
     entry.filename = filename;
     std::string test = p.uri_root + entry.filename;
    
     std::istringstream urlSource;
     std::ifstream fileSource;

     if(isUrl){
      urlSource.str(p.source);
     }else{
      fileSource.open(test.c_str(), std::ios::ate | std::ios::binary);
      if ( (fileSource.rdstate() & std::ifstream::failbit ) != 0 ) {
        WARN_MSG("file: %s, error: %s", test.c_str(), strerror(errno));
      }
     }

     entry.bytePos = totalBytes;
     entry.duration = duration;
     if(isUrl){
//      totalBytes += p.source.size();
     }else{
      totalBytes += fileSource.tellg(); 
     }

     if(initDone){
      p.lastTimestamp += duration;
      entry.timestamp = p.lastTimestamp + startTime;
      entry.wait = p.entryCount * duration;
     }else{
      entry.timestamp = 0;    //read all segments immediatly at the beginning, then use delays
     }
     p.entryCount++;
     p.entries.push_back(entry);
     p.lastFileIndex++;

  }

  ///Function for reloading the playlist in case of live streams.
  bool inputHLS::reloadPlaylist(Playlist &p){
    int skip = p.lastFileIndex - p.media_sequence;
    bool ret = false; 
    std::string line;
    std::string key;
    std::string val;
    int count = 0;

    uint64_t totalBytes = 0; 

    std::istringstream urlSource;
    std::ifstream fileSource;
   
    //update reloadTime before reading the playlist
    reloadNext.at(p.id) = Util::bootSecs() + p.waitTime;

    if(isUrl){
      openURL(p.uri.c_str(),p); //get size only!
      urlSource.str(p.source);
    }else{
      fileSource.open(p.uri.c_str());
    }

    std::istream & input = (isUrl ? (std::istream&)urlSource : (std::istream&)fileSource);
    std::getline(input,line);

    while(std::getline(input, line)) {
      cleanLine(line);
      if(!line.empty()){
        if (line.compare(0,7,"#EXT-X-") == 0) {
          size_t pos = line.find(":");
          key = line.substr(7,pos-7);
          
          //only update the media sequence, as this is needed for live
          if(key == "MEDIA-SEQUENCE") {
            p.media_sequence = atoi(line.c_str()+pos+1);
            skip = (p.lastFileIndex - p.media_sequence);
          }

        }else if(line.compare(0,7,"#EXTINF") == 0) {
          float f = atof(line.c_str()+8);
          //next line belongs to this item
          std::string filename;
          std::getline(input,filename);

          //check for already added segments
          if(skip == 0){
            addEntryToPlaylist(p,filename,f,totalBytes);
            count++;
//            INFO_MSG("adding new segment! entrySize: %d, %s",p.entries.size(),filename.c_str());
          }else{
            cleanLine(filename);
//            INFO_MSG("skipping line: %s",filename.c_str());
            skip--;
          }

        }
        else {
          VERYHIGH_MSG("ignoring wrong line: %s.", line.c_str());
          continue;
        }
      }
     
      /* process everything, reading limiter is placed in getnext
      if(p.vodLive && count > 0){
        INFO_MSG("breaking here!!!!!!!!!!!!");
        fileSource.close();
        return true;

        break;//max files to process
      }*/

    }

    if(isUrl){
      fileSource.close();
    }

    ret = (count >0);
    
    if(ret){
      
      p.noChangeCount = 0;
    }else{
//      INFO_MSG("playlist %d reloaded without changes!, checked %d times...",p.id,p.noChangeCount); 
      p.noChangeCount++;
      if(p.noChangeCount > 3){ 
        VERYHIGH_MSG("enough!");
        //return;
      }
    }

    return ret;
  }

  //remove trailing \r for windows generated playlist files 
  int inputHLS::cleanLine(std::string &s) { 
    if (s.length() > 0 && s.at( s.length() - 1 ) == '\r') {
      s.erase(s.size() - 1);
    }
  } 

  bool inputHLS::openURL(std::string urlString, Playlist &p){
    //HTTP::URL url("http://nikujkjk");
    HIGH_MSG("opening URL: %s",urlString.c_str());
    
    HTTP::URL url(urlString);
    if (url.protocol != "http"){
      FAIL_MSG("Protocol %s is not supported", url.protocol.c_str());
      return false;
    }

    //if connection is open, reuse this connection
    if(!conn){
//      INFO_MSG("init not connected");
      conn = Socket::Connection(url.host, url.getPort(), false);
      if(!conn){
        INFO_MSG("Failed to reach %s on port %lu", url.host.c_str(), url.getPort());
        return false;
      }

    }

    HTTP::Parser http;
    http.url = "/" + url.path;
    http.method = "GET";
    http.SetHeader("Host", url.host);
    http.SetHeader("X-MistServer", PACKAGE_VERSION);

    conn.SendNow(http.BuildRequest());
    http.Clean();
      
    unsigned int startTime = Util::epoch();
    p.source.clear(); 
    p.packetPtr = 0;
    while ((Util::epoch() - startTime < 10) && (conn || conn.Received().size())){
      if (conn.spool() || conn.Received().size()){
        if (http.Read(conn)){
          p.source = http.body;
          p.packetPtr = p.source.data();
          conn.close();
          return true;
        }
      }
    }

    if (conn){
          FAIL_MSG("Timeout!");
          conn.close();
    }else{
          FAIL_MSG("Lost connection!");
          INFO_MSG("bytes received %d",conn.Received().size());
    }

    return false;
  }

  ///Read next .ts file from the playlist. (from the list of entries which needs to be processed)
  bool inputHLS::readNextFile() {
    tsStream.clear();

    if(!playlists[currentPlaylist].entries.size()){
      VERYHIGH_MSG("no entries found in playlist: %d!",currentPlaylist);
      return false;
    }

    std::string url = (playlists[currentPlaylist].uri_root + playlists[currentPlaylist].entries.front().filename).c_str();

    if(isUrl){
      if(openURL(url,playlists[currentPlaylist])){
        playlists[currentPlaylist].entries.pop_front();  //remove the item which is opened for reading.
      }else{

      }
    }

    if(playlists[currentPlaylist].playlistType == LIVE){
      in.close();
      in.open(url.c_str());

     // INFO_MSG("\t\t\t\t\t\t\t\t ############ reading segment: %s for playlist: %d",url.c_str(), currentPlaylist) ;
      if(in.good()){
      playlists[currentPlaylist].entries.pop_front();  //remove the item which is opened for reading.
        return true;
      }else{
        return false;
      } 
    }else{
      currentIndex++;
      if(playlists[currentPlaylist].entries.size() <= currentIndex) {
        INFO_MSG("end of playlist reached!");
        return false;
      }else{
        in.close();
        url = playlists[currentPlaylist].uri_root + playlists[currentPlaylist].entries.at(currentIndex).filename;

       // INFO_MSG("\t\t\t\t\t\t\t\t ############ reading segment: %s for playlist: %d",url.c_str(), currentPlaylist) ;
        in.open(url.c_str());
        return true;
      }
    }
  }

  ///return the playlist id from which we need to read the first upcoming segment by timestamp. this will keep the playlists in sync while reading segments.
  int inputHLS::firstSegment(){
    bool foundSegment = false;
    int firstTimeStamp = 0;
    int tmp = 0;

    if(playlists.size() <=0){
      //do nothing, there is only one playlist, but return true when there are segments to process
      return (playlists[0].entries.size() > 0);
    }

    for(int i = 0;i<playlists.size();i++){
      if(playlists[i].entries.size() > 0){
        if(playlists[i].entries.front().timestamp < firstTimeStamp || !foundSegment){
          firstTimeStamp = playlists[i].entries.front().timestamp;
          foundSegment = true;
//          currentPlaylist = i;
          tmp = i;          
        }
      }
    }

    if(foundSegment){
      return tmp;
    }else{
      return -1;
    }
  }

  //read the next segment
  bool inputHLS::getNextSegment(){
   int tmp = 0;
   bool foundSegment = false;

   tmp = firstSegment();
   foundSegment = (tmp >= 0);
//   currentPlaylist = tmp;

    if(foundSegment){
      int segmentTime = playlists[tmp].entries.front().timestamp - Util::bootSecs();
      if(playlists[tmp].entries.front().timestamp - Util::bootSecs() > 0)
      {
        int t = playlists[tmp].entries.front().timestamp - Util::bootSecs() -1;
        while(t > 1){
          Util::wait(1000);
          t--;
          nProxy.userClient.keepAlive();
        }
//        printBuffer();
      }
    
    }else{
      VERYHIGH_MSG("no segments found!");
    }

    //first segment is set currentPlaylist with the first entry.
    return foundSegment;
  }

}

