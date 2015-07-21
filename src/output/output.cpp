#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <semaphore.h>
#include <iterator> //std::distance

#include <mist/stream.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/timing.h>
#include "output.h"

/*LTS-START*/
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
/*LTS-END*/

namespace Mist {
  JSON::Value Output::capa = JSON::Value();

  int getDTSCLen(char * mapped, long long int offset){
    return ntohl(((int*)(mapped + offset))[1]);
  }

  unsigned long long getDTSCTime(char * mapped, long long int offset){
    char * timePoint = mapped + offset + 12;
    return ((long long int)timePoint[0] << 56) | ((long long int)timePoint[1] << 48) | ((long long int)timePoint[2] << 40) | ((long long int)timePoint[3] << 32) | ((long long int)timePoint[4] << 24) | ((long long int)timePoint[5] << 16) | ((long long int)timePoint[6] << 8) | timePoint[7];
  }

  void Output::init(Util::Config * cfg){
    capa["optional"]["debug"]["name"] = "debug";
    capa["optional"]["debug"]["help"] = "The debug level at which messages need to be printed.";
    capa["optional"]["debug"]["option"] = "--debug";
    capa["optional"]["debug"]["type"] = "debug";
    capa["optional"]["startpos"]["name"] = "Starting position in live buffer";
    capa["optional"]["startpos"]["help"] = "For live, where in the buffer the stream starts playback by default. 0 = beginning, 1000 = end";
    capa["optional"]["startpos"]["option"] = "--startPos";
    capa["optional"]["startpos"]["type"] = "uint";
    cfg->addOption("startpos", JSON::fromString("{\"arg\":\"uint\",\"default\":500,\"short\":\"P\",\"long\":\"startPos\",\"help\":\"For live, where in the buffer the stream starts playback by default. 0 = beginning, 1000 = end\"}"));
  }
  
  Output::Output(Socket::Connection & conn) : myConn(conn) {
    firstTime = 0;
    crc = getpid();
    parseData = false;
    wantRequest = true;
    sought = false;
    isInitialized = false;
    isBlocking = false;
    lastStats = 0;
    maxSkipAhead = 7500;
    minSkipAhead = 5000;
    realTime = 1000;
    if (myConn){
      setBlocking(true);
    }else{
      DEBUG_MSG(DLVL_WARN, "Warning: MistOut created with closed socket!");
    }
    sentHeader = false;
  }
  
  void Output::setBlocking(bool blocking){
    isBlocking = blocking;
    myConn.setBlocking(isBlocking);
  }
  
  Output::~Output(){}

  void Output::updateMeta(){
    //read metadata from page to myMeta variable
    static char liveSemName[NAME_BUFFER_SIZE];
    snprintf(liveSemName, NAME_BUFFER_SIZE, SEM_LIVE, streamName.c_str());
    IPC::semaphore liveMeta(liveSemName, O_CREAT | O_RDWR, ACCESSPERMS, 1);
    bool lock = myMeta.live;
    if (lock){
      liveMeta.wait();
    }
    if (metaPages[0].mapped){
      DTSC::Packet tmpMeta(metaPages[0].mapped, metaPages[0].len, true);
      if (tmpMeta.getVersion()){
        myMeta.reinit(tmpMeta);
      }
    }
    if (lock){
      liveMeta.post();
    }
  }
  
  /// Called when stream initialization has failed.
  /// The standard implementation will set isInitialized to false and close the client connection,
  /// thus causing the process to exit cleanly.
  void Output::onFail(){
    isInitialized = false;
    myConn.close();
  }

  void Output::initialize(){
    if (isInitialized){
      return;
    }
    if (metaPages[0].mapped){
      return;
    }
    if (streamName.size() < 1){
      return; //abort - no stream to initialize...
    }
    isInitialized = true;
    reconnect();
    selectDefaultTracks();
    sought = false;
  }
 
  /// Connects or reconnects to the stream.
  /// Assumes streamName class member has been set already.
  /// Will start input if not currently active, calls onFail() if this does not succeed.
  /// After assuring stream is online, clears metaPages, then sets metaPages[0], statsPage and userClient to (hopefully) valid handles.
  /// Finally, calls updateMeta()
  void Output::reconnect(){
    if (!Util::startInput(streamName)){
      DEBUG_MSG(DLVL_FAIL, "Opening stream failed - aborting initalization");
      onFail();
      return;
    }
    char pageId[NAME_BUFFER_SIZE];
    snprintf(pageId, NAME_BUFFER_SIZE, SHM_STREAM_INDEX, streamName.c_str());
    metaPages.clear();
    metaPages[0].init(pageId, DEFAULT_META_PAGE_SIZE);
    if (!metaPages[0].mapped){
      DEBUG_MSG(DLVL_FAIL, "Could not connect to server for %s\n", streamName.c_str());
      onFail();
      return;
    }
    statsPage = IPC::sharedClient(SHM_STATISTICS, STAT_EX_SIZE, true);
    char userPageName[NAME_BUFFER_SIZE];
    snprintf(userPageName, NAME_BUFFER_SIZE, SHM_USERS, streamName.c_str());
    userClient = IPC::sharedClient(userPageName, PLAY_EX_SIZE, true);
    updateMeta();
  }

  void Output::selectDefaultTracks(){
    if (!isInitialized){
      initialize();
      return;
    }
    //check which tracks don't actually exist
    std::set<unsigned long> toRemove;
    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      if (!myMeta.tracks.count(*it)){
        toRemove.insert(*it);
      }
    }
    //remove those from selectedtracks
    for (std::set<unsigned long>::iterator it = toRemove.begin(); it != toRemove.end(); it++){
      selectedTracks.erase(*it);
    }
    
    //loop through all codec combinations, count max simultaneous active
    unsigned int bestSoFar = 0;
    unsigned int bestSoFarCount = 0;
    unsigned int index = 0;
    for (JSON::ArrIter it = capa["codecs"].ArrBegin(); it != capa["codecs"].ArrEnd(); it++){
      unsigned int genCounter = 0;
      unsigned int selCounter = 0;
      if ((*it).size() > 0){
        for (JSON::ArrIter itb = (*it).ArrBegin(); itb != (*it).ArrEnd(); itb++){
          if ((*itb).size() > 0){
            bool found = false;
            for (JSON::ArrIter itc = (*itb).ArrBegin(); itc != (*itb).ArrEnd() && !found; itc++){
              for (std::set<unsigned long>::iterator itd = selectedTracks.begin(); itd != selectedTracks.end(); itd++){
                if (myMeta.tracks[*itd].codec == (*itc).asStringRef()){
                  selCounter++;
                  found = true;
                  break;
                }
              }
              if (!found){
                for (std::map<unsigned int,DTSC::Track>::iterator trit = myMeta.tracks.begin(); trit != myMeta.tracks.end(); trit++){
                  if (trit->second.codec == (*itc).asStringRef()){
                    genCounter++;
                    found = true;
                    break;
                  }
                }
              }
            }
          }
        }
        if (selCounter == selectedTracks.size()){
          if (selCounter + genCounter > bestSoFarCount){
            bestSoFarCount = selCounter + genCounter;
            bestSoFar = index;
            DEBUG_MSG(DLVL_HIGH, "Match (%u/%u): %s", selCounter, selCounter+genCounter, (*it).toString().c_str());
          }
        }else{
          DEBUG_MSG(DLVL_VERYHIGH, "Not a match for currently selected tracks: %s", (*it).toString().c_str());
        }
      }
      index++;
    }
    
    DEBUG_MSG(DLVL_MEDIUM, "Trying to fill: %s", capa["codecs"][bestSoFar].toString().c_str());
    //try to fill as many codecs simultaneously as possible
    if (capa["codecs"][bestSoFar].size() > 0){
      for (JSON::ArrIter itb = capa["codecs"][bestSoFar].ArrBegin(); itb != capa["codecs"][bestSoFar].ArrEnd(); itb++){
        if ((*itb).size() && myMeta.tracks.size()){
          bool found = false;
          for (JSON::ArrIter itc = (*itb).ArrBegin(); itc != (*itb).ArrEnd() && !found; itc++){
            for (std::set<unsigned long>::iterator itd = selectedTracks.begin(); itd != selectedTracks.end(); itd++){
              if (myMeta.tracks[*itd].codec == (*itc).asStringRef()){
                found = true;
                break;
              }
            }
            if (!found){
              for (std::map<unsigned int,DTSC::Track>::iterator trit = myMeta.tracks.begin(); trit != myMeta.tracks.end(); trit++){
                if (trit->second.codec == (*itc).asStringRef()){
                  selectedTracks.insert(trit->first);
                  found = true;
                  break;
                }
              }
            }
          }
        }
      }
    }
    
    if (Util::Config::printDebugLevel >= DLVL_MEDIUM){
      //print the selected tracks
      std::stringstream selected;
      if (selectedTracks.size()){
        for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
          if (it != selectedTracks.begin()){
            selected << ", ";
          }
          selected << (*it);
        }
      }
      DEBUG_MSG(DLVL_MEDIUM, "Selected tracks: %s (%lu)", selected.str().c_str(), selectedTracks.size());    
    }
    
  }
  
  /// Clears the buffer, sets parseData to false, and generally makes not very much happen at all.
  void Output::stop(){
    buffer.clear();
    parseData = false;
  }
  
  unsigned int Output::getKeyForTime(long unsigned int trackId, long long timeStamp){
    DTSC::Track & trk = myMeta.tracks[trackId];
    if (!trk.keys.size()){
      return 0;
    }
    unsigned int keyNo = trk.keys.begin()->getNumber();
    unsigned int partCount = 0;
    std::deque<DTSC::Key>::iterator it;
    for (it = trk.keys.begin(); it != trk.keys.end() && it->getTime() <= timeStamp; it++){
      keyNo = it->getNumber();
      partCount += it->getParts();
    }
    //if the time is before the next keyframe but after the last part, correctly seek to next keyframe
    if (partCount && it != trk.keys.end() && timeStamp > it->getTime() - trk.parts[partCount-1].getDuration()){
      ++keyNo;
    }
    return keyNo;
  }
  
  int Output::pageNumForKey(long unsigned int trackId, long long int keyNum){
    if (!metaPages.count(trackId)){
      char id[NAME_BUFFER_SIZE];
      snprintf(id, NAME_BUFFER_SIZE, SHM_TRACK_INDEX, streamName.c_str(), trackId);
      metaPages[trackId].init(id, 8 * 1024);
    }
    int len = metaPages[trackId].len / 8;
    for (int i = 0; i < len; i++){
      int * tmpOffset = (int *)(metaPages[trackId].mapped + (i * 8));
      long amountKey = ntohl(tmpOffset[1]);
      if (amountKey == 0){continue;}
      long tmpKey = ntohl(tmpOffset[0]);
      if (tmpKey <= keyNum && ((tmpKey?tmpKey:1) + amountKey) > keyNum){
        return tmpKey;
      }
    }
    return -1;
  }
  
  void Output::loadPageForKey(long unsigned int trackId, long long int keyNum){
    if (myMeta.vod && keyNum > myMeta.tracks[trackId].keys.rbegin()->getNumber()){
      curPage.erase(trackId);
      currKeyOpen.erase(trackId);
      return;
    }
    DEBUG_MSG(DLVL_VERYHIGH, "Loading track %lu, containing key %lld", trackId, keyNum);
    unsigned int timeout = 0;
    unsigned long pageNum = pageNumForKey(trackId, keyNum);
    while (pageNum == -1){
      if (!timeout){
        DEBUG_MSG(DLVL_HIGH, "Requesting page with key %lu:%lld", trackId, keyNum);
      }
      ++timeout;
      //if we've been waiting for this page for 3 seconds, reconnect to the stream - something might be going wrong...
      if (timeout == 30){
        DEVEL_MSG("Loading is taking longer than usual, reconnecting to stream %s...", streamName.c_str());
        reconnect();
      }
      if (timeout > 100){
        DEBUG_MSG(DLVL_FAIL, "Timeout while waiting for requested page %lld for track %lu. Aborting.", keyNum, trackId);
        curPage.erase(trackId);
        currKeyOpen.erase(trackId);
        return;
      }
      if (keyNum){
        nxtKeyNum[trackId] = keyNum-1;
      }else{
        nxtKeyNum[trackId] = 0;
      }
      stats();
      Util::wait(100);
      pageNum = pageNumForKey(trackId, keyNum);
    }
    
    if (keyNum){
      nxtKeyNum[trackId] = keyNum-1;
    }else{
      nxtKeyNum[trackId] = 0;
    }
    stats();
    nxtKeyNum[trackId] = pageNum;
    
    if (currKeyOpen.count(trackId) && currKeyOpen[trackId] == (unsigned int)pageNum){
      return;
    }
    char id[NAME_BUFFER_SIZE];
    snprintf(id, NAME_BUFFER_SIZE, SHM_TRACK_DATA, streamName.c_str(), trackId, pageNum);
    curPage[trackId].init(id, DEFAULT_DATA_PAGE_SIZE);
    if (!(curPage[trackId].mapped)){
      DEBUG_MSG(DLVL_FAIL, "Initializing page %s failed", curPage[trackId].name.c_str());
      return;
    }
    currKeyOpen[trackId] = pageNum;
  }
  
  /// Prepares all tracks from selectedTracks for seeking to the specified ms position.
  void Output::seek(unsigned long long pos){
    sought = true;
    firstTime = Util::getMS() - pos;
    if (!isInitialized){
      initialize();
    }
    buffer.clear();
    thisPacket.null();
    if (myMeta.live){
      updateMeta();
    }
    DEBUG_MSG(DLVL_MEDIUM, "Seeking to %llums", pos);
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      seek(*it, pos);
    }
  }

  bool Output::seek(unsigned int tid, unsigned long long pos, bool getNextKey){
    loadPageForKey(tid, getKeyForTime(tid, pos) + (getNextKey?1:0));
    if (!curPage.count(tid) || !curPage[tid].mapped){
      DEBUG_MSG(DLVL_DEVEL, "Aborting seek to %llums in track %u, not available.", pos, tid);
      return false;
    }
    sortedPageInfo tmp;
    tmp.tid = tid;
    tmp.offset = 0;
    DTSC::Packet tmpPack;
    tmpPack.reInit(curPage[tid].mapped + tmp.offset, 0, true);
    tmp.time = tmpPack.getTime();
    char * mpd = curPage[tid].mapped;
    while ((long long)tmp.time < pos && tmpPack){
      tmp.offset += tmpPack.getDataLen();
      tmpPack.reInit(mpd + tmp.offset, 0, true);
      tmp.time = tmpPack.getTime();
    }
    if (tmpPack){
      buffer.insert(tmp);
      return true;
    }else{
      //don't print anything for empty packets - not sign of corruption, just unfinished stream.
      if (curPage[tid].mapped[tmp.offset] != 0){
        DEBUG_MSG(DLVL_FAIL, "Noes! Couldn't find packet on track %d because of some kind of corruption error or somesuch.", tid);
      }else{
        DEBUG_MSG(DLVL_FAIL, "Track %d no data (key %u @ %u) - waiting...", tid, getKeyForTime(tid, pos) + (getNextKey?1:0), tmp.offset);
        unsigned int i = 0;
        while (curPage[tid].mapped[tmp.offset] == 0 && ++i < 42){
          Util::wait(100);
        }
        if (curPage[tid].mapped[tmp.offset] == 0){
          DEBUG_MSG(DLVL_FAIL, "Track %d no data (key %u) - timeout", tid, getKeyForTime(tid, pos) + (getNextKey?1:0));
        }else{
          return seek(tid, pos, getNextKey);
        }
      }
      return false;
    }
  }

  /*LTS-START*/
  bool Output::onList(std::string ip, std::string list){
    if (list == ""){
      return false;
    }
    std::string entry;
    std::string lowerIpv6;//lower-case
    std::string upperIpv6;//full-caps
    do{
      entry = list.substr(0,list.find(" "));//make sure we have a single entry
      lowerIpv6 = "::ffff:" + entry;
      upperIpv6 = "::FFFF:" + entry;
      if (entry == ip || lowerIpv6 == ip || upperIpv6 == ip){
        return true;
      }
      long long unsigned int starPos = entry.find("*");
      if (starPos == std::string::npos){
        if (ip == entry){
          return true;
        }
      }else{
        if (starPos == 0){//beginning of the filter
          if (ip.substr(ip.length() - entry.size() - 1) == entry.substr(1)){
            return true;
          }
        }else{
          if (starPos == entry.size() - 1){//end of the filter
            if (ip.find(entry.substr(0, entry.size() - 1)) == 0 ){
              return true;
            }
            if (ip.find(entry.substr(0, lowerIpv6.size() - 1)) == 0 ){
              return true;
            }
            if (ip.find(entry.substr(0, upperIpv6.size() - 1)) == 0 ){
              return true;
            }
          }else{
            Log("CONF","Invalid list entry detected: " + entry);
          }
        }
      }
      list.erase(0, entry.size() + 1);
    }while (list != "");
    return false;
  }

  void Output::Log(std::string type, std::string message){
    /// \todo These logs need to show up in the controller.
    /// \todo Additionally, the triggering and untriggering of limits should be recorded in the controller as well.
    if (type == "HLIM"){
      DEBUG_MSG(DLVL_HIGH, "HardLimit Triggered: %s", message.c_str());
    }
    if (type == "SLIM"){
      DEBUG_MSG(DLVL_HIGH, "SoftLimit Triggered: %s", message.c_str());
    }
  }
  
  std::string Output::hostLookup(std::string ip){
    struct sockaddr_in6 sa;
    char hostName[1024];
    char service[20];
    if (inet_pton(AF_INET6, ip.c_str(), &(sa.sin6_addr)) != 1){
      return "\n";
    }
    sa.sin6_family = AF_INET6;
    sa.sin6_port = 0;
    sa.sin6_flowinfo = 0;
    sa.sin6_scope_id = 0;
    int tmpRet = getnameinfo((struct sockaddr*)&sa, sizeof sa, hostName, sizeof hostName, service, sizeof service, NI_NAMEREQD );
    if ( tmpRet == 0){
      return hostName;
    }
    return "";
  }
  
  bool Output::isBlacklisted(std::string host, std::string streamName, int timeConnected){
    return false;//blacklisting temporarily disabled for performance reasons
    JSON::Value Storage = JSON::fromFile(Util::getTmpFolder() + "streamlist");
    std::string myHostName = hostLookup(host);
    if (myHostName == "\n"){
      return false;
    }
    std::string myCountryName = getCountry(host);
    JSON::ArrIter limitIt;
    bool hasWhitelist = false;
    bool hostOnWhitelist = false;
    if (Storage["streams"].isMember(streamName)){
      if (Storage["streams"][streamName].isMember("limits") && Storage["streams"][streamName]["limits"].size()){
        for (limitIt = Storage["streams"][streamName]["limits"].ArrBegin(); limitIt != Storage["streams"][streamName]["limits"].ArrEnd(); limitIt++){
          if ((*limitIt)["name"].asString() == "host"){
            if ((*limitIt)["value"].asString()[0] == '+'){
              if (!onList(host, (*limitIt)["value"].asString().substr(1))){
                if (myHostName == ""){
                  if (timeConnected > Storage["config"]["limit_timeout"].asInt()){
                    return true;
                  }
                }else{
                  if ( !onList(myHostName, (*limitIt)["value"].asStringRef().substr(1))){
                    if ((*limitIt)["type"].asStringRef() == "hard"){
                      Log("HLIM", "Host " + host + " not whitelisted for stream " + streamName);
                      return true;
                    }else{
                      Log("SLIM", "Host " + host + " not whitelisted for stream " + streamName);
                    }
                  }
                }
              }
            }else{
              if ((*limitIt)["value"].asStringRef().size() > 1 && (*limitIt)["value"].asStringRef()[0] == '-'){
                if (onList(host, (*limitIt)["value"].asStringRef().substr(1))){
                  if ((*limitIt)["type"].asStringRef() == "hard"){
                    Log("HLIM", "Host " + host + " blacklisted for stream " + streamName);
                    return true;
                  }else{
                    Log("SLIM", "Host " + host + " blacklisted for stream " + streamName);
                  }
                }
                if (myHostName != "" && onList(myHostName, (*limitIt)["value"].asString().substr(1))){
                  if ((*limitIt)["type"].asStringRef() == "hard"){
                    Log("HLIM", "Host " + myHostName + " blacklisted for stream " + streamName);
                    return true;
                  }else{
                    Log("SLIM", "Host " + myHostName + " blacklisted for stream " + streamName);
                  }
                }
              }
            }
          }
          if ((*limitIt)["name"].asString() == "geo"){
            if ((*limitIt)["value"].asString()[0] == '+'){
              if (myCountryName == ""){
                if ((*limitIt)["type"].asString() == "hard"){
                  Log("HLIM", "Host " + host + " with unknown location blacklisted for stream " + streamName);
                  return true;
                }else{
                  Log("SLIM", "Host " + host + " with unknown location blacklisted for stream " + streamName);
                }
              }
              if (!onList(myCountryName, (*limitIt)["value"].asString().substr(1))){
                if ((*limitIt)["type"].asString() == "hard"){
                  Log("HLIM", "Host " + host + " with location " + myCountryName + " not whitelisted for stream " + streamName);
                  return true;
                }else{
                  Log("SLIM", "Host " + host + " with location " + myCountryName + " not whitelisted for stream " + streamName);
                }
              }
            }else{
              if ((*limitIt)["value"].asString()[0] == '-'){
                if (onList(myCountryName, (*limitIt)["value"].asString().substr(1))){
                  if ((*limitIt)["type"].asString() == "hard"){
                    Log("HLIM", "Host " + host + " with location " + myCountryName + " blacklisted for stream " + streamName);
                    return true;
                  }else{
                    Log("SLIM", "Host " + host + " with location " + myCountryName + " blacklisted for stream " + streamName);
                  }
                }
              }
            }
          }
        }
      }
    }
    if (Storage["config"]["limits"].size()){
      for (limitIt = Storage["config"]["limits"].ArrBegin(); limitIt != Storage["config"]["limits"].ArrEnd(); limitIt++){
        if ((*limitIt)["name"].asString() == "host"){
          if ((*limitIt)["value"].asString()[0] == '+'){
            if (!onList(host, (*limitIt)["value"].asString().substr(1))){
              if (myHostName == ""){
                if (timeConnected > Storage["config"]["limit_timeout"].asInt()){
                  return true;
                }
              }else{
                if ( !onList(myHostName, (*limitIt)["value"].asString().substr(1))){
                  if ((*limitIt)["type"].asString() == "hard"){
                    Log("HLIM", "Host " + host + " not whitelisted for stream " + streamName);
                    return true;
                  }else{
                    Log("SLIM", "Host " + host + " not whitelisted for stream " + streamName);
                  }
                }
              }
            }
          }else{
            if ((*limitIt)["value"].asString()[0] == '-'){
              if (onList(host, (*limitIt)["value"].asString().substr(1))){
                if ((*limitIt)["type"].asString() == "hard"){
                  Log("HLIM", "Host " + host + " blacklisted for stream " + streamName);
                  return true;
                }else{
                  Log("SLIM", "Host " + host + " blacklisted for stream " + streamName);
                }
              }
              if (myHostName != "" && onList(myHostName, (*limitIt)["value"].asString().substr(1))){
                if ((*limitIt)["type"].asString() == "hard"){
                  Log("HLIM", "Host " + myHostName + " blacklisted for stream " + streamName);
                  return true;
                }else{
                  Log("SLIM", "Host " + myHostName + " blacklisted for stream " + streamName);
                }
              }
            }
          }
        }
        if ((*limitIt)["name"].asString() == "geo"){
          if ((*limitIt)["value"].asString()[0] == '+'){
            if (myCountryName == ""){
              if ((*limitIt)["type"].asString() == "hard"){
                Log("HLIM", "Host " + host + " with unknown location blacklisted for stream " + streamName);
                return true;
              }else{
                Log("SLIM", "Host " + host + " with unknown location blacklisted for stream " + streamName);
              }
            }
            if (!onList(myCountryName, (*limitIt)["value"].asString().substr(1))){
              if ((*limitIt)["type"].asString() == "hard"){
                Log("HLIM", "Host " + host + " with location " + myCountryName + " not whitelisted for stream " + streamName);
                return true;
              }else{
                Log("SLIM", "Host " + host + " with location " + myCountryName + " not whitelisted for stream " + streamName);
              }
            }
          }else{
            if ((*limitIt)["value"].asStringRef().size() > 1 && (*limitIt)["value"].asStringRef()[0] == '-'){
              if (onList(myCountryName, (*limitIt)["value"].asStringRef().substr(1))){
                if ((*limitIt)["type"].asString() == "hard"){
                  Log("HLIM", "Host " + host + " with location " + myCountryName + " blacklisted for stream " + streamName);
                  return true;
                }else{
                  Log("SLIM", "Host " + host + " with location " + myCountryName + " blacklisted for stream " + streamName);
                }
              }
            }
          }
        }
      }
    }
    if (hasWhitelist){
      if (hostOnWhitelist || myHostName == ""){
        return false;
      }else{
        return true;
      }
    }
    return false;
  }

  #ifdef GEOIP
  GeoIP * Output::geoIP4 = 0;
  GeoIP * Output::geoIP6 = 0;
  #endif
  std::string Output::getCountry(std::string ip){
    char * code = NULL;
    #ifdef GEOIP
    if (geoIP4){
      code = (char*)GeoIP_country_code_by_addr(geoIP4, ip.c_str());
    }
    if (!code && geoIP6){
      code = (char*)GeoIP_country_code_by_addr_v6(geoIP6, ip.c_str());
    }
    #endif
    if (!code){
      return "";
    }
    return code;
  }
  /*LTS-END*/
  
  void Output::requestHandler(){
    static bool firstData = true;//only the first time, we call onRequest if there's data buffered already.
    if ((firstData && myConn.Received().size()) || myConn.spool()){
      firstData = false;
      DEBUG_MSG(DLVL_DONTEVEN, "onRequest");
      onRequest();
    }else{
      if (!isBlocking && !parseData){
        Util::sleep(500);
      }
    }
  }
 
  int Output::run() {
    DEBUG_MSG(DLVL_MEDIUM, "MistOut client handler started");
    while (config->is_active && myConn.connected() && (wantRequest || parseData)){
      stats();
      if (wantRequest){
        requestHandler();
      }
      if (parseData){
        if (!isInitialized){
          initialize();
        }
        if ( !sentHeader){
          DEBUG_MSG(DLVL_DONTEVEN, "sendHeader");
          sendHeader();
        }
        prepareNext();
        if (thisPacket){
          sendNext();
        }else{
          if (!onFinish()){
            break;
          }
        }
      }
    }
    DEBUG_MSG(DLVL_MEDIUM, "MistOut client handler shutting down: %s, %s, %s", myConn.connected() ? "conn_active" : "conn_closed", wantRequest ? "want_request" : "no_want_request", parseData ? "parsing_data" : "not_parsing_data");
    stats();
    userClient.finish();
    statsPage.finish();
    myConn.close();
    return 0;
  }
  
  /// Returns the ID of the main selected track, or 0 if no tracks are selected.
  /// The main track is the first video track, if any, and otherwise the first other track.
  long unsigned int Output::getMainSelectedTrack(){
    if (!selectedTracks.size()){
      return 0;
    }
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      if (myMeta.tracks[*it].type == "video"){
        return *it;
      }
    }
    return *(selectedTracks.begin());
  }
  
  void Output::prepareNext(){
    static int nonVideoCount = 0;
    if (!sought){
      if (myMeta.live){
        long unsigned int mainTrack = getMainSelectedTrack();
        if (myMeta.tracks[mainTrack].keys.size() < 2){
          if (!myMeta.tracks[mainTrack].keys.size()){
            myConn.close();
            return;
          }else{
            seek(myMeta.tracks[mainTrack].keys.begin()->getTime());
            prepareNext();
            return;
          }
        }
        unsigned long long seekPos = myMeta.tracks[mainTrack].keys.rbegin()->getTime();
        if (seekPos < 5000){
          seekPos = 0;
        }
        seek(seekPos);
      }else{
        seek(0);
      }
    }
    static unsigned int emptyCount = 0;
    if (!buffer.size()){
      thisPacket.null();
      DEBUG_MSG(DLVL_DEVEL, "Buffer completely played out");
      onFinish();
      return;
    }
    sortedPageInfo nxt = *(buffer.begin());
    buffer.erase(buffer.begin());

    DEBUG_MSG(DLVL_DONTEVEN, "Loading track %u (next=%lu), %llu ms", nxt.tid, nxtKeyNum[nxt.tid], nxt.time);
    
    if (nxt.offset >= curPage[nxt.tid].len){
      nxtKeyNum[nxt.tid] = getKeyForTime(nxt.tid, thisPacket.getTime());
      loadPageForKey(nxt.tid, ++nxtKeyNum[nxt.tid]);
      nxt.offset = 0;
      if (curPage.count(nxt.tid) && curPage[nxt.tid].mapped){
        if (getDTSCTime(curPage[nxt.tid].mapped, nxt.offset) < nxt.time){
          ERROR_MSG("Time going backwards in track %u - dropping track.", nxt.tid);
        }else{
          nxt.time = getDTSCTime(curPage[nxt.tid].mapped, nxt.offset);
          buffer.insert(nxt);
        }
        prepareNext();
        return;
      }
    }
    
    if (!curPage.count(nxt.tid) || !curPage[nxt.tid].mapped){
      //mapping failure? Drop this track and go to next.
      //not an error - usually means end of stream.
      DEBUG_MSG(DLVL_DEVEL, "Track %u no page - dropping track.", nxt.tid);
      prepareNext();
      return;
    }
    
    //have we arrived at the end of the memory page? (4 zeroes mark the end)
    if (!memcmp(curPage[nxt.tid].mapped + nxt.offset, "\000\000\000\000", 4)){
      //if we don't currently know where we are, we're lost. We should drop the track.
      if (!nxt.time){
        DEBUG_MSG(DLVL_DEVEL, "Timeless empty packet on track %u - dropping track.", nxt.tid);
        prepareNext();
        return;
      }
      //if this is a live stream, we might have just reached the live point.
      //check where the next key is
      int nextPage = pageNumForKey(nxt.tid, nxtKeyNum[nxt.tid]+1);
      //are we live, and the next key hasn't shown up on another page? then we're waiting.
      if (myMeta.live && currKeyOpen.count(nxt.tid) && (currKeyOpen[nxt.tid] == (unsigned int)nextPage || nextPage == -1)){
        if (myMeta && ++emptyCount < 42){
          //we're waiting for new data. Simply retry.
          buffer.insert(nxt);
        }else{
          //after ~10 seconds, give up and drop the track.
          DEBUG_MSG(DLVL_DEVEL, "Empty packet on track %u @ key %lu (next=%d) - could not reload, dropping track.", nxt.tid, nxtKeyNum[nxt.tid]+1, nextPage);
        }
        //keep updating the metadata at 250ms intervals while waiting for more data
        Util::sleep(250);
        updateMeta();
      }else{
        //if we're not live, we've simply reached the end of the page. Load the next key.
        nxtKeyNum[nxt.tid] = getKeyForTime(nxt.tid, thisPacket.getTime());
        loadPageForKey(nxt.tid, ++nxtKeyNum[nxt.tid]);
        nxt.offset = 0;
        if (curPage.count(nxt.tid) && curPage[nxt.tid].mapped){
          unsigned long long nextTime = getDTSCTime(curPage[nxt.tid].mapped, nxt.offset);
          if (nextTime && nextTime < nxt.time){
            DEBUG_MSG(DLVL_DEVEL, "Time going backwards in track %u - dropping track.", nxt.tid);
          }else{
            if (nextTime){
              nxt.time = nextTime;
            }
            buffer.insert(nxt);
            DEBUG_MSG(DLVL_MEDIUM, "Next page for track %u starts at %llu.", nxt.tid, nxt.time);
          }
        }else{
          DEBUG_MSG(DLVL_DEVEL, "Could not load next memory page for track %u - dropping track.", nxt.tid);
        }
      }
      prepareNext();
      return;
    }
    thisPacket.reInit(curPage[nxt.tid].mapped + nxt.offset, 0, true);
    if (thisPacket){
      if (thisPacket.getTime() != nxt.time && nxt.time){
        DEBUG_MSG(DLVL_MEDIUM, "ACTUALLY Loaded track %ld (next=%lu), %llu ms", thisPacket.getTrackId(), nxtKeyNum[nxt.tid], thisPacket.getTime());
      }
      if ((myMeta.tracks[nxt.tid].type == "video" && thisPacket.getFlag("keyframe")) || (++nonVideoCount % 30 == 0)){
        if (myMeta.live){
          updateMeta();
        }
        nxtKeyNum[nxt.tid] = getKeyForTime(nxt.tid, thisPacket.getTime());
        DEBUG_MSG(DLVL_VERYHIGH, "Track %u @ %llums = key %lu", nxt.tid, thisPacket.getTime(), nxtKeyNum[nxt.tid]);
      }
      emptyCount = 0;
    }
    nxt.offset += thisPacket.getDataLen();
    if (realTime){
      while (nxt.time > (((Util::getMS() - firstTime)*1000)+maxSkipAhead)/realTime) {
        Util::sleep(nxt.time - (((Util::getMS() - firstTime)*1000)+minSkipAhead)/realTime);
      }
    }
    if (curPage[nxt.tid]){
      if (nxt.offset < curPage[nxt.tid].len){
        unsigned long long nextTime = getDTSCTime(curPage[nxt.tid].mapped, nxt.offset);
        if (nextTime){
          nxt.time = nextTime;
        }else{
          ++nxt.time;
        }
      }
      buffer.insert(nxt);
    }
    stats();
  }

  void Output::stats(){
    static bool setHost = true;
    if (!isInitialized){
      return;
    }
    if (statsPage.getData()){
      unsigned long long int now = Util::epoch();
      if (now != lastStats){
        /*LTS-START*/
        if (statsPage.getData()[-1] > 127){
          myConn.close();
          return;
        }
        /*LTS-END*/
        lastStats = now;
        IPC::statExchange tmpEx(statsPage.getData());
        tmpEx.now(now);
        if (setHost){
          tmpEx.host(myConn.getBinHost());
          setHost = false;
        }
        tmpEx.crc(crc);
        tmpEx.streamName(streamName);
        tmpEx.connector(capa["name"].asString());
        tmpEx.up(myConn.dataUp());
        tmpEx.down(myConn.dataDown());
        tmpEx.time(now - myConn.connTime());
        if (thisPacket){
          tmpEx.lastSecond(thisPacket.getTime());
        }else{
          tmpEx.lastSecond(0);
        }
        statsPage.keepAlive();
      }
    }
    int tNum = 0;
    if (!userClient.getData()){
      char userPageName[NAME_BUFFER_SIZE];
      snprintf(userPageName, NAME_BUFFER_SIZE, SHM_USERS, streamName.c_str());
      userClient = IPC::sharedClient(userPageName, PLAY_EX_SIZE, true);
      if (!userClient.getData()){
        DEBUG_MSG(DLVL_WARN, "Player connection failure - aborting output");
        myConn.close();
        return;
      }
    }
    if (!trackMap.size()){
      for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end() && tNum < SIMUL_TRACKS; it++){
        unsigned int tId = *it;
        char * thisData = userClient.getData() + (6 * tNum);
        thisData[0] = ((tId >> 24) & 0xFF);
        thisData[1] = ((tId >> 16) & 0xFF);
        thisData[2] = ((tId >> 8) & 0xFF);
        thisData[3] = ((tId) & 0xFF);
        thisData[4] = ((nxtKeyNum[tId] >> 8) & 0xFF);
        thisData[5] = ((nxtKeyNum[tId]) & 0xFF);
        tNum ++;
      }
    }
    userClient.keepAlive();
    if (tNum > SIMUL_TRACKS){
      DEBUG_MSG(DLVL_WARN, "Too many tracks selected, using only first %d", SIMUL_TRACKS);
    }
  }
  
  void Output::onRequest(){
    //simply clear the buffer, we don't support any kind of input by default
    myConn.Received().clear();
    wantRequest = false;
  }

  void Output::sendHeader(){
    //just set the sentHeader bool to true, by default
    sentHeader = true;
  }
}
