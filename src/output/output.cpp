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
#include <mist/util.h>
#include "output.h"

/*LTS-START*/
#include <mist/triggers.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/stat.h>
/*LTS-END*/

namespace Mist{
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

    JSON::Value option;
    option["long"] = "noinput";
    option["short"] = "N";
    option["help"] = "Do not start input if not already started";
    option["value"].append(0ll);
    cfg->addOption("noinput", option);
  }
  
  void Output::bufferLivePacket(const DTSC::Packet & packet){
    if (!pushIsOngoing){
      waitForStreamPushReady();
    }
    if (nProxy.negTimer > 600){
      WARN_MSG("No negotiation response from buffer - reconnecting.");
      nProxy.clear();
      reconnect();
    }
    InOutBase::bufferLivePacket(packet);
    pushIsOngoing = true;
  }
  
  Output::Output(Socket::Connection & conn) : myConn(conn){
    pushing = false;
    pushIsOngoing = false;
    firstTime = 0;
    firstPacketTime = 0xFFFFFFFFFFFFFFFFull;
    lastPacketTime = 0;
    crc = getpid();
    parseData = false;
    wantRequest = true;
    sought = false;
    isInitialized = false;
    isBlocking = false;
    needsLookAhead = 0;
    lastStats = 0;
    maxSkipAhead = 7500;
    uaDelay = 10;
    realTime = 1000;
    lastRecv = Util::epoch();
    if (myConn){
      setBlocking(true);
    }else{
      DEBUG_MSG(DLVL_WARN, "Warning: MistOut created with closed socket!");
    }
    sentHeader = false;
    isRecordingToFile = false;
    
    //If we have a streamname option, set internal streamname to that option
    if (!streamName.size() && config->hasOption("streamname")){
      streamName = config->getString("streamname");
    }

    /*LTS-START*/
    // If we have a target, scan for trailing ?, remove it, parse into targetParams
    if (config->hasOption("target")){
      std::string tgt = config->getString("target");
      if (tgt.rfind('?') != std::string::npos){
        INFO_MSG("Stripping target options: %s", tgt.substr(tgt.rfind('?') + 1).c_str());
        HTTP::parseVars(tgt.substr(tgt.rfind('?') + 1), targetParams);
        config->getOption("target", true).append(tgt.substr(0, tgt.rfind('?')));
      }else{
        INFO_MSG("Not modifying target (%s), no options present", tgt.c_str());
      }
    }
    /*LTS-END*/
  }

  bool Output::isFileTarget(){
    INFO_MSG("Default file target handler (false)");
    return false;
  }

  void Output::listener(Util::Config & conf, int (*callback)(Socket::Connection & S)){
    conf.serveForkedSocket(callback);
  }
  
  void Output::setBlocking(bool blocking){
    isBlocking = blocking;
    myConn.setBlocking(isBlocking);
  }
  
  uint32_t Output::currTrackCount() const{
    return buffer.size();
  }

  bool Output::isRecording(){
    return config->hasOption("target") && config->getString("target").size();
  }

  void Output::updateMeta(){
    //cancel if not alive or pushing a new stream
    if (!nProxy.userClient.isAlive() || (isPushing() && myMeta.tracks.size())){
      return;
    }
    //read metadata from page to myMeta variable
    if (nProxy.metaPages[0].mapped){
      IPC::semaphore * liveSem = 0;
      if (!myMeta.vod){
        static char liveSemName[NAME_BUFFER_SIZE];
        snprintf(liveSemName, NAME_BUFFER_SIZE, SEM_LIVE, streamName.c_str());
        liveSem = new IPC::semaphore(liveSemName, O_RDWR, ACCESSPERMS, 1, !myMeta.live);
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
    sought = false;
    /*LTS-START*/
    if(Triggers::shouldTrigger("CONN_PLAY", streamName)){
      std::string payload = streamName+"\n" + getConnectedHost() +"\n"+capa["name"].asStringRef()+"\n"+reqUrl;
      if (!Triggers::doTrigger("CONN_PLAY", payload, streamName)){
        INFO_MSG("Shutting down due to CONN_PLAY trigger rejection");
        onFinish();
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
        char initialSync = 0;
        //attempt to load sync status from session cache in shm
        {
          IPC::semaphore cacheLock(SEM_SESSCACHE, O_RDWR, ACCESSPERMS, 1);
          if (cacheLock){cacheLock.wait();}
          IPC::sharedPage shmSessions(SHM_SESSIONS, SHM_SESSIONS_SIZE, false, false);
          if (shmSessions.mapped){
            char shmEmpty[SHM_SESSIONS_ITEM];
            memset(shmEmpty, 0, SHM_SESSIONS_ITEM);
            std::string host = tmpEx.host();
            if (host.substr(0, 12) == std::string("\000\000\000\000\000\000\000\000\000\000\377\377", 12)){
              char tmpstr[16];
              snprintf(tmpstr, 16, "%hhu.%hhu.%hhu.%hhu", host[12], host[13], host[14], host[15]);
              host = tmpstr;
            }else{
              char tmpstr[40];
              snprintf(tmpstr, 40, "%0.2x%0.2x:%0.2x%0.2x:%0.2x%0.2x:%0.2x%0.2x:%0.2x%0.2x:%0.2x%0.2x:%0.2x%0.2x:%0.2x%0.2x", host[0], host[1], host[2], host[3], host[4], host[5], host[6], host[7], host[8], host[9], host[10], host[11], host[12], host[13], host[14], host[15]);
              host = tmpstr;
            }
            uint32_t shmOffset = 0;
            const std::string & cName = capa["name"].asStringRef();
            while (shmOffset + SHM_SESSIONS_ITEM < SHM_SESSIONS_SIZE){
              //compare crc
              if (*((uint32_t*)(shmSessions.mapped+shmOffset)) == tmpEx.crc()){
                //compare stream name
                if (strncmp(shmSessions.mapped+shmOffset+4, streamName.c_str(), 100) == 0){
                  //compare connector
                  if (strncmp(shmSessions.mapped+shmOffset+104, cName.c_str(), 20) == 0){
                    //compare host
                    if (strncmp(shmSessions.mapped+shmOffset+124, host.c_str(), 40) == 0){
                      initialSync = shmSessions.mapped[shmOffset+164];
                      INFO_MSG("Instant-sync from session cache to %u", (unsigned int)initialSync);
                      break;
                    }
                  }
                }
              }
              //stop if we reached the end
              if (memcmp(shmSessions.mapped+shmOffset, shmEmpty, SHM_SESSIONS_ITEM) == 0){
                break;
              }
              shmOffset += SHM_SESSIONS_ITEM;
            }
          }
          if (cacheLock){cacheLock.post();}
        }
        unsigned int i = 0;
        tmpEx.setSync(initialSync);
        //wait max 10 seconds for sync
        while ((!tmpEx.getSync() || tmpEx.getSync() == 2) && i++ < 100 && keepGoing()){
          Util::wait(100);
          stats(true);
          tmpEx = IPC::statExchange(statsPage.getData());
        }
        HIGH_MSG("USER_NEW sync achieved: %u", (unsigned int)tmpEx.getSync());
        //1 = check requested (connection is new)
        if (tmpEx.getSync() == 1){
          std::string payload = streamName+"\n" + getConnectedHost() +"\n" + JSON::Value((long long)crc).asString() + "\n"+capa["name"].asStringRef()+"\n"+reqUrl+"\n"+tmpEx.getSessId();
          if (!Triggers::doTrigger("USER_NEW", payload, streamName)){
            MEDIUM_MSG("Closing connection because denied by USER_NEW trigger");
            onFinish();
            myConn.close();
            tmpEx.setSync(100);//100 = denied
          }else{
            tmpEx.setSync(10);//10 = accepted
          }
        }
        //100 = denied
        if (tmpEx.getSync() == 100){
          onFinish();
          myConn.close();
          MEDIUM_MSG("Closing connection because denied by USER_NEW sync byte");
        }
        if (tmpEx.getSync() == 0 || tmpEx.getSync() == 2){
          onFinish();
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
 
  bool Output::isReadyForPlay(){
    if (isPushing()){return true;}
    if (myMeta.tracks.size()){
      if (!selectedTracks.size()){
        selectDefaultTracks();
      }
      unsigned int mainTrack = getMainSelectedTrack();
      if (mainTrack && myMeta.tracks.count(mainTrack) && (myMeta.tracks[mainTrack].keys.size() >= 2 || myMeta.tracks[mainTrack].lastms - myMeta.tracks[mainTrack].firstms > 5000)){
        return true;
      }else{
        HIGH_MSG("NOT READY YET (%lu tracks, %lu = %lu keys)", myMeta.tracks.size(), getMainSelectedTrack(), myMeta.tracks[getMainSelectedTrack()].keys.size());
      }
    }else{
      HIGH_MSG("NOT READY YET (%lu tracks)", myMeta.tracks.size());
    }
    return false;
  }

  /// Connects or reconnects to the stream.
  /// Assumes streamName class member has been set already.
  /// Will start input if not currently active, calls onFail() if this does not succeed.
  /// After assuring stream is online, clears nProxy.metaPages, then sets nProxy.metaPages[0], statsPage and nProxy.userClient to (hopefully) valid handles.
  /// Finally, calls updateMeta()
  void Output::reconnect(){
    thisPacket.null();
    if (config->hasOption("noinput") && config->getBool("noinput")){
      Util::sanitizeName(streamName);
      if (!Util::streamAlive(streamName)){
        FAIL_MSG("Stream %s not already active - aborting initialization", streamName.c_str());
        onFail();
        return;
      }
    }else{
      if (!Util::startInput(streamName, "", true, isPushing())){
        FAIL_MSG("Opening stream %s failed - aborting initialization", streamName.c_str());
        onFail();
        return;
      }
    }
    if (statsPage.getData()){
      statsPage.finish();
      myConn.resetCounter();
    }
    if (nProxy.userClient.getData()){
      nProxy.userClient.finish();
    }
    nProxy.streamName = streamName;
    char userPageName[NAME_BUFFER_SIZE];
    snprintf(userPageName, NAME_BUFFER_SIZE, SHM_USERS, streamName.c_str());
    unsigned int attempts = 0;
    while (!nProxy.userClient.isAlive() && ++attempts < 20 && Util::streamAlive(streamName)){
      nProxy.userClient = IPC::sharedClient(userPageName, PLAY_EX_SIZE, true);
    }
    if (!nProxy.userClient.isAlive()){
      FAIL_MSG("Could not register as client for %s", streamName.c_str());
      onFail();
      return;
    }
    char pageId[NAME_BUFFER_SIZE];
    snprintf(pageId, NAME_BUFFER_SIZE, SHM_STREAM_INDEX, streamName.c_str());
    nProxy.metaPages.clear();
    nProxy.metaPages[0].init(pageId, DEFAULT_STRM_PAGE_SIZE);
    if (!nProxy.metaPages[0].mapped){
      FAIL_MSG("Could not connect to data for %s", streamName.c_str());
      onFail();
      return;
    }
    statsPage = IPC::sharedClient(SHM_STATISTICS, STAT_EX_SIZE, true);
    stats(true);
    updateMeta();
    selectDefaultTracks();
    if (!myMeta.vod && !isReadyForPlay()){
      unsigned long long waitUntil = Util::epoch() + 30;
      while (!myMeta.vod && !isReadyForPlay() && nProxy.userClient.isAlive() && keepGoing()){
        if (Util::epoch() > waitUntil + 45 || (!selectedTracks.size() && Util::epoch() > waitUntil)){
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
    jsonForEach(capa["codecs"], it){
      unsigned int genCounter = 0;
      unsigned int selCounter = 0;
      if ((*it).size() > 0){
        jsonForEach((*it), itb){
          if ((*itb).size() > 0){
            bool found = false;
            jsonForEach(*itb, itc){
              for (std::set<unsigned long>::iterator itd = selectedTracks.begin(); itd != selectedTracks.end(); itd++){
                if (myMeta.tracks[*itd].codec == (*itc).asStringRef()){
                  selCounter++;
                  found = true;
                  break;
                }
              }
            }
            if (!found){
              jsonForEach(*itb, itc){
                for (std::map<unsigned int, DTSC::Track>::iterator trit = myMeta.tracks.begin(); trit != myMeta.tracks.end(); trit++){
                  if (trit->second.codec == (*itc).asStringRef() || (*itc).asStringRef() == "*"){
                    genCounter++;
                    found = true;
                    if ((*itc).asStringRef() != "*"){break;}
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
            HIGH_MSG("Match (%u/%u): %s", selCounter, selCounter + genCounter, (*it).toString().c_str());
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
      jsonForEach(capa["codecs"][bestSoFar], itb){
        if ((*itb).size() && myMeta.tracks.size()){
          bool found = false;
          jsonForEach((*itb), itc){
            for (std::set<unsigned long>::iterator itd = selectedTracks.begin(); itd != selectedTracks.end(); itd++){
              if (myMeta.tracks[*itd].codec == (*itc).asStringRef()){
                found = true;
                break;
              }
            }
          }
          if (!found){
            jsonForEach((*itb), itc){
              if (found){break;}
              if (myMeta.live){
                for (std::map<unsigned int, DTSC::Track>::reverse_iterator trit = myMeta.tracks.rbegin(); trit != myMeta.tracks.rend(); trit++){
                  if (trit->second.codec == (*itc).asStringRef() || (*itc).asStringRef() == "*"){
                    selectedTracks.insert(trit->first);
                    found = true;
                    if ((*itc).asStringRef() != "*"){break;}
                  }
                }
              }else{
                for (std::map<unsigned int, DTSC::Track>::iterator trit = myMeta.tracks.begin(); trit != myMeta.tracks.end(); trit++){
                  if (trit->second.codec == (*itc).asStringRef() || (*itc).asStringRef() == "*"){
                    selectedTracks.insert(trit->first);
                    found = true;
                    if ((*itc).asStringRef() != "*"){break;}
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
      MEDIUM_MSG("Selected tracks: %s (%lu)", selected.str().c_str(), selectedTracks.size());    
    }
    
    if (!selectedTracks.size() && myMeta.tracks.size() && capa["codecs"][bestSoFar].size()){
      WARN_MSG("No tracks selected (%u total) for stream %s!", myMeta.tracks.size(), streamName.c_str());
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
 
  /// Loads the page for the given trackId and keyNum into memory.
  /// Overwrites any existing page for the same trackId.
  /// Automatically calls thisPacket.null() if necessary.
  void Output::loadPageForKey(long unsigned int trackId, long long int keyNum){
    if (!myMeta.tracks.count(trackId) || !myMeta.tracks[trackId].keys.size()){
      WARN_MSG("Load for track %lu key %lld aborted - track is empty", trackId, keyNum);
      return;
    }
    if (myMeta.vod && keyNum > myMeta.tracks[trackId].keys.rbegin()->getNumber()){
      INFO_MSG("Load for track %lu key %lld aborted, is > %lld", trackId, keyNum, myMeta.tracks[trackId].keys.rbegin()->getNumber());
      nProxy.curPage.erase(trackId);
      currKeyOpen.erase(trackId);
      return;
    }
    VERYHIGH_MSG("Loading track %lu, containing key %lld", trackId, keyNum);
    unsigned int timeout = 0;
    unsigned long pageNum = pageNumForKey(trackId, keyNum);
    while (keepGoing() && pageNum == -1){
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
    
    if (!keepGoing()){
      return;
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
    //If we're loading the track thisPacket is on, null it to prevent accesses.
    if (thisPacket && thisPacket.getTrackId() == trackId){
      thisPacket.null();
    }
    char id[NAME_BUFFER_SIZE];
    snprintf(id, NAME_BUFFER_SIZE, SHM_TRACK_DATA, streamName.c_str(), trackId, pageNum);
    nProxy.curPage[trackId].init(id, DEFAULT_DATA_PAGE_SIZE);
    if (!(nProxy.curPage[trackId].mapped)){
      FAIL_MSG("Initializing page %s failed", nProxy.curPage[trackId].name.c_str());
      currKeyOpen.erase(trackId);
      return;
    }
    currKeyOpen[trackId] = pageNum;
    VERYHIGH_MSG("Page %s loaded for %s", id, streamName.c_str());
  }

  ///Return the current time of the media buffer, or 0 if no buffer available.
  uint64_t Output::currentTime(){
    if (!buffer.size()){return 0;}
    return buffer.begin()->time;
  }
  
  ///Return the start time of the selected tracks.
  ///Returns the start time of earliest track if nothing is selected.
  ///Returns zero if no tracks exist.
  uint64_t Output::startTime(){
    if (!myMeta.tracks.size()){return 0;}
    uint64_t start = 0xFFFFFFFFFFFFFFFFull;
    if (selectedTracks.size()){
      for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
        if (myMeta.tracks.count(*it)){
          if (start > myMeta.tracks[*it].firstms){start = myMeta.tracks[*it].firstms;}
        }
      }
    }else{
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        if (start > it->second.firstms){start = it->second.firstms;}
      }
    }
    return start;
  }

  ///Return the end time of the selected tracks, or 0 if unknown or live.
  ///Returns the end time of latest track if nothing is selected.
  ///Returns zero if no tracks exist.
  uint64_t Output::endTime(){
    if (myMeta.live){return 0;}
    if (!myMeta.tracks.size()){return 0;}
    uint64_t end = 0;
    if (selectedTracks.size()){
      for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
        if (myMeta.tracks.count(*it)){
          if (end < myMeta.tracks[*it].lastms){end = myMeta.tracks[*it].lastms;}
        }
      }
    }else{
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        if (end < it->second.lastms){end = it->second.lastms;}
      }
    }
    return end;
  }

  ///Return the most live time stamp of the selected tracks, or 0 if unknown or non-live.
  ///Returns the time stamp of the newest track if nothing is selected.
  ///Returns zero if no tracks exist.
  uint64_t Output::liveTime(){
    if (!myMeta.live){return 0;}
    if (!myMeta.tracks.size()){return 0;}
    uint64_t end = 0;
    if (selectedTracks.size()){
      for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
        if (myMeta.tracks.count(*it)){
          if (end < myMeta.tracks[*it].lastms){end = myMeta.tracks[*it].lastms;}
        }
      }
    }else{
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        if (end < it->second.lastms){end = it->second.lastms;}
      }
    }
    return end;
  }

  /// Prepares all tracks from selectedTracks for seeking to the specified ms position.
  void Output::seek(unsigned long long pos){
    sought = true;
    if (!isInitialized){
      initialize();
    }
    buffer.clear();
    thisPacket.null();
    if (myMeta.live){
      updateMeta();
    }
    MEDIUM_MSG("Seeking to %llums", pos);
    std::set<long unsigned int> seekTracks = selectedTracks;
    for (std::set<long unsigned int>::iterator it = seekTracks.begin(); it != seekTracks.end(); it++){
      if (myMeta.tracks.count(*it)){
        seek(*it, pos);
      }
    }
    firstTime = Util::getMS() - buffer.begin()->time;
  }

  bool Output::seek(unsigned int tid, unsigned long long pos, bool getNextKey){
    if (myMeta.live && myMeta.tracks[tid].lastms < pos){
      unsigned int maxTime = 0;
      while (myMeta.tracks[tid].lastms < pos && myConn && ++maxTime <= 20 && keepGoing()){
        Util::wait(500);
        stats();
        updateMeta();
      }
    }
    if (myMeta.tracks[tid].lastms < pos){
      WARN_MSG("Aborting seek to %llums in track %u: past end of track (= %llums).", pos, tid, myMeta.tracks[tid].lastms);
      selectedTracks.erase(tid);
      return false;
    }
    unsigned int keyNum = getKeyForTime(tid, pos);
    if (myMeta.tracks[tid].getKey(keyNum).getTime() > pos){
      if (myMeta.live){
        WARN_MSG("Actually seeking to %d, for %d is not available any more", myMeta.tracks[tid].getKey(keyNum).getTime(), pos);
        pos = myMeta.tracks[tid].getKey(keyNum).getTime();
      }
    }
    loadPageForKey(tid, keyNum + (getNextKey?1:0));
    if (!nProxy.curPage.count(tid) || !nProxy.curPage[tid].mapped){
      WARN_MSG("Aborting seek to %llums in track %u: not available.", pos, tid);
      selectedTracks.erase(tid);
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
      HIGH_MSG("Sought to time %llu in %s@%u", tmp.time, streamName.c_str(), tid);
      buffer.insert(tmp);
      return true;
    }else{
      //don't print anything for empty packets - not sign of corruption, just unfinished stream.
      if (nProxy.curPage[tid].mapped[tmp.offset] != 0){
        FAIL_MSG("Noes! Couldn't find packet on track %d because of some kind of corruption error or somesuch.", tid);
      }else{
        VERYHIGH_MSG("Track %d no data (key %u @ %u) - waiting...", tid, getKeyForTime(tid, pos) + (getNextKey?1:0), tmp.offset);
        unsigned int i = 0;
        while (!myMeta.live && nProxy.curPage[tid].mapped[tmp.offset] == 0 && ++i <= 10 && keepGoing()){
          Util::wait(100*i);
          stats();
        }
        if (nProxy.curPage[tid].mapped[tmp.offset] == 0){
          FAIL_MSG("Track %d no data (key %u@%llu) - timeout", tid, getKeyForTime(tid, pos) + (getNextKey?1:0), tmp.offset);
        }else{
          return seek(tid, pos, getNextKey);
        }
      }
      selectedTracks.erase(tid);
      return false;
    }
  }

  /// This function decides where in the stream initial playback starts.
  /// The default implementation calls seek(0) for VoD.
  /// For live, it seeks to the last sync'ed keyframe of the main track, no closer than needsLookAhead+minKeepAway ms from the end.
  /// Unless lastms < 5000, then it seeks to the first keyframe of the main track.
  /// Aborts if there is no main track or it has no keyframes.
  void Output::initialSeek(){
    unsigned long long seekPos = 0;
    if (myMeta.live){
      long unsigned int mainTrack = getMainSelectedTrack();
      //cancel if there are no keys in the main track
      if (!myMeta.tracks.count(mainTrack) || !myMeta.tracks[mainTrack].keys.size()){return;}
      //seek to the newest keyframe, unless that is <5s, then seek to the oldest keyframe
      for (std::deque<DTSC::Key>::reverse_iterator it = myMeta.tracks[mainTrack].keys.rbegin(); it != myMeta.tracks[mainTrack].keys.rend(); ++it){
        seekPos = it->getTime();
        if (seekPos < 5000){continue;}//if we're near the start, skip back
        bool good = true;
        //check if all tracks have data for this point in time
        for (std::set<unsigned long>::iterator ti = selectedTracks.begin(); ti != selectedTracks.end(); ++ti){
          if (!myMeta.tracks.count(*ti)){
            HIGH_MSG("Skipping track %lu, not in tracks", *ti);
            continue;
          }//ignore missing tracks
          DTSC::Track & thisTrack = myMeta.tracks[*ti];
          if (thisTrack.lastms < seekPos+needsLookAhead+thisTrack.minKeepAway){good = false; break;}
          if (mainTrack == *ti){continue;}//skip self
          if (thisTrack.lastms == thisTrack.firstms){
            HIGH_MSG("Skipping track %lu, last equals first", *ti);
            continue;
          }//ignore point-tracks
          HIGH_MSG("Track %lu is good", *ti);
        }
        //if yes, seek here
        if (good){break;}
      }
    } 
    /*LTS-START*/
    if (isRecordingToFile){
      if (myMeta.live && targetParams.count("recstartunix") || targetParams.count("recstopunix")){
        uint64_t unixStreamBegin = Util::epoch() - (liveTime() / 1000);
        if (targetParams.count("recstartunix")){
          long long startUnix = atoll(targetParams["recstartunix"].c_str());
          if (startUnix < unixStreamBegin){
            WARN_MSG("Recording start time is earlier than stream begin - starting earliest possible");
            targetParams["recstart"] = "-1";
          }else{
            targetParams["recstart"] = JSON::Value((long long)((startUnix - unixStreamBegin)*1000)).asString();
          }
        }
        if (targetParams.count("recstopunix")){
          long long stopUnix = atoll(targetParams["recstopunix"].c_str());
          if (stopUnix < unixStreamBegin){
            FAIL_MSG("Recording stop time is earlier than stream begin - aborting");
            onFail();
            return;
          }else{
            targetParams["recstop"] = JSON::Value((long long)((stopUnix - unixStreamBegin)*1000)).asString();
          }
        }
      }
      if (targetParams.count("recstop")){
        long long endRec = atoll(targetParams["recstop"].c_str());
        if (endRec < 0 || endRec < startTime()){
          FAIL_MSG("Entire recording range is in the past");
          onFail();
          return;
        }
        INFO_MSG("Recording will stop at %lld", endRec);
      }
      if (targetParams.count("recstart") && atoll(targetParams["recstart"].c_str()) != 0){
        unsigned long int mainTrack = getMainSelectedTrack();
        long long startRec = atoll(targetParams["recstart"].c_str());
        if (startRec > 0 && startRec > myMeta.tracks[mainTrack].lastms){
          if (myMeta.vod){
            FAIL_MSG("Recording start past end of non-live source");
            onFail();
            return;
          }
          INFO_MSG("Waiting for stream to reach recording starting point");
          long unsigned int streamAvail = myMeta.tracks[mainTrack].lastms;
          long unsigned int lastUpdated = Util::getMS();
          while (Util::getMS() - lastUpdated < 5000 && startRec > streamAvail && keepGoing()){
            Util::sleep(500);
            updateMeta();
            if (myMeta.tracks[mainTrack].lastms > streamAvail){
              stats();
              streamAvail = myMeta.tracks[mainTrack].lastms;
              lastUpdated = Util::getMS();
            }
          }
        }
        if (startRec < 0 || startRec < startTime()){
          WARN_MSG("Record begin at %llu ms not available, starting at %llu ms instead", startRec, startTime());
          startRec = startTime();
        }
        INFO_MSG("Recording will start at %lld", startRec);
        seekPos = startRec;
      }
    }
    /*LTS-END*/
    MEDIUM_MSG("Initial seek to %llums", seekPos);
    seek(seekPos);
  }

  /// This function attempts to forward playback in live streams to a more live point.
  /// It seeks to the last sync'ed keyframe of the main track, no closer than needsLookAhead+minKeepAway ms from the end.
  /// Aborts if not live, there is no main track or it has no keyframes.
  bool Output::liveSeek(){
    static uint32_t seekCount = 2;
    unsigned long long seekPos = 0;
    if (!myMeta.live){return false;}
    long unsigned int mainTrack = getMainSelectedTrack();
    //cancel if there are no keys in the main track
    if (!myMeta.tracks.count(mainTrack)){return false;}
    DTSC::Track & mainTrk = myMeta.tracks[mainTrack];
    if (!mainTrk.keys.size()){return false;}
    uint32_t minKeepAway = mainTrk.minKeepAway;

    for (std::deque<DTSC::Key>::reverse_iterator it = myMeta.tracks[mainTrack].keys.rbegin(); it != myMeta.tracks[mainTrack].keys.rend(); ++it){
      seekPos = it->getTime();
      //Only skip forward if we can win a decent amount
      if(seekPos <= thisPacket.getTime() + 250*seekCount){break;}
      bool good = true;
      //check if all tracks have data for this point in time
      for (std::set<unsigned long>::iterator ti = selectedTracks.begin(); ti != selectedTracks.end(); ++ti){
        if (!myMeta.tracks.count(*ti)){
          HIGH_MSG("Skipping track %lu, not in tracks", *ti);
          continue;
        }//ignore missing tracks
        DTSC::Track & thisTrack = myMeta.tracks[*ti];
        if (thisTrack.lastms < seekPos+needsLookAhead+thisTrack.minKeepAway){good = false; break;}
        if (mainTrack == *ti){continue;}//skip self
        if (thisTrack.lastms == thisTrack.firstms){
          HIGH_MSG("Skipping track %lu, last equals first", *ti);
          continue;
        }//ignore point-tracks
        HIGH_MSG("Track %lu is good", *ti);
      }
      //if yes, seek here
      if (good){
        INFO_MSG("Skipping forward %llums (%u ms LA, %lu ms mKA, > %lums)", seekPos - thisPacket.getTime(), needsLookAhead, mainTrk.minKeepAway, seekCount*250);
        if (seekCount < 20){++seekCount;}
        seek(seekPos);
        return true;
      }
    }
    return false;
  }

  void Output::requestHandler(){
    static bool firstData = true;//only the first time, we call onRequest if there's data buffered already.
    if ((firstData && myConn.Received().size()) || myConn.spool()){
      firstData = false;
      DONTEVEN_MSG("onRequest");
      onRequest();
      lastRecv = Util::epoch();
    }else{
      if (!isBlocking && !parseData){
        if (Util::epoch() - lastRecv > 300){
          WARN_MSG("Disconnecting 5 minute idle connection");
          myConn.close();
        }else{
          Util::sleep(500);
        }
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
  int Output::run(){
    /*LTS-START*/
    //Connect to file target, if needed
    if(isFileTarget()){
      if (!streamName.size()){
        WARN_MSG("Recording unconnected %s output to file! Cancelled.", capa["name"].asString().c_str());
        myConn.close();
        return 2;
      }
      initialize();
      if (!myMeta.tracks.size() || !selectedTracks.size() || !keepGoing()){
        INFO_MSG("Stream not available - aborting");
        myConn.close();
      }else{
        if (config->getString("target") == "-"){
          parseData = true;
          wantRequest = false;
          if (!targetParams.count("realtime")){realTime = 0;}
          INFO_MSG("Outputting %s to stdout with %s format", streamName.c_str(), capa["name"].asString().c_str());
        }else{
          if (connectToFile(config->getString("target"))){
            parseData = true;
            wantRequest = false;
            if (!targetParams.count("realtime")){realTime = 0;}
            INFO_MSG("Recording %s to %s with %s format", streamName.c_str(), config->getString("target").c_str(), capa["name"].asString().c_str());
          }else{
            myConn.close();
          }
        }
      }
    }
    //Handle CONN_OPEN trigger, if needed
    if(Triggers::shouldTrigger("CONN_OPEN", streamName)){
      std::string payload = streamName+"\n" + getConnectedHost() +"\n"+capa["name"].asStringRef()+"\n"+reqUrl;
      if (!Triggers::doTrigger("CONN_OPEN", payload, streamName)){
        return 1;
      }
    }
    /*LTS-END*/
    DONTEVEN_MSG("MistOut client handler started");
    while (keepGoing() && (wantRequest || parseData)){
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
          initialSeek();
        }
        if (prepareNext()){
          if (thisPacket){
            lastPacketTime = thisPacket.getTime();
            if (firstPacketTime == 0xFFFFFFFFFFFFFFFFull){
              firstPacketTime = lastPacketTime;
            }

            //slow down processing, if real time speed is wanted
            if (realTime){
              uint8_t i = 6;
              while (--i && thisPacket.getTime() > (((Util::getMS() - firstTime)*1000)+maxSkipAhead)/realTime && keepGoing()){
                Util::sleep(std::min(thisPacket.getTime() - (((Util::getMS() - firstTime)*1000)+maxSkipAhead)/realTime, 1000llu));
                stats();
              }
            }

            //delay the stream until metadata has caught up, if needed
            if (needsLookAhead){
              //we sleep in 250ms increments, or less if the lookahead time itself is less
              uint32_t sleepTime = std::min((uint32_t)250, needsLookAhead);
              //wait at most double the look ahead time, plus ten seconds
              uint32_t timeoutTries = (needsLookAhead / sleepTime) * 2 + (10000/sleepTime);
              uint64_t needsTime = thisPacket.getTime() + needsLookAhead; 
              while(--timeoutTries && keepGoing()){
                bool lookReady = true;
                for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
                  if (myMeta.tracks[*it].lastms <= needsTime){
                    if (timeoutTries == 1){
                      WARN_MSG("Track %lu: %llu <= %llu", *it, myMeta.tracks[*it].lastms, needsTime);
                    }
                    lookReady = false;
                    break;
                  }
                }
                if (lookReady){break;}
                Util::wait(sleepTime);
                stats();
                updateMeta();
              }
              if (!timeoutTries){
                WARN_MSG("Waiting for lookahead timed out - resetting lookahead!");
                needsLookAhead = 0;
              }
            }
            
            if (isRecordingToFile && targetParams.count("recstop") && atoll(targetParams["recstop"].c_str()) < lastPacketTime){
              INFO_MSG("End of planned recording reached, shutting down");
              if (!onFinish()){
                break;
              }
            }
            sendNext();
          }else{
            INFO_MSG("Shutting down because of stream end");
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
    onFinish();
    
    /*LTS-START*/
    if(Triggers::shouldTrigger("CONN_CLOSE", streamName)){
      std::string payload = streamName+"\n"+getConnectedHost()+"\n"+capa["name"].asStringRef()+"\n"+reqUrl;
      Triggers::doTrigger("CONN_CLOSE", payload, streamName);
    }
    if (isRecordingToFile && config->hasOption("target") && Triggers::shouldTrigger("RECORDING_END", streamName)){
      uint64_t rightNow = Util::epoch();
      std::stringstream payl;
      payl << streamName << '\n';
      payl << config->getString("target") << '\n';
      payl << capa["name"].asStringRef() << '\n';
      payl << myConn.dataUp() << '\n';
      payl << (rightNow - myConn.connTime()) << '\n';
      payl << myConn.connTime() << '\n';
      payl << rightNow << '\n';
      if (firstPacketTime != 0xFFFFFFFFFFFFFFFFull){
        payl << (lastPacketTime - firstPacketTime) << '\n';
      }else{
        payl << 0 << '\n';
      }
      payl << firstPacketTime << '\n';
      payl << lastPacketTime << '\n';
      Triggers::doTrigger("RECORDING_END", payl.str(), streamName);
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
  ///If it returns true and thisPacket evaluates to false, playback has completed.
  ///Could be called repeatedly in a loop if you really really want a new packet.
  /// \returns true if thisPacket was filled with the next packet.
  /// \returns false if we could not reliably determine the next packet yet.
  bool Output::prepareNext(){
    static bool atLivePoint = false;
    static int nonVideoCount = 0;
    static unsigned int emptyCount = 0;
    if (!buffer.size()){
      if (isRecording() && myMeta.live){
        selectDefaultTracks();
        initialSeek();
        return false;
      }
      thisPacket.null();
      INFO_MSG("Buffer completely played out");
      return true;
    }
    //check if we have a next seek point for every track that is selected
    if (buffer.size() != selectedTracks.size()){
      std::set<uint32_t> dropTracks;
      if (buffer.size() < selectedTracks.size()){
        //prepare to drop any selectedTrack without buffe entry
        for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); ++it){
          bool found = false;
          for (std::set<sortedPageInfo>::iterator bi = buffer.begin(); bi != buffer.end(); ++bi){
            if (bi->tid == *it){
              found = true;
              break;
            }
          }
          if (!found){
            dropTracks.insert(*it);
          }
        }
      }else{
        //prepare to drop any buffer entry without selectedTrack
        for (std::set<sortedPageInfo>::iterator bi = buffer.begin(); bi != buffer.end(); ++bi){
          if (!selectedTracks.count(bi->tid)){
            dropTracks.insert(bi->tid);
          }
        }
      }
      //actually drop what we found.
      //if both of the above cases occur, the next prepareNext iteration will take care of that
      for (std::set<uint32_t>::iterator it = dropTracks.begin(); it != dropTracks.end(); ++it){
        dropTrack(*it, "seek/select mismatch");
      }
      return false;
    }

    sortedPageInfo nxt = *(buffer.begin());

    if (!myMeta.tracks.count(nxt.tid)){
      dropTrack(nxt.tid, "disappeared from metadata");
      return false;
    }

    DONTEVEN_MSG("Loading track %u (next=%lu), %llu ms", nxt.tid, nxtKeyNum[nxt.tid], nxt.time);
   
    //if we're going to read past the end of the data page, load the next page
    //this only happens for VoD
    if (nxt.offset >= nProxy.curPage[nxt.tid].len){
      if (thisPacket){
        nxtKeyNum[nxt.tid] = getKeyForTime(nxt.tid, thisPacket.getTime());
      }
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
        dropTrack(nxt.tid, "page load failure");
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
      //for VoD, check if we've reached the end of the track, if so, drop it
      if (myMeta.vod && nxt.time > myMeta.tracks[nxt.tid].lastms){
        dropTrack(nxt.tid, "Reached end of track", false);
      }
      //if this is a live stream, we might have just reached the live point.
      //check where the next key is
      nxtKeyNum[nxt.tid] = getKeyForTime(nxt.tid, nxt.time);
      int nextPage = pageNumForKey(nxt.tid, nxtKeyNum[nxt.tid]+1);
      //if the next key hasn't shown up on another page, then we're waiting.
      //VoD might be slow, so we check VoD case also, just in case
      if (currKeyOpen.count(nxt.tid) && (currKeyOpen[nxt.tid] == (unsigned int)nextPage || nextPage == -1)){
        if (++emptyCount < 100){
          Util::wait(250);
          //we're waiting for new data to show up
          if (emptyCount % 64 == 0){
            reconnect();//reconnect every 16 seconds
          }else{
            //updating meta is only useful with live streams
            if (myMeta.live && emptyCount % 4 == 0){
              updateMeta();
            }
          }
        }else{
          //after ~25 seconds, give up and drop the track.
          dropTrack(nxt.tid, "EOP: data wait timeout");
        }
        return false;
      }

      //The next key showed up on another page!
      //We've simply reached the end of the page. Load the next key = next page.
      loadPageForKey(nxt.tid, ++nxtKeyNum[nxt.tid]);
      nxt.offset = 0;
      if (nProxy.curPage.count(nxt.tid) && nProxy.curPage[nxt.tid].mapped){
        unsigned long long nextTime = getDTSCTime(nProxy.curPage[nxt.tid].mapped, nxt.offset);
        if (nextTime && nextTime < nxt.time){
          dropTrack(nxt.tid, "EOP: time going backwards ("+JSON::Value((long long)nextTime).asString()+" < "+JSON::Value((long long)nxt.time).asString()+")");
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
    if (thisPacket.getTime() != nxt.time && nxt.time){
      if (!atLivePoint){
        static int warned = 0;
        if (warned < 5){
          WARN_MSG("Loaded %s track %ld@%llu instead of %u@%llu (%dms, %s, offset %lu)", streamName.c_str(), thisPacket.getTrackId(),
                   thisPacket.getTime(), nxt.tid, nxt.time, (int)((long long)thisPacket.getTime() - (long long)nxt.time),
                   myMeta.tracks[nxt.tid].codec.c_str(), nxt.offset);
          if (++warned == 5){WARN_MSG("Further warnings about time mismatches printed on HIGH level.");}
        }else{
          HIGH_MSG("Loaded %s track %ld@%llu instead of %u@%llu (%dms, %s, offset %lu)", streamName.c_str(), thisPacket.getTrackId(),
                   thisPacket.getTime(), nxt.tid, nxt.time, (int)((long long)thisPacket.getTime() - (long long)nxt.time),
                   myMeta.tracks[nxt.tid].codec.c_str(), nxt.offset);
        }
      }
      nxt.time = thisPacket.getTime();
      //swap out the next object in the buffer with a new one
      buffer.erase(buffer.begin());
      buffer.insert(nxt);
      VERYHIGH_MSG("JIT reordering %u@%llu.", nxt.tid, nxt.time);
      return false;
    }

    //when live, every keyframe, check correctness of the keyframe number
    if (myMeta.live && thisPacket.getFlag("keyframe")){
      //cancel if not alive
      if (!nProxy.userClient.isAlive()){
        return false;
      }
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
        WARN_MSG("Keyframe value is not correct (%llu != %llu) - state will now be inconsistent; resetting", myMeta.tracks[nxt.tid].getKey(nxtKeyNum[nxt.tid]).getTime(), thisPacket.getTime());
        initialSeek();
        return false;
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
    if (isPushing()){
      return "INPUT";
    }
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

    HIGH_MSG("Writing stats: %s, %s, %lu, %llu, %llu", getConnectedHost().c_str(), streamName.c_str(), crc & 0xFFFFFFFFu, myConn.dataUp(), myConn.dataDown());
    if (statsPage.getData()){
      /*LTS-START*/
      if (!statsPage.isAlive()){
        INFO_MSG("Shutting down on controller request");
        onFinish();
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
      /*LTS-START*/
      //Tag the session with the user agent
      static bool newUA = true;//we only do this once per connection
      if (newUA && ((now - myConn.connTime()) >= uaDelay || !myConn) && UA.size()){
        std::string APIcall = "{\"tag_sessid\":{\"" + tmpEx.getSessId() + "\":" + JSON::string_escape("UA:"+UA) + "}}";
        Socket::UDPConnection uSock;
        uSock.SetDestination("localhost", 4242);
        uSock.SendNow(APIcall);
        newUA = false;
      }
      /*LTS-END*/
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
        onFinish();
        myConn.close();
        return;
      }
    }
    if (!nProxy.userClient.isAlive()){
      if (isPushing() && !pushIsOngoing){
        waitForStreamPushReady();
        if (!nProxy.userClient.isAlive()){
          WARN_MSG("Failed to wait for buffer, aborting incoming push");
          onFinish();
          myConn.close();
          return;
        }
      }else{
        INFO_MSG("Received disconnect request from input");
        onFinish();
        myConn.close();
        return;
      }
    }
    if (!isPushing()){
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
  
  bool Output::connectToFile(std::string file){
    int flags = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    int mode = O_RDWR | O_CREAT | O_TRUNC;
    if (!Util::createPathFor(file)){
      ERROR_MSG("Cannot not create file %s: could not create parent folder", file.c_str());
      return false;
    }
    int outFile = open(file.c_str(), mode, flags);
    if (outFile < 0){
      ERROR_MSG("Failed to open file %s, error: %s", file.c_str(), strerror(errno));
      return false;
    }
    
    int r = dup2(outFile, myConn.getSocket());
    if (r == -1){
      ERROR_MSG("Failed to create an alias for the socket using dup2: %s.", strerror(errno));
      return false;
    }
    close(outFile);
    isRecordingToFile = true;
    sought = false;
    return true;
  }

  /// Checks if the set streamName allows pushes from this connector/IP/password combination.
  /// Runs all appropriate triggers and checks.
  /// Returns true if the push should continue, false otherwise.
  bool Output::allowPush(const std::string & passwd){
    pushing = true;
    std::string strmSource;

    // Initialize the stream source if needed, connect to it
    initialize();

    waitForStreamPushReady();
    //pull the source setting from metadata
    strmSource = myMeta.sourceURI;

    if (!strmSource.size()){
      FAIL_MSG("Push rejected - stream %s not configured or unavailable", streamName.c_str());
      pushing = false;
      return false;
    }
    if (strmSource.substr(0, 7) != "push://"){
      FAIL_MSG("Push rejected - stream %s not a push-able stream. (%s != push://*)", streamName.c_str(), strmSource.c_str());
      pushing = false;
      return false;
    }

    std::string source = strmSource.substr(7);
    std::string IP = source.substr(0, source.find('@'));

    /*LTS-START*/
    std::string password;
    if (source.find('@') != std::string::npos){
      password = source.substr(source.find('@')+1);
      if (password != ""){
        if (password == passwd){
          INFO_MSG("Password accepted - ignoring IP settings.");
          IP = "";
        }else{
          INFO_MSG("Password rejected - checking IP.");
          if (IP == ""){
            IP = "deny-all.invalid";
          }
        }
      }
    }

    std::string smp = streamName.substr(0, streamName.find_first_of("+ "));
    if(Triggers::shouldTrigger("STREAM_PUSH", smp)){
      std::string payload = streamName+"\n" + getConnectedHost() +"\n"+capa["name"].asStringRef()+"\n"+reqUrl;
      if (!Triggers::doTrigger("STREAM_PUSH", payload, smp)){
        FAIL_MSG("Push from %s to %s rejected - STREAM_PUSH trigger denied the push", getConnectedHost().c_str(), streamName.c_str());
        pushing = false;
        return false;
      }
    }
    /*LTS-END*/

    if (IP != ""){
      if (!myConn.isAddress(IP)){
        FAIL_MSG("Push from %s to %s rejected - source host not whitelisted", getConnectedHost().c_str(), streamName.c_str());
        pushing = false;
        return false;
      }
    }
    return true;
  }

  /// Attempts to wait for a stream to finish shutting down if it is, then restarts and reconnects.
  void Output::waitForStreamPushReady(){
    uint8_t streamStatus = Util::getStreamStatus(streamName);
    MEDIUM_MSG("Current status for %s buffer is %u", streamName.c_str(), streamStatus);
    while (streamStatus != STRMSTAT_WAIT && streamStatus != STRMSTAT_READY && keepGoing()){
      INFO_MSG("Waiting for %s buffer to be ready... (%u)", streamName.c_str(), streamStatus);
      if (nProxy.userClient.getData()){
        nProxy.userClient.finish();
        nProxy.userClient = IPC::sharedClient();
      }
      if (statsPage.getData()){
        statsPage.keepAlive();
      }
      Util::wait(1000);
      streamStatus = Util::getStreamStatus(streamName);
      if (streamStatus == STRMSTAT_OFF || streamStatus == STRMSTAT_WAIT || streamStatus == STRMSTAT_READY){
        INFO_MSG("Reconnecting to %s buffer... (%u)", streamName.c_str(), streamStatus);
        reconnect();
        streamStatus = Util::getStreamStatus(streamName);
      }
    }
  }

}

