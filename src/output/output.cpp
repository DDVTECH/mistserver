#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <semaphore.h>
#include <iterator> //std::distance

#include <mist/bitfields.h>
#include <mist/stream.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/timing.h>
#include "output.h"

/*LTS-START*/
#include <mist/triggers.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
/*LTS-END*/

namespace Mist {
  JSON::Value Output::capa = JSON::Value();

  int getDTSCLen(char * mapped, long long int offset){
    return Bit::btohl(mapped + offset + 4);
  }

  unsigned long long getDTSCTime(char * mapped, long long int offset){
    return Bit::btohll(mapped + offset + 12);
  }

  void Output::init(Util::Config * cfg){
    capa["optional"]["debug"]["name"] = "debug";
    capa["optional"]["debug"]["help"] = "The debug level at which messages need to be printed.";
    capa["optional"]["debug"]["option"] = "--debug";
    capa["optional"]["debug"]["type"] = "debug";
    capa["optional"]["startpos"]["name"] = "Starting position in live buffer";
    capa["optional"]["startpos"]["help"] = "For live, where in the buffer the stream starts playback by default. 0 = beginning, 1000 = end";
    capa["optional"]["startpos"]["option"] = "--startPos";
    capa["optional"]["startpos"]["short"] = "P";
    capa["optional"]["startpos"]["default"] = (long long)500;
    capa["optional"]["startpos"]["type"] = "uint";
  }
  
  Output::Output(Socket::Connection & conn) : myConn(conn) {
    firstTime = 0;
    crc = getpid();
    parseData = false;
    wantRequest = true;
    sought = false;
    isInitialized = false;
    isBlocking = false;
    completeKeysOnly = false;
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
  
  void Output::updateMeta(){
    //read metadata from page to myMeta variable
    if (nProxy.metaPages[0].mapped){
      IPC::semaphore * liveSem = 0;
      if (!myMeta.vod){
        static char liveSemName[NAME_BUFFER_SIZE];
        snprintf(liveSemName, NAME_BUFFER_SIZE, SEM_LIVE, streamName.c_str());
        liveSem = new IPC::semaphore(liveSemName, O_RDWR, ACCESSPERMS, 1);
        if (*liveSem){
          liveSem->wait();
        }else{
          delete liveSem;
          liveSem = 0;
        }
      }
      DTSC::Packet tmpMeta(nProxy.metaPages[0].mapped, nProxy.metaPages[0].len, true);
      if (tmpMeta.getVersion()){
        myMeta.reinit(tmpMeta);
      }
      if (liveSem){
        liveSem->post();
        delete liveSem;
        liveSem = 0;
      }
    }
  }
  
  /// Called when stream initialization has failed.
  /// The standard implementation will set isInitialized to false and close the client connection,
  /// thus causing the process to exit cleanly.
  void Output::onFail(){
    isInitialized = false;
    wantRequest = true;
    parseData= false;
    streamName.clear();
    myConn.close();
  }

  /// \triggers 
  /// The `"CONN_PLAY"` trigger is stream-specific, and is ran when an active connection first opens a stream. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// connected client host
  /// output handler name
  /// request URL (if any)
  /// ~~~~~~~~~~~~~~~
  /// The `"USER_NEW"` trigger is stream-specific, and is ran when a new user first opens a stream. Segmented protcols are unduplicated over the duration of the statistics log (~10 minutes), true streaming protocols (RTMP, RTSP) are not deduplicated as no duplication ever takes place. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// connected client host
  /// User agent checksum (CRC32 of User-agent string)
  /// output handler name
  /// request URL (if any)
  /// ~~~~~~~~~~~~~~~
  void Output::initialize(){
    if (isInitialized){
      return;
    }
    if (nProxy.metaPages[0].mapped){
      return;
    }
    if (streamName.size() < 1){
      return; //abort - no stream to initialize...
    }
    isInitialized = true;
    reconnect();
    //if the connection failed, fail
    if (streamName.size() < 1){
      onFail();
      return;
    }
    selectDefaultTracks();
    sought = false;
    /*LTS-START*/
    if(Triggers::shouldTrigger("CONN_PLAY", streamName)){
      std::string payload = streamName+"\n" + getConnectedHost() +"\n"+capa["name"].asStringRef()+"\n"+reqUrl;
      if (!Triggers::doTrigger("CONN_PLAY", payload, streamName)){
        myConn.close();
      }
    }
    doSync(true);
    /*LTS-END*/
  }

  /// If called with force set to true and a USER_NEW trigger enabled, forces a sync immediately.
  /// Otherwise, does nothing unless the sync byte is set to 2, in which case it forces a sync as well.
  /// May be called recursively because it calls stats() which calls this function.
  /// If this happens, the extra calls to the function return instantly.
  void Output::doSync(bool force){
    static bool recursing = false;
    if (recursing){return;}
    recursing = true;
    IPC::statExchange tmpEx(statsPage.getData());
    if (tmpEx.getSync() == 2 || force){
      if (getStatsName() == capa["name"].asStringRef() && Triggers::shouldTrigger("USER_NEW", streamName)){
        //sync byte 0 = no sync yet, wait for sync from controller...
        unsigned int i = 0;
        tmpEx.setSync(0);
        //wait max 10 seconds for sync
        while ((!tmpEx.getSync() || tmpEx.getSync() == 2) && i++ < 100){
          Util::wait(100);
          stats(true);
          tmpEx = IPC::statExchange(statsPage.getData());
        }
        HIGH_MSG("USER_NEW sync achieved: %u", (unsigned int)tmpEx.getSync());
        //1 = check requested (connection is new)
        if (tmpEx.getSync() == 1){
          std::string payload = streamName+"\n" + getConnectedHost() +"\n" + JSON::Value((long long)crc).asString() + "\n"+capa["name"].asStringRef()+"\n"+reqUrl;
          if (!Triggers::doTrigger("USER_NEW", payload, streamName)){
            MEDIUM_MSG("Closing connection because denied by USER_NEW trigger");
            myConn.close();
            tmpEx.setSync(100);//100 = denied
          }else{
            tmpEx.setSync(10);//10 = accepted
          }
        }
        //100 = denied
        if (tmpEx.getSync() == 100){
          myConn.close();
          MEDIUM_MSG("Closing connection because denied by USER_NEW sync byte");
        }
        if (tmpEx.getSync() == 0 || tmpEx.getSync() == 2){
          myConn.close();
          FAIL_MSG("Closing connection because sync byte timeout!");
        }
        //anything else = accepted
      }else{
        tmpEx.setSync(10);//auto-accept if no trigger
      }
    }
    recursing = false;
  }

  std::string Output::getConnectedHost(){
    return myConn.getHost();
  }

  std::string Output::getConnectedBinHost(){
    return myConn.getBinHost();
  }
 
  bool Output::isReadyForPlay() {
    if (myMeta.tracks.size()){
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        if (it->second.keys.size() >= 2){
          return true;
        }
      }
    }
    return false;
  }
  /// Connects or reconnects to the stream.
  /// Assumes streamName class member has been set already.
  /// Will start input if not currently active, calls onFail() if this does not succeed.
  /// After assuring stream is online, clears nProxy.metaPages, then sets nProxy.metaPages[0], statsPage and nProxy.userClient to (hopefully) valid handles.
  /// Finally, calls updateMeta()
  void Output::reconnect(){
    if (!Util::startInput(streamName)){
      FAIL_MSG("Opening stream %s failed - aborting initalization", streamName.c_str());
      onFail();
      return;
    }
    if (statsPage.getData()){
      statsPage.finish();
    }
    statsPage = IPC::sharedClient(SHM_STATISTICS, STAT_EX_SIZE, true);
    if (nProxy.userClient.getData()){
      nProxy.userClient.finish();
    }
    char userPageName[NAME_BUFFER_SIZE];
    snprintf(userPageName, NAME_BUFFER_SIZE, SHM_USERS, streamName.c_str());
    nProxy.userClient = IPC::sharedClient(userPageName, PLAY_EX_SIZE, true);
    char pageId[NAME_BUFFER_SIZE];
    snprintf(pageId, NAME_BUFFER_SIZE, SHM_STREAM_INDEX, streamName.c_str());
    nProxy.metaPages.clear();
    nProxy.metaPages[0].init(pageId, DEFAULT_STRM_PAGE_SIZE);
    if (!nProxy.metaPages[0].mapped){
      FAIL_MSG("Could not connect to server for %s", streamName.c_str());
      onFail();
      return;
    }
    stats(true);
    updateMeta();
    if (myMeta.live && !isReadyForPlay()){
      unsigned long long waitUntil = Util::epoch() + 15;
      while (!isReadyForPlay()){
        if (Util::epoch() > waitUntil){
          INFO_MSG("Giving up waiting for playable tracks. Stream: %s, IP: %s", streamName.c_str(), getConnectedHost().c_str());
          break;
        }
        Util::wait(750);
        stats();
        updateMeta();
      }
    }
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
    jsonForEach(capa["codecs"], it) {
      unsigned int genCounter = 0;
      unsigned int selCounter = 0;
      if ((*it).size() > 0){
        jsonForEach((*it), itb) {
          if ((*itb).size() > 0){
            bool found = false;
            jsonForEach(*itb, itc) {
              for (std::set<unsigned long>::iterator itd = selectedTracks.begin(); itd != selectedTracks.end(); itd++){
                if (myMeta.tracks[*itd].codec == (*itc).asStringRef()){
                  selCounter++;
                  found = true;
                  break;
                }
              }
              if (!found){
                for (std::map<unsigned int,DTSC::Track>::iterator trit = myMeta.tracks.begin(); trit != myMeta.tracks.end(); trit++){
                  if (trit->second.codec == (*itc).asStringRef() || (*itc).asStringRef() == "*"){
                    genCounter++;
                    found = true;
                    if ((*itc).asStringRef() != "*"){
                      break;
                    }
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
            HIGH_MSG("Match (%u/%u): %s", selCounter, selCounter+genCounter, (*it).toString().c_str());
          }
        }else{
          VERYHIGH_MSG("Not a match for currently selected tracks: %s", (*it).toString().c_str());
        }
      }
      index++;
    }
    
    MEDIUM_MSG("Trying to fill: %s", capa["codecs"][bestSoFar].toString().c_str());
    //try to fill as many codecs simultaneously as possible
    if (capa["codecs"][bestSoFar].size() > 0){
      jsonForEach(capa["codecs"][bestSoFar], itb) {
        if ((*itb).size() && myMeta.tracks.size()){
          bool found = false;
          jsonForEach((*itb), itc) {
            if (found) {
              break;
            }
            for (std::set<unsigned long>::iterator itd = selectedTracks.begin(); itd != selectedTracks.end(); itd++){
              if (myMeta.tracks[*itd].codec == (*itc).asStringRef()){
                found = true;
                break;
              }
            }
            if (!found){
              for (std::map<unsigned int,DTSC::Track>::iterator trit = myMeta.tracks.begin(); trit != myMeta.tracks.end(); trit++){
                if (trit->second.codec == (*itc).asStringRef() || (*itc).asStringRef() == "*"){
                  selectedTracks.insert(trit->first);
                  found = true;
                  if ((*itc).asStringRef() != "*"){
                    break;
                  }
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
    
    if (selectedTracks.size() == 0) {
      INSANE_MSG("We didn't find any tracks which that we can use. selectedTrack.size() is 0.");
      for (std::map<unsigned int,DTSC::Track>::iterator trit = myMeta.tracks.begin(); trit != myMeta.tracks.end(); trit++){
        INSANE_MSG("Found track/codec: %s", trit->second.codec.c_str());
      }
      static std::string source;
      if (!source.size()){
        IPC::sharedPage serverCfg(SHM_CONF, DEFAULT_CONF_PAGE_SIZE, false, false); ///< Contains server configuration and capabilities
        IPC::semaphore configLock(SEM_CONF, O_CREAT | O_RDWR, ACCESSPERMS, 1);
        configLock.wait();
        std::string smp = streamName.substr(0, streamName.find_first_of("+ "));
        //check if smp (everything before + or space) exists
        DTSC::Scan streamCfg = DTSC::Scan(serverCfg.mapped, serverCfg.len).getMember("streams").getMember(smp);
        if (streamCfg){
          source = streamCfg.getMember("source").asString();
        }
        configLock.post();
        configLock.close();
      }
      if (!myMeta.tracks.size() && (source.find("dtsc://") == 0)){
        //Wait 5 seconds and try again. Keep a counter, try at most 3 times
        static int counter = 0;
        if (counter++ < 10){
          Util::wait(1000);
          nProxy.userClient.keepAlive();
          stats();
          updateMeta();
          selectDefaultTracks();
        }
      }
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
    if (!nProxy.metaPages.count(trackId) || !nProxy.metaPages[trackId].mapped){
      char id[NAME_BUFFER_SIZE];
      snprintf(id, NAME_BUFFER_SIZE, SHM_TRACK_INDEX, streamName.c_str(), trackId);
      nProxy.metaPages[trackId].init(id, SHM_TRACK_INDEX_SIZE);
    }
    if (!nProxy.metaPages[trackId].mapped){return -1;}
    int len = nProxy.metaPages[trackId].len / 8;
    for (int i = 0; i < len; i++){
      int * tmpOffset = (int *)(nProxy.metaPages[trackId].mapped + (i * 8));
      long amountKey = ntohl(tmpOffset[1]);
      if (amountKey == 0){continue;}
      long tmpKey = ntohl(tmpOffset[0]);
      if (tmpKey <= keyNum && ((tmpKey?tmpKey:1) + amountKey) > keyNum){
        return tmpKey;
      }
    }
    return -1;
  }

  /// Gets the highest page number available for the given trackId.
  int Output::pageNumMax(long unsigned int trackId){
    if (!nProxy.metaPages.count(trackId) || !nProxy.metaPages[trackId].mapped){
      char id[NAME_BUFFER_SIZE];
      snprintf(id, NAME_BUFFER_SIZE, SHM_TRACK_INDEX, streamName.c_str(), trackId);
      nProxy.metaPages[trackId].init(id, SHM_TRACK_INDEX_SIZE);
    }
    if (!nProxy.metaPages[trackId].mapped){return -1;}
    int len = nProxy.metaPages[trackId].len / 8;
    int highest = -1;
    for (int i = 0; i < len; i++){
      int * tmpOffset = (int *)(nProxy.metaPages[trackId].mapped + (i * 8));
      long amountKey = ntohl(tmpOffset[1]);
      if (amountKey == 0){continue;}
      long tmpKey = ntohl(tmpOffset[0]);
      if (tmpKey > highest){highest = tmpKey;}
    }
    return highest;
  }
  
  void Output::loadPageForKey(long unsigned int trackId, long long int keyNum){
    if (myMeta.vod && keyNum > myMeta.tracks[trackId].keys.rbegin()->getNumber()){
      INFO_MSG("Seek in track %lu to key %lld aborted, is > %lld", trackId, keyNum, myMeta.tracks[trackId].keys.rbegin()->getNumber());
      nProxy.curPage.erase(trackId);
      currKeyOpen.erase(trackId);
      return;
    }
    VERYHIGH_MSG("Loading track %lu, containing key %lld", trackId, keyNum);
    unsigned int timeout = 0;
    unsigned long pageNum = pageNumForKey(trackId, keyNum);
    while (pageNum == -1){
      if (!timeout){
        HIGH_MSG("Requesting page with key %lu:%lld", trackId, keyNum);
      }
      ++timeout;
      //if we've been waiting for this page for 3 seconds, reconnect to the stream - something might be going wrong...
      if (timeout == 30){
        DEVEL_MSG("Loading is taking longer than usual, reconnecting to stream %s...", streamName.c_str());
        reconnect();
      }
      if (timeout > 100){
        FAIL_MSG("Timeout while waiting for requested page %lld for track %lu. Aborting.", keyNum, trackId);
        nProxy.curPage.erase(trackId);
        currKeyOpen.erase(trackId);
        return;
      }
      if (keyNum){
        nxtKeyNum[trackId] = keyNum-1;
      }else{
        nxtKeyNum[trackId] = 0;
      }
      stats(true);
      Util::wait(100);
      pageNum = pageNumForKey(trackId, keyNum);
    }
    
    if (keyNum){
      nxtKeyNum[trackId] = keyNum-1;
    }else{
      nxtKeyNum[trackId] = 0;
    }
    stats(true);
    
    if (currKeyOpen.count(trackId) && currKeyOpen[trackId] == (unsigned int)pageNum){
      return;
    }
    char id[NAME_BUFFER_SIZE];
    snprintf(id, NAME_BUFFER_SIZE, SHM_TRACK_DATA, streamName.c_str(), trackId, pageNum);
    nProxy.curPage[trackId].init(id, DEFAULT_DATA_PAGE_SIZE);
    if (!(nProxy.curPage[trackId].mapped)){
      FAIL_MSG("Initializing page %s failed", nProxy.curPage[trackId].name.c_str());
      return;
    }
    currKeyOpen[trackId] = pageNum;
    VERYHIGH_MSG("Page %s loaded for %s", id, streamName.c_str());
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
    MEDIUM_MSG("Seeking to %llums", pos);
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      if (myMeta.tracks.count(*it)){
        seek(*it, pos);
      }
    }
  }

  bool Output::seek(unsigned int tid, unsigned long long pos, bool getNextKey){
    if (myMeta.tracks[tid].lastms < pos){
      INFO_MSG("Aborting seek to %llums in track %u: past end of track (= %llums).", pos, tid, myMeta.tracks[tid].lastms);
      return false;
    }
    unsigned int keyNum = getKeyForTime(tid, pos);
    if (myMeta.tracks[tid].getKey(keyNum).getTime() > pos){
      if (myMeta.live){
        INFO_MSG("Actually seeking to %d, for %d is not available any more", myMeta.tracks[tid].getKey(keyNum).getTime(), pos);
        pos = myMeta.tracks[tid].getKey(keyNum).getTime();
      }
    }
    loadPageForKey(tid, keyNum + (getNextKey?1:0));
    if (!nProxy.curPage.count(tid) || !nProxy.curPage[tid].mapped){
      INFO_MSG("Aborting seek to %llums in track %u: not available.", pos, tid);
      return false;
    }
    sortedPageInfo tmp;
    tmp.tid = tid;
    tmp.offset = 0;
    DTSC::Packet tmpPack;
    tmpPack.reInit(nProxy.curPage[tid].mapped + tmp.offset, 0, true);
    tmp.time = tmpPack.getTime();
    char * mpd = nProxy.curPage[tid].mapped;
    while ((long long)tmp.time < pos && tmpPack){
      tmp.offset += tmpPack.getDataLen();
      tmpPack.reInit(mpd + tmp.offset, 0, true);
      tmp.time = tmpPack.getTime();
    }
    if (tmpPack){
      HIGH_MSG("Sought to time %d in %s@%u", tmp.time, streamName.c_str(), tid);
      buffer.insert(tmp);
      return true;
    }else{
      //don't print anything for empty packets - not sign of corruption, just unfinished stream.
      if (nProxy.curPage[tid].mapped[tmp.offset] != 0){
        FAIL_MSG("Noes! Couldn't find packet on track %d because of some kind of corruption error or somesuch.", tid);
      }else{
        VERYHIGH_MSG("Track %d no data (key %u @ %u) - waiting...", tid, getKeyForTime(tid, pos) + (getNextKey?1:0), tmp.offset);
        unsigned int i = 0;
        while (nProxy.curPage[tid].mapped[tmp.offset] == 0 && ++i < 42){
          Util::wait(100);
        }
        if (nProxy.curPage[tid].mapped[tmp.offset] == 0){
          FAIL_MSG("Track %d no data (key %u) - timeout", tid, getKeyForTime(tid, pos) + (getNextKey?1:0));
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
    bool hasWhitelist = false;
    bool hostOnWhitelist = false;
    if (Storage["streams"].isMember(streamName)){
      if (Storage["streams"][streamName].isMember("limits") && Storage["streams"][streamName]["limits"].size()){
        jsonForEach(Storage["streams"][streamName]["limits"], limitIt){
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
      jsonForEach(Storage["config"]["limits"], limitIt){
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
 
  /// \triggers 
  /// The `"CONN_OPEN"` trigger is stream-specific, and is ran when a connection is made or passed to a new handler. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// connected client host
  /// output handler name
  /// request URL (if any)
  /// ~~~~~~~~~~~~~~~
  /// The `"CONN_CLOSE"` trigger is stream-specific, and is ran when a connection closes. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// connected client host
  /// output handler name
  /// request URL (if any)
  /// ~~~~~~~~~~~~~~~
  int Output::run() {
    /*LTS-START*/
    if(Triggers::shouldTrigger("CONN_OPEN", streamName)){
      std::string payload = streamName+"\n" + getConnectedHost() +"\n"+capa["name"].asStringRef()+"\n"+reqUrl;
      if (!Triggers::doTrigger("CONN_OPEN", payload, streamName)){
        return 1;
      }
    }
    /*LTS-END*/
    DONTEVEN_MSG("MistOut client handler started");
    while (config->is_active && myConn && (wantRequest || parseData)){
      if (wantRequest){
        requestHandler();
      }
      if (parseData){
        if (!isInitialized){
          initialize();
        }
        if ( !sentHeader){
          DONTEVEN_MSG("sendHeader");
          sendHeader();
        }
        if (!sought){
          if (myMeta.live){
            long unsigned int mainTrack = getMainSelectedTrack();
            //cancel if there are no keys in the main track
            if (!myMeta.tracks.count(mainTrack) || !myMeta.tracks[mainTrack].keys.size()){break;}
            //seek to the newest keyframe, unless that is <5s, then seek to the oldest keyframe
            unsigned long long seekPos = myMeta.tracks[mainTrack].keys.rbegin()->getTime();
            if (seekPos < 5000){
              seekPos = myMeta.tracks[mainTrack].keys.begin()->getTime();
            }
            seek(seekPos);
          }else{
            seek(0);
          }
        }
        if (prepareNext()){
          if (thisPacket){


            //slow down processing, if real time speed is wanted
            if (realTime){
              while (thisPacket.getTime() > (((Util::getMS() - firstTime)*1000)+maxSkipAhead)/realTime) {
                Util::sleep(std::min(thisPacket.getTime() - (((Util::getMS() - firstTime)*1000)+minSkipAhead)/realTime, 1000llu));
                stats();
              }
            }

            //delay the stream until its current keyframe is complete, if only complete keys wanted
            if (completeKeysOnly){
              bool completeKeyReady = false;
              int timeoutTries = 40;//wait default 250ms*40=10 seconds
              for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
                if (it->second.keys.size() >1){
                  int thisTimeoutTries = ((it->second.lastms - it->second.firstms) / (it->second.keys.size()-1)) / 125;
                  if (thisTimeoutTries > timeoutTries) timeoutTries = thisTimeoutTries;
                }
              }
              while(!completeKeyReady && timeoutTries>0){
                completeKeyReady = true;
                for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
                  if (!myMeta.tracks[*it].keys.size() || myMeta.tracks[*it].keys.rbegin()->getTime() + myMeta.tracks[*it].keys.rbegin()->getLength() <= thisPacket.getTime() ){
                    completeKeyReady = false;
                    break;
                  }
                }
                if (!completeKeyReady){
                  timeoutTries--;//we count down
                  stats();
                  Util::wait(250);
                  updateMeta();
                }
              }
              if (timeoutTries<=0){
                WARN_MSG("Waiting for key frame timed out");
                completeKeysOnly = false;
              }
            }

            sendNext();
          }else{
            /*LTS-START*/      
            if(Triggers::shouldTrigger("CONN_STOP", streamName)){
              std::string payload = streamName+"\n" + getConnectedHost() +"\n"+capa["name"].asStringRef()+"\n";
              Triggers::doTrigger("CONN_STOP", payload, streamName);
            }
            /*LTS-END*/
            if (!onFinish()){
              break;
            }
          }
        }
      }
      stats();
    }
    MEDIUM_MSG("MistOut client handler shutting down: %s, %s, %s", myConn.connected() ? "conn_active" : "conn_closed", wantRequest ? "want_request" : "no_want_request", parseData ? "parsing_data" : "not_parsing_data");
    
    /*LTS-START*/
    if(Triggers::shouldTrigger("CONN_CLOSE", streamName)){
      std::string payload = streamName+"\n"+getConnectedHost()+"\n"+capa["name"].asStringRef()+"\n"+reqUrl; ///\todo generate payload
      Triggers::doTrigger("CONN_CLOSE", payload, streamName); //no stream specified    
    }
    /*LTS-END*/
  
    stats(true);
    nProxy.userClient.finish();
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
      if (myMeta.tracks.count(*it) && myMeta.tracks[*it].type == "video"){
        return *it;
      }
    }
    return *(selectedTracks.begin());
  }

  void Output::dropTrack(uint32_t trackId, std::string reason, bool probablyBad){
    //depending on whether this is probably bad and the current debug level, print a message
    unsigned int printLevel = DLVL_INFO;
    if (probablyBad){
      printLevel = DLVL_WARN;
    }
    DEBUG_MSG(printLevel, "Dropping %s (%s) track %lu@k%lu (nextP=%d, lastP=%d): %s", streamName.c_str(), myMeta.tracks[trackId].codec.c_str(), (long unsigned)trackId, nxtKeyNum[trackId]+1, pageNumForKey(trackId, nxtKeyNum[trackId]+1), pageNumMax(trackId), reason.c_str());
    //now actually drop the track from the buffer
    for (std::set<sortedPageInfo>::iterator it = buffer.begin(); it != buffer.end(); ++it){
      if (it->tid == trackId){
        buffer.erase(it);
        break;
      }
    }
    selectedTracks.erase(trackId);
  }
 
  ///Attempts to prepare a new packet for output.
  ///If thisPacket evaluates to false, playback has completed.
  ///Could be called repeatedly in a loop if you really really want a new packet.
  /// \returns true if thisPacket was filled with the next packet.
  /// \returns false if we could not reliably determine the next packet yet.
  bool Output::prepareNext(){
    static bool atLivePoint = false;
    static int nonVideoCount = 0;
    static unsigned int emptyCount = 0;
    if (!buffer.size()){
      thisPacket.null();
      INFO_MSG("Buffer completely played out");
      return true;
    }
    sortedPageInfo nxt = *(buffer.begin());

    if (!myMeta.tracks.count(nxt.tid)){
      dropTrack(nxt.tid, "disappeared from metadata", true);
      return false;
    }

    DONTEVEN_MSG("Loading track %u (next=%lu), %llu ms", nxt.tid, nxtKeyNum[nxt.tid], nxt.time);
   
    //if we're going to read past the end of the data page, load the next page
    //this only happens for VoD
    if (nxt.offset >= nProxy.curPage[nxt.tid].len){
      nxtKeyNum[nxt.tid] = getKeyForTime(nxt.tid, thisPacket.getTime());
      loadPageForKey(nxt.tid, ++nxtKeyNum[nxt.tid]);
      nxt.offset = 0;
      if (nProxy.curPage.count(nxt.tid) && nProxy.curPage[nxt.tid].mapped){
        if (getDTSCTime(nProxy.curPage[nxt.tid].mapped, nxt.offset) < nxt.time){
          dropTrack(nxt.tid, "time going backwards");
        }else{
          nxt.time = getDTSCTime(nProxy.curPage[nxt.tid].mapped, nxt.offset);
          //swap out the next object in the buffer with a new one
          buffer.erase(buffer.begin());
          buffer.insert(nxt);
        }
      }else{
        dropTrack(nxt.tid, "page load failure", true);
      }
      return false;
    }
    
    //have we arrived at the end of the memory page? (4 zeroes mark the end)
    if (!memcmp(nProxy.curPage[nxt.tid].mapped + nxt.offset, "\000\000\000\000", 4)){
      //if we don't currently know where we are, we're lost. We should drop the track.
      if (!nxt.time){
        dropTrack(nxt.tid, "timeless empty packet");
        return false;
      }
      //if this is a live stream, we might have just reached the live point.
      //check where the next key is
      nxtKeyNum[nxt.tid] = getKeyForTime(nxt.tid, thisPacket.getTime());
      int nextPage = pageNumForKey(nxt.tid, nxtKeyNum[nxt.tid]+1);
      //are we live, and the next key hasn't shown up on another page, then we're waiting.
      if (myMeta.live && currKeyOpen.count(nxt.tid) && (currKeyOpen[nxt.tid] == (unsigned int)nextPage || nextPage == -1)){
        if (++emptyCount < 100){
          Util::wait(250);
          //we're waiting for new data to show up
          if (emptyCount % 8 == 0){
            reconnect();//reconnect every 2 seconds
          }else{
            if (emptyCount % 4 == 0){
              updateMeta();
            }
          }
        }else{
          //after ~25 seconds, give up and drop the track.
          dropTrack(nxt.tid, "could not reload empty packet");
        }
        return false;
      }

      //We've simply reached the end of the page. Load the next key = next page.
      loadPageForKey(nxt.tid, ++nxtKeyNum[nxt.tid]);
      nxt.offset = 0;
      if (nProxy.curPage.count(nxt.tid) && nProxy.curPage[nxt.tid].mapped){
        unsigned long long nextTime = getDTSCTime(nProxy.curPage[nxt.tid].mapped, nxt.offset);
        if (nextTime && nextTime < nxt.time){
          dropTrack(nxt.tid, "time going backwards");
        }else{
          if (nextTime){
            nxt.time = nextTime;
          }
          //swap out the next object in the buffer with a new one
          buffer.erase(buffer.begin());
          buffer.insert(nxt);
          MEDIUM_MSG("Next page for track %u starts at %llu.", nxt.tid, nxt.time);
        }
      }else{
        dropTrack(nxt.tid, "page load failure");
      }
      return false;
    }
    
    //we've handled all special cases - at this point the packet should exist
    //let's load it
    thisPacket.reInit(nProxy.curPage[nxt.tid].mapped + nxt.offset, 0, true);
    //if it failed, drop the track and continue
    if (!thisPacket){
      dropTrack(nxt.tid, "packet load failure");
      return false;
    }
    emptyCount = 0;//valid packet - reset empty counter

    //if there's a timestamp mismatch, print this.
    //except for live, where we never know the time in advance
    if (thisPacket.getTime() != nxt.time && nxt.time && !atLivePoint){
      static int warned = 0;
      if (warned < 5){
        WARN_MSG("Loaded %s track %ld@%llu in stead of %u@%llu (%dms, %s)", streamName.c_str(), thisPacket.getTrackId(), thisPacket.getTime(), nxt.tid, nxt.time, (int)((long long)thisPacket.getTime() - (long long)nxt.time), myMeta.tracks[nxt.tid].codec.c_str());
        if (++warned == 5){
          WARN_MSG("Further warnings about time mismatches printed on HIGH level.");
        }
      }else{
        HIGH_MSG("Loaded %s track %ld@%llu in stead of %u@%llu (%dms, %s)", streamName.c_str(), thisPacket.getTrackId(), thisPacket.getTime(), nxt.tid, nxt.time, (int)((long long)thisPacket.getTime() - (long long)nxt.time), myMeta.tracks[nxt.tid].codec.c_str());
      }
    }

    //when live, every keyframe, check correctness of the keyframe number
    if (myMeta.live && thisPacket.getFlag("keyframe")){
      //Check whether returned keyframe is correct. If not, wait for approximately 10 seconds while checking.
      //Failure here will cause tracks to drop due to inconsistent internal state.
      nxtKeyNum[nxt.tid] = getKeyForTime(nxt.tid, thisPacket.getTime());
      int counter = 0;
      while(counter < 40 && myMeta.tracks[nxt.tid].getKey(nxtKeyNum[nxt.tid]).getTime() != thisPacket.getTime()){
        if (counter++){
          //Only sleep 250ms if this is not the first updatemeta try
          Util::wait(250);
        }
        updateMeta();
        nxtKeyNum[nxt.tid] = getKeyForTime(nxt.tid, thisPacket.getTime());
      }
      if (myMeta.tracks[nxt.tid].getKey(nxtKeyNum[nxt.tid]).getTime() != thisPacket.getTime()){
        WARN_MSG("Keyframe value is not correct - state will now be inconsistent.");
      }
      EXTREME_MSG("Track %u @ %llums = key %lu", nxt.tid, thisPacket.getTime(), nxtKeyNum[nxt.tid]);
    }

    //always assume we're not at the live point
    atLivePoint = false;
    //we assume the next packet is the next on this same page
    nxt.offset += thisPacket.getDataLen();
    if (nxt.offset < nProxy.curPage[nxt.tid].len){
      unsigned long long nextTime = getDTSCTime(nProxy.curPage[nxt.tid].mapped, nxt.offset);
      if (nextTime){
        nxt.time = nextTime;
      }else{
        ++nxt.time;
        //no packet -> we are at the live point
        atLivePoint = true;
      }
    }

    //exchange the current packet in the buffer for the next one
    buffer.erase(buffer.begin());
    buffer.insert(nxt);

    return true;
  }

  /// Returns the name as it should be used in statistics.
  /// Outputs used as an input should return INPUT, outputs used for automation should return OUTPUT, others should return their proper name.
  /// The default implementation is usually good enough for all the non-INPUT types.
  std::string Output::getStatsName(){
    if (config->hasOption("target") && config->getString("target").size()){
      return "OUTPUT";
    }else{
      return capa["name"].asStringRef();
    }
  }

  void Output::stats(bool force){
    //cancel stats update if not initialized
    if (!isInitialized){return;}
    //also cancel if it has been less than a second since the last update
    //unless force is set to true
    unsigned long long int now = Util::epoch();
    if (now == lastStats && !force){return;}
    lastStats = now;

    EXTREME_MSG("Writing stats: %s, %s, %lu", getConnectedHost().c_str(), streamName.c_str(), crc & 0xFFFFFFFFu);
    if (statsPage.getData()){
      /*LTS-START*/
      if (!statsPage.isAlive()){
        myConn.close();
        return;
      }
      /*LTS-END*/
      IPC::statExchange tmpEx(statsPage.getData());
      tmpEx.now(now);
      if (tmpEx.host() == std::string("\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000", 16)){
        tmpEx.host(getConnectedBinHost());
      }
      tmpEx.crc(crc);
      tmpEx.streamName(streamName);
      tmpEx.connector(getStatsName());
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
    doSync();
    int tNum = 0;
    if (!nProxy.userClient.getData()){
      char userPageName[NAME_BUFFER_SIZE];
      snprintf(userPageName, NAME_BUFFER_SIZE, SHM_USERS, streamName.c_str());
      nProxy.userClient = IPC::sharedClient(userPageName, PLAY_EX_SIZE, true);
      if (!nProxy.userClient.getData()){
        WARN_MSG("Player connection failure - aborting output");
        myConn.close();
        return;
      }
    }
    if (!nProxy.userClient.isAlive()){
      myConn.close();
      return;
    }
    if (!nProxy.trackMap.size()){
      IPC::userConnection userConn(nProxy.userClient.getData());
      for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end() && tNum < SIMUL_TRACKS; it++){
        userConn.setTrackId(tNum, *it);
        userConn.setKeynum(tNum, nxtKeyNum[*it]);
        tNum ++;
      }
    }
    nProxy.userClient.keepAlive();
    if (tNum > SIMUL_TRACKS){
      WARN_MSG("Too many tracks selected, using only first %d", SIMUL_TRACKS);
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
  
  bool Output::connectToFile(std::string file) {
    int flags = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    int mode = O_RDWR | O_CREAT | O_TRUNC;
    int outFile = open(file.c_str(), mode, flags);
    if (outFile < 0) {
      ERROR_MSG("Failed to open file %s, error: %s", file.c_str(), strerror(errno));
      return false;
    }
    
    int r = dup2(outFile, myConn.getSocket());
    if (r == -1) {
      ERROR_MSG("Failed to create an alias for the socket using dup2: %s.", strerror(errno));
      return false;
    }
    close(outFile);
    return true;
  }

}

