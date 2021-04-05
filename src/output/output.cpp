#include <algorithm>
#include <fcntl.h>
#include <iterator> //std::distance
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "output.h"
#include <mist/bitfields.h>
#include <mist/defines.h>
#include <mist/h264.h>
#include <mist/http_parser.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/util.h>

/*LTS-START*/
#include <arpa/inet.h>
#include <mist/langcodes.h>
#include <mist/triggers.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
/*LTS-END*/

namespace Mist{
  JSON::Value Output::capa = JSON::Value();
  Util::Config *Output::config = NULL;

  uint32_t getDTSCLen(char *mapped, uint64_t offset){return Bit::btohl(mapped + offset + 4);}

  uint64_t getDTSCTime(char *mapped, uint64_t offset){return Bit::btohll(mapped + offset + 12);}

  void Output::init(Util::Config *cfg){
    capa["optional"]["debug"]["name"] = "debug";
    capa["optional"]["debug"]["help"] = "The debug level at which messages need to be printed.";
    capa["optional"]["debug"]["option"] = "--debug";
    capa["optional"]["debug"]["type"] = "debug";

    JSON::Value option;
    option["long"] = "noinput";
    option["short"] = "N";
    option["help"] = "Do not start input if not already started";
    option["value"].append(0);
    cfg->addOption("noinput", option);
    config = cfg;
  }

  Output::Output(Socket::Connection &conn) : myConn(conn){
    pushing = false;
    recursingSync = false;
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
    extraKeepAway = 0;
    lastStats = 0;
    maxSkipAhead = 7500;
    uaDelay = 10;
    realTime = 1000;
    emptyCount = 0;
    seekCount = 2;
    firstData = true;
    newUA = true;
    lastPushUpdate = 0;

    lastRecv = Util::bootSecs();
    if (myConn){
      setBlocking(true);
      //Make sure that if the socket is a non-stdio socket, we close it when forking
      if (myConn.getSocket() > 2){
        Util::Procs::socketList.insert(myConn.getSocket());
      }
    }else{
      WARN_MSG("Warning: MistOut created with closed socket!");
    }
    sentHeader = false;
    isRecordingToFile = false;

    // If we have a streamname option, set internal streamname to that option
    if (!streamName.size() && config->hasOption("streamname")){
      streamName = config->getString("streamname");
      Util::setStreamName(streamName);
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
    if (targetParams.count("rate")){
      long long int multiplier = JSON::Value(targetParams["rate"]).asInt();
      if (multiplier){
        realTime = 1000 / multiplier;
      }else{
        realTime = 0;
      }
    }
    if (isRecording() && DTSC::trackValidMask == TRACK_VALID_EXT_HUMAN){
      DTSC::trackValidMask = TRACK_VALID_EXT_PUSH;
      if (targetParams.count("unmask")){DTSC::trackValidMask = TRACK_VALID_ALL;}
    }
    /*LTS-END*/
  }

  bool Output::isFileTarget(){
    VERYHIGH_MSG("Default file target handler (false)");
    return false;
  }

  void Output::listener(Util::Config &conf, int (*callback)(Socket::Connection &S)){
    conf.serveForkedSocket(callback);
  }

  void Output::setBlocking(bool blocking){
    isBlocking = blocking;
    myConn.setBlocking(isBlocking);
  }

  bool Output::isRecording(){
    return config->hasOption("target") && config->getString("target").size();
  }

  /// Called when stream initialization has failed.
  /// The standard implementation will set isInitialized to false and close the client connection,
  /// thus causing the process to exit cleanly.
  void Output::onFail(const std::string &msg, bool critical){
    if (critical){
      FAIL_MSG("onFail '%s': %s", streamName.c_str(), msg.c_str());
    }else{
      MEDIUM_MSG("onFail '%s': %s", streamName.c_str(), msg.c_str());
    }
    Util::logExitReason(msg.c_str());
    isInitialized = false;
    wantRequest = true;
    parseData = false;
    myConn.close();
  }

  void Output::initialize(){
    MEDIUM_MSG("initialize");
    if (isInitialized){return;}
    if (streamName.size() < 1){
      return; // abort - no stream to initialize...
    }
    reconnect();
    // if the connection failed, fail
    if (!meta || streamName.size() < 1){
      onFail("Could not connect to stream", true);
      return;
    }
    sought = false;
    /*LTS-START*/
    if (Triggers::shouldTrigger("CONN_PLAY", streamName)){
      std::string payload =
          streamName + "\n" + getConnectedHost() + "\n" + capa["name"].asStringRef() + "\n" + reqUrl;
      if (!Triggers::doTrigger("CONN_PLAY", payload, streamName)){
        onFail("Not allowed to play (CONN_PLAY)");
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
    if (!statComm){return;}
    if (recursingSync){return;}
    recursingSync = true;
    if (statComm.getSync() == 2 || force){
      if (getStatsName() == capa["name"].asStringRef() && Triggers::shouldTrigger("USER_NEW", streamName)){
        // sync byte 0 = no sync yet, wait for sync from controller...
        char initialSync = 0;
        // attempt to load sync status from session cache in shm
        {
          IPC::semaphore cacheLock(SEM_SESSCACHE, O_RDWR, ACCESSPERMS, 16);
          if (cacheLock){cacheLock.wait();}
          IPC::sharedPage shmSessions(SHM_SESSIONS, SHM_SESSIONS_SIZE, false, false);
          if (shmSessions.mapped){
            char shmEmpty[SHM_SESSIONS_ITEM];
            memset(shmEmpty, 0, SHM_SESSIONS_ITEM);
            std::string host;
            Socket::hostBytesToStr(statComm.getHost().data(), 16, host);
            uint32_t shmOffset = 0;
            const std::string &cName = capa["name"].asStringRef();
            while (shmOffset + SHM_SESSIONS_ITEM < SHM_SESSIONS_SIZE){
              // compare crc
              if (*(uint32_t*)(shmSessions.mapped + shmOffset) == crc){
                // compare stream name
                if (strncmp(shmSessions.mapped + shmOffset + 4, streamName.c_str(), 100) == 0){
                  // compare connector
                  if (strncmp(shmSessions.mapped + shmOffset + 104, cName.c_str(), 20) == 0){
                    // compare host
                    if (strncmp(shmSessions.mapped + shmOffset + 124, host.c_str(), 40) == 0){
                      initialSync = shmSessions.mapped[shmOffset + 164];
                      HIGH_MSG("Instant-sync from session cache to %u", (unsigned int)initialSync);
                      break;
                    }
                  }
                }
              }
              // stop if we reached the end
              if (memcmp(shmSessions.mapped + shmOffset, shmEmpty, SHM_SESSIONS_ITEM) == 0){
                break;
              }
              shmOffset += SHM_SESSIONS_ITEM;
            }
          }
          if (cacheLock){cacheLock.post();}
        }
        unsigned int i = 0;
        statComm.setSync(initialSync);
        // wait max 10 seconds for sync
        while ((!statComm.getSync() || statComm.getSync() == 2) && i++ < 100){
          Util::wait(100);
          stats(true);
        }
        HIGH_MSG("USER_NEW sync achieved: %u", statComm.getSync());
        // 1 = check requested (connection is new)
        if (statComm.getSync() == 1){
          std::string payload = streamName + "\n" + getConnectedHost() + "\n" +
                                JSON::Value(crc).asString() + "\n" + capa["name"].asStringRef() +
                                "\n" + reqUrl + "\n" + statComm.getSessId();
          if (!Triggers::doTrigger("USER_NEW", payload, streamName)){
            onFail("Not allowed to play (USER_NEW)");
            statComm.setSync(100); // 100 = denied
          }else{
            statComm.setSync(10); // 10 = accepted
          }
        }
        // 100 = denied
        if (statComm.getSync() == 100){onFail("Not allowed to play (USER_NEW cache)");}
        if (statComm.getSync() == 0){
          onFail("Not allowed to play (USER_NEW init timeout)", true);
        }
        if (statComm.getSync() == 2){
          onFail("Not allowed to play (USER_NEW re-init timeout)", true);
        }
        // anything else = accepted
      }else{
        statComm.setSync(10); // auto-accept if no trigger
      }
    }
    recursingSync = false;
  }

  std::string Output::getConnectedHost(){return myConn.getHost();}

  std::string Output::getConnectedBinHost(){
    if (!prevHost.size()){
      if (myConn && myConn.getPureSocket() != -1){
        prevHost = myConn.getBinHost();
      }
      if (!prevHost.size()){prevHost.assign("\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\001", 16);}
    }
    return prevHost;
  }

  bool Output::isReadyForPlay(){
    // If a protocol does not support any codecs, we assume you know what you're doing
    if (!capa.isMember("codecs")){return true;}
    if (!isInitialized){initialize();}
    meta.reloadReplacedPagesIfNeeded();
    if (getSupportedTracks().size()){
      if (!userSelect.size()){selectDefaultTracks();}
      size_t mainTrack = getMainSelectedTrack();
      if (mainTrack != INVALID_TRACK_ID){
        DTSC::Keys keys(M.keys(mainTrack));
        if (keys.getValidCount() >= 2 || M.getLastms(mainTrack) - M.getFirstms(mainTrack) > 5000){
          return true;
        }
        HIGH_MSG("NOT READY YET (%zu tracks, main track: %zu, with %zu keys)",
                 M.getValidTracks().size(), getMainSelectedTrack(), keys.getValidCount());
      }else{
        HIGH_MSG("NOT READY YET (%zu tracks)", getSupportedTracks().size());
      }
    }else{
      HIGH_MSG("NOT READY (%zu tracks)", getSupportedTracks().size());
    }
    return false;
  }

  /// Disconnects from all stat/user-related shared structures.
  void Output::disconnect(){
    MEDIUM_MSG("disconnect");
    if (statComm){
      statComm.unload();
      myConn.resetCounter();
    }
    userSelect.clear();
    isInitialized = false;
    meta.clear();
  }

  /// Connects or reconnects to the stream.
  /// Assumes streamName class member has been set already.
  /// Will start input if not currently active, calls onFail() if this does not succeed.
  /// After assuring stream is online, clears metaPages, then sets metaPages[0], statistics and
  /// userClient to (hopefully) valid handles.
  void Output::reconnect(){
    thisPacket.null();
    if (config->hasOption("noinput") && config->getBool("noinput")){
      Util::sanitizeName(streamName);
      if (!Util::streamAlive(streamName)){
        onFail("Stream not active already, aborting");
        return;
      }
    }else{
      if (!Util::startInput(streamName, "", true, isPushing())){
        // If stream is configured, use fallback stream setting, if set.
        JSON::Value strCnf = Util::getStreamConfig(streamName);
        if (strCnf && strCnf["fallback_stream"].asStringRef().size()){
          streamName = strCnf["fallback_stream"].asStringRef();
          Util::setStreamName(streamName);
          INFO_MSG("Switching to configured fallback stream '%s'", streamName.c_str());
          reconnect();
          return;
        }

        // Not configured or no fallback stream? Use the default stream handler instead
        // Note: Since fallback stream is handled recursively, the defaultStream handler
        // may still be triggered for the fallback stream! This is intentional.
        JSON::Value defStrmJson = Util::getGlobalConfig("defaultStream");
        std::string defStrm = defStrmJson.asString();
        if (Triggers::shouldTrigger("DEFAULT_STREAM", streamName)){
          std::string payload = defStrm + "\n" + streamName + "\n" + getConnectedHost() + "\n" +
                                capa["name"].asStringRef() + "\n" + reqUrl;
          // The return value is ignored, because the response (defStrm in this case) tells us what to do next, if anything.
          Triggers::doTrigger("DEFAULT_STREAM", payload, streamName, false, defStrm);
        }
        if (!defStrm.size()){
          onFail("Stream open failed", true);
          return;
        }
        std::string newStrm = defStrm;
        Util::streamVariables(newStrm, streamName, "");
        if (streamName == newStrm){
          onFail("Stream open failed; nothing to fall back to (" + defStrm + " == " + newStrm + ")", true);
          return;
        }
        INFO_MSG("Stream open failed; falling back to default stream '%s' -> '%s'", defStrm.c_str(),
                 newStrm.c_str());
        std::string origStream = streamName;
        streamName = newStrm;
        Util::setStreamName(streamName);
        if (!Util::startInput(streamName, "", true, isPushing())){
          onFail("Stream open failed (fallback stream for '" + origStream + "')", true);
          return;
        }
      }
    }

    disconnect();

    meta.reInit(streamName, false);
    unsigned int attempts = 0;
    while (!meta && ++attempts < 20 && Util::streamAlive(streamName)){
      meta.reInit(streamName, false);
    }
    if (!meta){return;}
    isInitialized = true;
    statComm.reload();
    stats(true);
    if (isPushing()){return;}
    if (!isRecording() && !M.getVod() && !isReadyForPlay()){
      uint64_t waitUntil = Util::bootSecs() + 45;
      while (!M.getVod() && !isReadyForPlay()){
        if (Util::bootSecs() > waitUntil || (!userSelect.size() && Util::bootSecs() > waitUntil)){
          INFO_MSG("Giving up waiting for playable tracks. IP: %s", getConnectedHost().c_str());
          break;
        }
        Util::wait(500);
        meta.reloadReplacedPagesIfNeeded();
        stats();
      }
    }
    selectDefaultTracks();
  }

  std::set<size_t> Output::getSupportedTracks(const std::string &type) const{
    return Util::getSupportedTracks(M, capa, type);
  }

  /// Automatically selects the tracks that are possible and/or wanted.
  /// Returns true if the track selection changed in any way.
  bool Output::selectDefaultTracks(){
    if (!isInitialized){
      initialize();
      if (!isInitialized){return false;}
    }

    meta.reloadReplacedPagesIfNeeded();

    bool autoSeek = buffer.size();
    uint64_t seekTarget = currentTime();
    std::set<size_t> newSelects =
        Util::wouldSelect(M, targetParams, capa, UA, autoSeek ? seekTarget : 0);

    if (autoSeek){
      std::set<size_t> toRemove;
      for (std::set<size_t>::iterator it = newSelects.begin(); it != newSelects.end(); it++){
        // autoSeeking and target not in bounds? Drop it too.
        if (M.getLastms(*it) < std::max(seekTarget, (uint64_t)6000lu) - 6000){
          toRemove.insert(*it);
        }
      }
      // remove those from selectedtracks
      for (std::set<size_t>::iterator it = toRemove.begin(); it != toRemove.end(); it++){
        newSelects.erase(*it);
      }
    }

    std::set<size_t> oldSelects;
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); ++it){
      oldSelects.insert(it->first);
    }
    //No changes? Abort and return false;
    if (oldSelects == newSelects){return false;}

    //Temp set holding the differences between old and new track selections
    std::set<size_t> diffs;

    //Find elements in old selection but not in new selection
    std::set_difference(oldSelects.begin(), oldSelects.end(), newSelects.begin(), newSelects.end(), std::inserter(diffs, diffs.end()));
    if (diffs.size()){MEDIUM_MSG("Dropping %zu tracks", diffs.size());}
    for (std::set<size_t>::iterator it = diffs.begin(); it != diffs.end(); it++){
      HIGH_MSG("Dropping track %zu", *it);
      userSelect.erase(*it);
    }

    //Find elements in new selection but not in old selection
    diffs.clear();
    std::set_difference(newSelects.begin(), newSelects.end(), oldSelects.begin(), oldSelects.end(), std::inserter(diffs, diffs.end()));
    if (diffs.size()){MEDIUM_MSG("Adding %zu tracks", diffs.size());}
    for (std::set<size_t>::iterator it = diffs.begin(); it != diffs.end(); it++){
      HIGH_MSG("Adding track %zu", *it);
      userSelect[*it].reload(streamName, *it);
      if (!userSelect[*it]){
        WARN_MSG("Could not select track %zu, dropping track", *it);
        newSelects.erase(*it);
        userSelect.erase(*it);
      }
    }

    newSelects.clear();
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); ++it){
      newSelects.insert(it->first);
    }

    //After attempting to add/remove tracks, now no changes? Abort and return false;
    if (oldSelects == newSelects){return false;}


    if (autoSeek){
      INFO_MSG("Automatically seeking to position %" PRIu64 " to resume playback", seekTarget);
      seek(seekTarget);
    }
    return true;
  }

  /// Clears the buffer, sets parseData to false, and generally makes not very much happen at all.
  void Output::stop(){
    buffer.clear();
    parseData = false;
  }

  ///Returns the timestamp of the next upcoming keyframe after thisPacket, or 0 if that cannot be determined (yet).
  uint64_t Output::nextKeyTime(){
    size_t trk = thisPacket.getTrackId();
    //If this is a video packet, we assume this is the main track and we don't look anything up
    if (M.getType(trk) != "video"){
      //For non-video packets, we get the main selected track
      trk = getMainSelectedTrack();
      //Unless that gives us an invalid track ID, then we fall back to the current track
      if (trk == INVALID_TRACK_ID){trk = thisPacket.getTrackId();}
    }
    //Abort if the track is not loaded
    if (!M.trackLoaded(trk)){return 0;}
    const DTSC::Keys &keys = M.keys(trk);
    //Abort if there are no keys
    if (!keys.getValidCount()){return 0;}
    //Get the key for the current time
    size_t keyNum = M.getKeyNumForTime(trk, lastPacketTime);
    if (keys.getEndValid() <= keyNum+1){return 0;}
    //Return the next key
    return keys.getTime(keyNum+1);
  }
  
  uint64_t Output::pageNumForKey(size_t trackId, size_t keyNum){
    const Util::RelAccX &tPages = M.pages(trackId);
    for (uint64_t i = tPages.getDeleted(); i < tPages.getEndPos(); i++){
      uint64_t pageNum = tPages.getInt("firstkey", i);
      if (pageNum > keyNum) break;
      uint64_t pageKeys = tPages.getInt("keycount", i);
      if (keyNum > pageNum + pageKeys - 1) continue;
      uint64_t pageAvail = tPages.getInt("avail", i);
      return pageAvail == 0 ? INVALID_KEY_NUM : pageNum;
    }
    return INVALID_KEY_NUM;
  }

  /// Gets the highest page number available for the given trackId.
  uint64_t Output::pageNumMax(size_t trackId){
    const Util::RelAccX &tPages = M.pages(trackId);
    uint64_t highest = 0;
    for (uint64_t i = tPages.getDeleted(); i < tPages.getEndPos(); i++){
      uint64_t pageNum = tPages.getInt("firstkey", i);
      if (pageNum > highest){highest = pageNum;}
    }
    return highest;
  }

  /// Loads the page for the given trackId and keyNum into memory.
  /// Overwrites any existing page for the same trackId.
  /// Automatically calls thisPacket.null() if necessary.
  void Output::loadPageForKey(size_t trackId, size_t keyNum){
    if (!M.trackValid(trackId)){
      WARN_MSG("Load for track %zu key %zu aborted - track does not exist", trackId, keyNum);
      return;
    }
    if (!M.trackLoaded(trackId)){meta.reloadReplacedPagesIfNeeded();}
    DTSC::Keys keys(M.keys(trackId));
    if (!keys.getValidCount()){
      WARN_MSG("Load for track %zu key %zu aborted - track is empty", trackId, keyNum);
      return;
    }
    size_t lastAvailKey = keys.getEndValid() - 1;
    if (meta.getVod() && keyNum > lastAvailKey){
      INFO_MSG("Load for track %zu key %zu aborted, is > %zu", trackId, keyNum, lastAvailKey);
      curPage.erase(trackId);
      currentPage.erase(trackId);
      return;
    }
    uint64_t micros = Util::getMicros();
    VERYHIGH_MSG("Loading track %zu, containing key %zu", trackId, keyNum);
    uint32_t timeout = 0;
    uint32_t pageNum = pageNumForKey(trackId, keyNum);
    while (keepGoing() && pageNum == INVALID_KEY_NUM){
      if (!timeout){HIGH_MSG("Requesting page with key %zu:%zu", trackId, keyNum);}
      ++timeout;
      //Time out after 15 seconds
      if (timeout > 300){
        FAIL_MSG("Timeout while waiting for requested key %zu for track %zu. Aborting.", keyNum, trackId);
        curPage.erase(trackId);
        currentPage.erase(trackId);
        return;
      }
      if (!userSelect.count(trackId) || !userSelect[trackId]){
        WARN_MSG("Loading page for non-selected track %zu", trackId);
      }else{
        userSelect[trackId].setKeyNum(keyNum);
      }

      stats(true);
      playbackSleep(50);
      pageNum = pageNumForKey(trackId, keyNum);
    }

    if (!keepGoing()){
      INFO_MSG("Aborting page load due to shutdown");
      return;
    }

    if (!userSelect.count(trackId) || !userSelect[trackId]){
      WARN_MSG("Loading page for non-selected track %zu", trackId);
    }else{
      userSelect[trackId].setKeyNum(keyNum);
    }

    stats(true);

    if (currentPage.count(trackId) && currentPage[trackId] == pageNum){return;}
    // If we're loading the track thisPacket is on, null it to prevent accesses.
    if (thisPacket && thisIdx == trackId){thisPacket.null();}
    char id[NAME_BUFFER_SIZE];
    snprintf(id, NAME_BUFFER_SIZE, SHM_TRACK_DATA, streamName.c_str(), trackId, pageNum);
    curPage[trackId].init(id, DEFAULT_DATA_PAGE_SIZE);
    if (!(curPage[trackId].mapped)){
      FAIL_MSG("Initializing page %s failed", curPage[trackId].name.c_str());
      currentPage.erase(trackId);
      return;
    }
    currentPage[trackId] = pageNum;
    micros = Util::getMicros(micros);
    if (micros > 2000000){
      INFO_MSG("Page %s loaded for %s in %.2fms", id, streamName.c_str(), micros/1000.0);
    }else{
      VERYHIGH_MSG("Page %s loaded for %s in %.2fms", id, streamName.c_str(), micros/1000.0);
    }
  }

  /// Return the current time of the media buffer, or 0 if no buffer available.
  uint64_t Output::currentTime(){
    if (!buffer.size()){return 0;}
    return buffer.begin()->time;
  }

  /// Return the start time of the selected tracks.
  /// Returns the start time of earliest track if nothing is selected.
  /// Returns zero if no tracks exist.
  uint64_t Output::startTime(){
    std::set<size_t> validTracks = M.getValidTracks();
    if (!validTracks.size()){return 0;}
    uint64_t start = 0xFFFFFFFFFFFFFFFFull;
    if (userSelect.size()){
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        if (M.trackValid(it->first) && start > M.getFirstms(it->first)){
          start = M.getFirstms(it->first);
        }
      }
    }else{
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
        if (start > M.getFirstms(*it)){start = M.getFirstms(*it);}
      }
    }
    return start;
  }

  /// Return the end time of the selected tracks, or 0 if unknown or live.
  /// Returns the end time of latest track if nothing is selected.
  /// Returns zero if no tracks exist.
  uint64_t Output::endTime(){
    std::set<size_t> validTracks = M.getValidTracks();
    if (!validTracks.size()){return 0;}
    uint64_t end = 0;
    if (userSelect.size()){
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        if (M.trackValid(it->first) && end < M.getLastms(it->first)){
          end = meta.getLastms(it->first);
        }
      }
    }else{
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
        if (end < meta.getLastms(*it)){end = meta.getLastms(*it);}
      }
    }
    return end;
  }

  /// Prepares all tracks from selectedTracks for seeking to the specified ms position.
  void Output::seekKeyframesIn(unsigned long long pos, unsigned long long maxDelta){
    sought = true;
    if (!isInitialized){initialize();}
    buffer.clear();
    thisPacket.null();
    MEDIUM_MSG("Seeking keyframes near %llums, max delta of %llu", pos, maxDelta);
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      if (!M.getValidTracks().count(it->first)){continue;}
      uint64_t time = M.getTimeForKeyIndex(it->first, M.getKeyIndexForTime(it->first, pos));
      uint64_t timeDelta = M.getTimeForKeyIndex(it->first, M.getKeyIndexForTime(it->first, pos + maxDelta));
      if (time >= (pos - maxDelta)){
        pos = time;
      }else if (timeDelta >= (pos - maxDelta)){
        pos = timeDelta;
      }
      seek(it->first, pos, false);
    }
  }

  /// Prepares all tracks from selectedTracks for seeking to the specified ms position.
  /// If toKey is true, clips the seek to the nearest keyframe if the main track is a video track.
  bool Output::seek(uint64_t pos, bool toKey){
    sought = true;
    if (!isInitialized){initialize();}
    buffer.clear();
    thisPacket.null();
    if (toKey){
      size_t mainTrack = getMainSelectedTrack();
      if (mainTrack == INVALID_TRACK_ID){
        WARN_MSG("Sync-seeking impossible (main track invalid); performing regular seek instead");
        return seek(pos);
      }
      if (M.getType(mainTrack) == "video"){
        DTSC::Keys keys(M.keys(mainTrack));
        uint32_t keyNum = M.getKeyNumForTime(mainTrack, pos);
        pos = keys.getTime(keyNum);
      }
    }
    MEDIUM_MSG("Seeking to %" PRIu64 "ms (%s)", pos, toKey ? "sync" : "direct");
    std::set<size_t> seekTracks;
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      seekTracks.insert(it->first);
    }
    bool ret = seekTracks.size();
    for (std::set<size_t>::iterator it = seekTracks.begin(); it != seekTracks.end(); it++){
      ret &= seek(*it, pos, false);
    }
    firstTime = Util::bootMS() - (currentTime() * realTime / 1000);
    return ret;
  }

  bool Output::seek(size_t tid, uint64_t pos, bool getNextKey){
    if (!M.trackValid(tid)){
      MEDIUM_MSG("Aborting seek to %" PRIu64 "ms in track %zu: Invalid track id.", pos, tid);
      userSelect.erase(tid);
      return false;
    }
    if (!M.trackLoaded(tid)){meta.reloadReplacedPagesIfNeeded();}
    if (!userSelect.count(tid) || !userSelect[tid]){
      WARN_MSG("Aborting seek to %" PRIu64 "ms in track %zu: user select failure (%s)", pos, tid, userSelect.count(tid)?"not connected":"not selected");
      userSelect.erase(tid);
      return false;
    }

    HIGH_MSG("Seeking for pos %" PRIu64, pos);
    if (meta.getLive() && meta.getLastms(tid) < pos){
      unsigned int maxTime = 0;
      while (meta.getLastms(tid) < pos && myConn && ++maxTime <= 20){
        Util::wait(500);
        stats();
      }
    }
    if (meta.getLastms(tid) < pos){
      WARN_MSG("Aborting seek to %" PRIu64 "ms in track %zu: past end of track (= %" PRIu64 "ms).",
               pos, tid, meta.getLastms(tid));
      userSelect.erase(tid);
      return false;
    }
    DTSC::Keys keys(M.keys(tid));
    uint32_t keyNum = M.getKeyNumForTime(tid, pos);
    uint64_t actualKeyTime = keys.getTime(keyNum);
    HIGH_MSG("Seeking to track %zu key %" PRIu32 " => time %" PRIu64, tid, keyNum, pos);
    if (actualKeyTime > pos){
      if (M.getLive()){
        pos = actualKeyTime;
        userSelect[tid].setKeyNum(keyNum);
      }
    }
    loadPageForKey(tid, keyNum + (getNextKey ? 1 : 0));
    if (!curPage.count(tid) || !curPage[tid].mapped){
      WARN_MSG("Aborting seek to %" PRIu64 "ms in track %zu: not available.", pos, tid);
      userSelect.erase(tid);
      return false;
    }
    sortedPageInfo tmp;
    tmp.tid = tid;
    tmp.offset = 0;
    tmp.partIndex = 0;
    DTSC::Packet tmpPack;
    tmpPack.reInit(curPage[tid].mapped + tmp.offset, 0, true);
    tmp.time = tmpPack.getTime();
    char *mpd = curPage[tid].mapped;
    while (tmp.time < pos && tmpPack){
      tmp.offset += tmpPack.getDataLen();
      tmpPack.reInit(mpd + tmp.offset, 0, true);
      tmp.time = tmpPack.getTime();
    }
    if (tmpPack){
      HIGH_MSG("Sought to time %" PRIu64 " (yields a packet at %" PRIu64 "ms) in %s@%zu", tmp.time,
               tmpPack.getTime(), streamName.c_str(), tid);
      tmp.partIndex = M.getPartIndex(tmpPack.getTime(), tmp.tid);
      buffer.insert(tmp);
      return true;
    }
    // don't print anything for empty packets - not sign of corruption, just unfinished stream.
    if (curPage[tid].mapped[tmp.offset] != 0){
      FAIL_MSG("Noes! Couldn't find packet on track %zu because of some kind of corruption error "
               "or somesuch.",
               tid);
      return false;
    }
    VERYHIGH_MSG("Track %zu no data (key %" PRIu32 " @ %" PRIu64 ") - waiting...", tid,
                 keyNum + (getNextKey ? 1 : 0), tmp.offset);
    uint32_t i = 0;
    while (!meta.getLive() && curPage[tid].mapped[tmp.offset] == 0 && ++i <= 10){
      Util::wait(100 * i);
      stats();
    }
    if (curPage[tid].mapped[tmp.offset]){return seek(tid, pos, getNextKey);}
    FAIL_MSG("Track %zu no data (key %" PRIu32 "@%" PRIu64 ", page %s, time %" PRIu64 " -> %" PRIu64 ", next=%" PRIu64 ") - timeout", tid, keyNum + (getNextKey ? 1 : 0), tmp.offset, curPage[tid].name.c_str(), pos, actualKeyTime, keys.getTime(keyNum+1));
    userSelect.erase(tid);
    firstTime = Util::bootMS() - (buffer.begin()->time * realTime / 1000);
    return false;
  }

  /// This function decides where in the stream initial playback starts.
  /// The default implementation calls seek(0) for VoD.
  /// For live, it seeks to the last sync'ed keyframe of the main track, no closer than
  /// needsLookAhead+minKeepAway ms from the end. Unless lastms < 5000, then it seeks to the first
  /// keyframe of the main track. Aborts if there is no main track or it has no keyframes.
  void Output::initialSeek(){
    if (!meta){return;}
    uint64_t seekPos = 0;
    if (meta.getLive()){
      size_t mainTrack = getMainSelectedTrack();
      if (mainTrack == INVALID_TRACK_ID){return;}
      DTSC::Keys keys(M.keys(mainTrack));
      if (!keys.getValidCount()){return;}
      // seek to the newest keyframe, unless that is <5s, then seek to the oldest keyframe
      uint32_t firstKey = keys.getFirstValid();
      uint32_t lastKey = keys.getEndValid() - 1;
      for (int64_t i = lastKey; i >= firstKey; i--){
        seekPos = keys.getTime(i);
        if (seekPos < 5000){continue;}// if we're near the start, skip back
        bool good = true;
        // check if all tracks have data for this point in time
        for (std::map<size_t, Comms::Users>::iterator ti = userSelect.begin(); ti != userSelect.end(); ++ti){
          if (meta.getLastms(ti->first) < seekPos + needsLookAhead){
            good = false;
            break;
          }
          if (mainTrack == ti->first){continue;}// skip self
          if (!M.trackValid(ti->first)){
            HIGH_MSG("Skipping track %lu, not in tracks", ti->first);
            continue;
          }// ignore missing tracks
          if (M.getLastms(ti->first) < seekPos + needsLookAhead + extraKeepAway + M.getMinKeepAway(ti->first)){
            good = false;
            break;
          }
          if (meta.getLastms(ti->first) == M.getFirstms(ti->first)){
            HIGH_MSG("Skipping track %lu, last equals first", ti->first);
            continue;
          }// ignore point-tracks
          if (meta.getLastms(ti->first) < seekPos){
            good = false;
            break;
          }
          HIGH_MSG("Track %zu is good", ti->first);
        }
        // if yes, seek here
        if (good){break;}
      }
    }
    /*LTS-START*/
    if (isRecordingToFile){
      if (M.getLive() &&
          (targetParams.count("recstartunix") || targetParams.count("recstopunix"))){
        uint64_t unixStreamBegin = Util::epoch() - endTime()/1000;
        if (targetParams.count("recstartunix")){
          uint64_t startUnix = atoll(targetParams["recstartunix"].c_str());
          if (startUnix < unixStreamBegin){
            WARN_MSG(
                "Recording start time is earlier than stream begin - starting earliest possible");
            targetParams["recstart"] = "-1";
          }else{
            targetParams["recstart"] = JSON::Value((startUnix - unixStreamBegin) * 1000).asString();
          }
        }
        if (targetParams.count("recstopunix")){
          uint64_t stopUnix = atoll(targetParams["recstopunix"].c_str());
          if (stopUnix < unixStreamBegin){
            onFail("Recording stop time is earlier than stream begin - aborting", true);
            return;
          }else{
            targetParams["recstop"] = JSON::Value((stopUnix - unixStreamBegin) * 1000).asString();
          }
        }
      }
      if (targetParams.count("recstop")){
        uint64_t endRec = atoll(targetParams["recstop"].c_str());
        if (endRec < startTime()){
          onFail("Entire recording range is in the past", true);
          return;
        }
        INFO_MSG("Recording will stop at %" PRIu64, endRec);
      }
      if (targetParams.count("recstart") && atoll(targetParams["recstart"].c_str()) != 0){
        size_t mainTrack = getMainSelectedTrack();
        uint64_t startRec = atoll(targetParams["recstart"].c_str());
        if (startRec > M.getLastms(mainTrack)){
          if (M.getVod()){
            onFail("Recording start past end of non-live source", true);
            return;
          }
          uint64_t streamAvail = M.getLastms(mainTrack);
          uint64_t lastUpdated = Util::getMS();
          while (Util::getMS() - lastUpdated < 5000 && startRec > streamAvail && keepGoing()){
            Util::sleep(500);
            if (M.getLastms(mainTrack) > streamAvail){
              stats();
              streamAvail = M.getLastms(mainTrack);
              lastUpdated = Util::getMS();
            }
          }
        }
        if (startRec < startTime()){
          WARN_MSG("Record begin at %" PRIu64 " ms not available, starting at %" PRIu64
                   " ms instead",
                   startRec, startTime());
          startRec = startTime();
        }
        INFO_MSG("Recording will start at %" PRIu64, startRec);
        seekPos = startRec;
      }
      // Duration to record in seconds. Overrides recstop.
      if (targetParams.count("split")){
        long long endRec = atoll(targetParams["split"].c_str()) * 1000;
        INFO_MSG("Will split recording every %lld seconds", atoll(targetParams["split"].c_str()));
        targetParams["nxt-split"] = JSON::Value((int64_t)(seekPos + endRec)).asString();
      }
      if (targetParams.count("duration")){
        long long endRec = atoll(targetParams["duration"].c_str()) * 1000;
        targetParams["recstop"] = JSON::Value((int64_t)(seekPos + endRec)).asString();
      }
      if (targetParams.count("recstop")){
        long long endRec = atoll(targetParams["recstop"].c_str());
        if (endRec < 0 || endRec < startTime()){
          onFail("Entire recording range is in the past", true);
          return;
        }
        INFO_MSG("Recording will stop at %lld", endRec);
      }
    }else{
      if (M.getLive() && targetParams.count("pushdelay")){
        INFO_MSG("Converting pushdelay syntax into corresponding startunix+realtime options");
        targetParams["startunix"] = std::string("-") + targetParams["pushdelay"];
        targetParams["realtime"] = "1";
      }
      if (M.getLive() && (targetParams.count("startunix") || targetParams.count("stopunix"))){
        uint64_t unixStreamBegin = Util::epoch() - endTime()/1000;
        if (targetParams.count("startunix")){
          int64_t startUnix = atoll(targetParams["startunix"].c_str());
          if (startUnix < 0){
            int64_t origStartUnix = startUnix;
            startUnix += Util::epoch();
            if (startUnix < unixStreamBegin){
              INFO_MSG("Waiting for stream to reach intended starting point");
              while (startUnix < Util::epoch() - (endTime() / 1000) && keepGoing()){
                Util::wait(1000);
                stats();
                startUnix = origStartUnix + Util::epoch();
              }
            }
          }
          if (startUnix < unixStreamBegin){
            WARN_MSG("Start time is earlier than stream begin - starting earliest possible");
            WARN_MSG("%" PRId64 " < %" PRId64, startUnix, unixStreamBegin);
            targetParams["start"] = "-1";
          }else{
            targetParams["start"] = JSON::Value((startUnix - unixStreamBegin) * 1000).asString();
          }
        }
        if (targetParams.count("stopunix")){
          int64_t stopUnix = atoll(targetParams["stopunix"].c_str());
          if (stopUnix < 0){stopUnix += Util::epoch();}
          if (stopUnix < unixStreamBegin){
            onFail("Stop time is earlier than stream begin - aborting", true);
            return;
          }else{
            targetParams["stop"] = JSON::Value((stopUnix - unixStreamBegin) * 1000).asString();
          }
        }
      }
      if (targetParams.count("stop")){
        int64_t endRec = atoll(targetParams["stop"].c_str());
        if (endRec < 0 || endRec < startTime()){
          onFail("Entire range is in the past", true);
          return;
        }
        INFO_MSG("Playback will stop at %" PRIu64, endRec);
      }
      if (targetParams.count("start") && atoll(targetParams["start"].c_str()) != 0){
        size_t mainTrack = getMainSelectedTrack();
        int64_t startRec = atoll(targetParams["start"].c_str());
        if (startRec > M.getLastms(mainTrack)){
          if (M.getVod()){
            onFail("Playback start past end of non-live source", true);
            return;
          }
          INFO_MSG("Waiting for stream to reach playback starting point");
          int64_t streamAvail = M.getLastms(mainTrack);
          int64_t lastUpdated = Util::getMS();
          while (Util::getMS() - lastUpdated < 5000 && startRec > streamAvail && keepGoing()){
            Util::sleep(500);
            if (M.getLastms(mainTrack) > streamAvail){
              stats();
              streamAvail = M.getLastms(mainTrack);
              lastUpdated = Util::getMS();
            }
          }
        }
        if (startRec < 0 || startRec < startTime()){
          WARN_MSG("Playback begin at %" PRId64 " ms not available, starting at %" PRIu64
                   " ms instead",
                   startRec, startTime());
          startRec = startTime();
        }
        INFO_MSG("Playback will start at %" PRIu64, startRec);
        seekPos = startRec;
      }
    }
    /*LTS-END*/
    MEDIUM_MSG("Initial seek to %" PRIu64 "ms", seekPos);
    seek(seekPos);
  }

  /// Returns the highest getMinKeepAway of all selected tracks
  uint64_t Output::getMinKeepAway(){
    uint64_t r = 0;
    for (std::map<size_t, Comms::Users>::iterator ti = userSelect.begin(); ti != userSelect.end(); ++ti){
      if (ti->first == INVALID_TRACK_ID){continue;}
      if (M.getMinKeepAway(ti->first) > r){r = M.getMinKeepAway(ti->first);}
    }
    //Limit the value to the maxKeepAway setting
    //Also lowers extraKeepAway if needed
    uint64_t maxKeepAway = M.getMaxKeepAway();
    if (maxKeepAway){
      if (r > maxKeepAway){r = maxKeepAway;}
      if (r+extraKeepAway > maxKeepAway){extraKeepAway = maxKeepAway - r;}
    }
    return r;
  }

  /// This function attempts to forward playback in live streams to a more live point.
  /// It seeks to the last sync'ed keyframe of the main track, no closer than needsLookAhead+minKeepAway ms from the end.
  /// Aborts if not live, there is no main track or it has no keyframes.
  bool Output::liveSeek(){
    if (!realTime){return false;}//Makes no sense when playing in turbo mode
    uint64_t seekPos = 0;
    if (!meta.getLive()){return false;}
    size_t mainTrack = getMainSelectedTrack();
    if (mainTrack == INVALID_TRACK_ID){return false;}
    uint64_t lMs = meta.getLastms(mainTrack);
    uint64_t cTime = thisPacket.getTime();
    uint64_t mKa = getMinKeepAway();
    if (!maxSkipAhead){
      bool noReturn = false;
      uint64_t newSpeed = 1000;
      if (extraKeepAway > 0){extraKeepAway--;}//Reduce extra latency if possible
      if (lMs - mKa - needsLookAhead - extraKeepAway > cTime + 50){
        // We need to speed up!
        uint64_t diff = (lMs - mKa - needsLookAhead - extraKeepAway) - cTime;
        if (diff > 3000){
          noReturn = true;
          newSpeed = 1000;
        }else if (diff > 1000){
          newSpeed = 750;
        }else if (diff > 500){
          newSpeed = 900;
        }else{
          newSpeed = 950;
        }
      }
      if (realTime != newSpeed){
        HIGH_MSG("Changing playback speed from %" PRIu64 " to %" PRIu64 "(%" PRIu64 " ms LA, %" PRIu64 " ms mKA, %lu eKA)", realTime, newSpeed, needsLookAhead, mKa, extraKeepAway);
        firstTime = Util::bootMS() - (cTime * newSpeed / 1000);
        realTime = newSpeed;
      }
      if (!noReturn){return false;}
    }
    // cancel if there are no keys in the main track
    if (mainTrack == INVALID_TRACK_ID){return false;}
    DTSC::Keys mainKeys(meta.keys(mainTrack));
    if (!mainKeys.getValidCount()){return false;}

    for (uint32_t keyNum = mainKeys.getEndValid() - 1; keyNum >= mainKeys.getFirstValid(); keyNum--){
      seekPos = mainKeys.getTime(keyNum);
      // Only skip forward if we can win a decent amount (100ms)
      if (seekPos <= cTime + 100 * seekCount){break;}
      bool good = true;
      // check if all tracks have data for this point in time
      for (std::map<size_t, Comms::Users>::iterator ti = userSelect.begin(); ti != userSelect.end(); ++ti){
        if (ti->first == INVALID_TRACK_ID){
          HIGH_MSG("Skipping track %zu, not in tracks", ti->first);
          continue;
        }// ignore missing tracks
        if (meta.getLastms(ti->first) < seekPos + needsLookAhead + extraKeepAway + mKa){
          good = false;
          break;
        }
        if (mainTrack == ti->first){continue;}// skip self
        if (meta.getLastms(ti->first) == meta.getFirstms(ti->first)){
          HIGH_MSG("Skipping track %lu, last equals first", ti->first);
          continue;
        }// ignore point-tracks
        HIGH_MSG("Track %lu is good", ti->first);
      }
      // if yes, seek here
      if (good){
        HIGH_MSG("Skipping forward %" PRIu64 "ms (%" PRIu64 " ms LA, %" PRIu64
                 " ms mKA, %lu eKA, > %" PRIu32 "ms, mSa %" PRIu64 " ms)",
                 seekPos - cTime, needsLookAhead, mKa, extraKeepAway, seekCount * 100, maxSkipAhead);
        if (seekCount < 20){++seekCount;}
        seek(seekPos);
        return true;
      }
    }
    return false;
  }

  void Output::requestHandler(){
    if ((firstData && myConn.Received().size()) || myConn.spool()){
      firstData = false;
      DONTEVEN_MSG("onRequest");
      onRequest();
      lastRecv = Util::bootSecs();
    }else{
      if (!isBlocking && !parseData){
        if (Util::bootSecs() - lastRecv > 300){
          WARN_MSG("Disconnecting 5 minute idle connection");
          onFail("Connection idle for 5 minutes");
        }else{
          Util::sleep(20);
        }
      }
    }
  }

  /// Waits for the given amount of millis, increasing the realtime playback
  /// related times as needed to keep smooth playback intact.
  void Output::playbackSleep(uint64_t millis){
    if (realTime && M.getLive()){
      firstTime += millis;
      extraKeepAway += millis;
    }
    Util::wait(millis);
  }

  /// Called right before sendNext(). Should return true if this is a stopping point.
  bool Output::reachedPlannedStop(){
    // If we're recording to file and reached the target position, stop
    if (isRecordingToFile && targetParams.count("recstop") &&
        atoll(targetParams["recstop"].c_str()) <= lastPacketTime){
      INFO_MSG("End of planned recording reached");
      return true;
    }
    // Regardless of playback method, if we've reached the wanted stop point, stop
    if (targetParams.count("stop") && atoll(targetParams["stop"].c_str()) <= lastPacketTime){
      INFO_MSG("End of planned playback reached");
      return true;
    }
    // check if we need to split here
    if (inlineRestartCapable() && targetParams.count("split")){
      // Make sure that inlineRestartCapable outputs with splitting enabled only stop right before
      // keyframes This works because this function is executed right BEFORE sendNext(), causing
      // thisPacket to be the next packet in the newly splitted file.
      if (!thisPacket.getFlag("keyframe")){return false;}
      // is this a split point?
      if (targetParams.count("nxt-split") && atoll(targetParams["nxt-split"].c_str()) <= lastPacketTime){
        INFO_MSG("Split point reached");
        return true;
      }
    }
    // Otherwise, we're not stopping
    return false;
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
    // Connect to file target, if needed
    if (isFileTarget()){
      if (!streamName.size()){
        WARN_MSG("Recording unconnected %s output to file! Cancelled.", capa["name"].asString().c_str());
        onFail("Unconnected recording output", true);
        return 2;
      }
      initialize();
      if (!M.getValidTracks().size() || !userSelect.size() || !keepGoing()){
        INFO_MSG("Stream not available - aborting");
        onFail("Stream not available for recording", true);
        return 3;
      }
      if (config->getString("target") == "-"){
        parseData = true;
        wantRequest = false;
        if (!targetParams.count("realtime")){realTime = 0;}
        INFO_MSG("Outputting %s to stdout with %s format", streamName.c_str(),
                 capa["name"].asString().c_str());
      }else{
        if (!connectToFile(config->getString("target"), targetParams.count("append"))){
          onFail("Could not connect to the target for recording", true);
          return 3;
        }
        parseData = true;
        wantRequest = false;
        if (!targetParams.count("realtime")){realTime = 0;}
        INFO_MSG("Recording %s to %s with %s format", streamName.c_str(),
                 config->getString("target").c_str(), capa["name"].asString().c_str());
      }
    }
    // Handle CONN_OPEN trigger, if needed
    if (Triggers::shouldTrigger("CONN_OPEN", streamName)){
      std::string payload =
          streamName + "\n" + getConnectedHost() + "\n" + capa["name"].asStringRef() + "\n" + reqUrl;
      if (!Triggers::doTrigger("CONN_OPEN", payload, streamName)){return 1;}
    }
    /*LTS-END*/
    DONTEVEN_MSG("MistOut client handler started");
    while (keepGoing() && (wantRequest || parseData)){
      if (wantRequest){requestHandler();}
      if (parseData){
        if (!isInitialized){
          initialize();
          if (!isInitialized){
            onFail("Stream initialization failed");
            break;
          }
        }
        if (!sentHeader && keepGoing()){
          DONTEVEN_MSG("sendHeader");
          sendHeader();
        }
        if (!sought){initialSeek();}
        if (prepareNext()){
          if (thisPacket){
            lastPacketTime = thisTime;
            if (firstPacketTime == 0xFFFFFFFFFFFFFFFFull){firstPacketTime = lastPacketTime;}

            // slow down processing, if real time speed is wanted
            if (realTime){
              uint8_t i = 6;
              while (--i && thisPacket.getTime() > (((Util::bootMS() - firstTime) * 1000) / realTime + maxSkipAhead) &&
                     keepGoing()){
                uint64_t amount = thisPacket.getTime() - (((Util::bootMS() - firstTime) * 1000) / realTime + maxSkipAhead);
                if (amount > 1000){amount = 1000;}
                Util::sleep(amount);
                //Make sure we stay responsive to requests and stats while waiting
                if (wantRequest){
                  requestHandler();
                  if (!realTime){break;}
                }
                stats();
              }
              if (!thisPacket){continue;}
            }

            // delay the stream until metadata has caught up, if needed
            if (needsLookAhead && M.getLive()){
              // we sleep in 20ms increments, or less if the lookahead time itself is less
              uint32_t sleepTime = std::min(20ul, needsLookAhead);
              // wait at most double the look ahead time, plus ten seconds
              uint64_t timeoutTries = (needsLookAhead / sleepTime) * 2 + (10000 / sleepTime);
              uint64_t needsTime = thisTime + needsLookAhead;
              bool firstTime = true;
              while (--timeoutTries && keepGoing()){
                bool lookReady = true;
                for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin();
                     it != userSelect.end(); it++){
                  if (meta.getLastms(it->first) <= needsTime){
                    if (timeoutTries == 1){
                      WARN_MSG("Track %zu: %" PRIu64 " <= %" PRIu64, it->first,
                               meta.getLastms(it->first), needsTime);
                    }
                    lookReady = false;
                    break;
                  }
                }
                if (lookReady){break;}
                if (firstTime){
                  firstTime = false;
                }else{
                  playbackSleep(sleepTime);
                }
                //Make sure we stay responsive to requests and stats while waiting
                if (wantRequest){requestHandler();}
                stats();
                meta.reloadReplacedPagesIfNeeded();
              }
              if (!timeoutTries){
                WARN_MSG("Waiting for lookahead (%zums in %zu tracks) timed out - resetting lookahead!", needsLookAhead, userSelect.size());
                needsLookAhead = 0;
              }
            }

            if (reachedPlannedStop()){
              const char *origTarget = getenv("MST_ORIG_TARGET");
              targetParams.erase("nxt-split");
              if (inlineRestartCapable() && origTarget && !reachedPlannedStop()){
                std::string newTarget = origTarget;
                Util::streamVariables(newTarget, streamName);
                if (newTarget.rfind('?') != std::string::npos){
                  newTarget.erase(newTarget.rfind('?'));
                }
                INFO_MSG("Switching to next push target filename: %s", newTarget.c_str());
                if (!connectToFile(newTarget)){
                  FAIL_MSG("Failed to open file, aborting: %s", newTarget.c_str());
                  Util::logExitReason("failed to open file, aborting: %s", newTarget.c_str());
                  onFinish();
                  break;
                }
                uint64_t endRec = lastPacketTime + atoll(targetParams["split"].c_str()) * 1000;
                targetParams["nxt-split"] = JSON::Value(endRec).asString();
                sentHeader = false;
                sendHeader();
              }else{
                if (!onFinish()){
                  INFO_MSG("Shutting down because planned stopping point reached");
                  Util::logExitReason("planned stopping point reached");
                  break;
                }
              }
            }
            sendNext();
          }else{
            parseData = false;
            /*LTS-START*/
            if (Triggers::shouldTrigger("CONN_STOP", streamName)){
              std::string payload =
                  streamName + "\n" + getConnectedHost() + "\n" + capa["name"].asStringRef() + "\n";
              Triggers::doTrigger("CONN_STOP", payload, streamName);
            }
            /*LTS-END*/
            if (!onFinish()){
              Util::logExitReason("end of stream");
              break;
            }
          }
        }
        if (!meta){
          Util::logExitReason("lost internal connection to stream data");
          break;
        }
      }
      stats();
    }
    if (!config->is_active){Util::logExitReason("set inactive");}
    if (!myConn){Util::logExitReason("connection closed");}
    if (strncmp(Util::exitReason, "connection closed", 17) == 0){
      MEDIUM_MSG("Client handler shutting down, exit reason: %s", Util::exitReason);
    }else{
      INFO_MSG("Client handler shutting down, exit reason: %s", Util::exitReason);
    }
    onFinish();

    /*LTS-START*/
    if (Triggers::shouldTrigger("CONN_CLOSE", streamName)){
      std::string payload =
          streamName + "\n" + getConnectedHost() + "\n" + capa["name"].asStringRef() + "\n" + reqUrl;
      Triggers::doTrigger("CONN_CLOSE", payload, streamName);
    }
    if (isRecordingToFile && config->hasOption("target") && Triggers::shouldTrigger("RECORDING_END", streamName)){
      uint64_t rightNow = Util::epoch();
      std::stringstream payl;
      payl << streamName << '\n';
      payl << config->getString("target") << '\n';
      payl << capa["name"].asStringRef() << '\n';
      payl << myConn.dataUp() << '\n';
      payl << (Util::bootSecs() - myConn.connTime()) << '\n';
      payl << (rightNow - (Util::bootSecs() - myConn.connTime())) << '\n';
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
    if (statComm){statComm.setStatus(COMM_STATUS_DISCONNECT | statComm.getStatus());}

    userSelect.clear();

    myConn.close();
    return 0;
  }

  void Output::dropTrack(size_t trackId, const std::string &reason, bool probablyBad){
    if (!userSelect.count(trackId)){return;}
    // depending on whether this is probably bad and the current debug level, print a message
    size_t printLevel = (probablyBad ? DLVL_WARN : DLVL_INFO);
    const Comms::Users &usr = userSelect.at(trackId);
    DEBUG_MSG(printLevel, "Dropping %s (%s) track %zu@k%zu (nextP=%zu, lastP=%zu): %s",
              streamName.c_str(), meta.getCodec(trackId).c_str(), trackId, usr.getKeyNum() + 1,
              pageNumForKey(trackId, usr.getKeyNum() + 1), pageNumMax(trackId), reason.c_str());
    // now actually drop the track from the buffer
    for (std::set<sortedPageInfo>::iterator it = buffer.begin(); it != buffer.end(); ++it){
      if (it->tid == trackId){
        buffer.erase(it);
        break;
      }
    }
    userSelect.erase(trackId);
  }

  /// Assumes at least one video track is selected.
  /// Seeks back in the buffer to the newest keyframe with a timestamp less than the current
  /// timestamp. Sets thisPacket to that frame, and then undoes the seek. The next call to
  /// prepareNext continues as if this function was never called.
  bool Output::getKeyFrame(){
    // store copy of current state
    std::set<sortedPageInfo> tmp_buffer = buffer;
    std::map<size_t, Comms::Users> tmp_userSelect = userSelect;
    std::map<size_t, uint32_t> tmp_currentPage = currentPage;

    // reset the current packet to null, assuming failure
    thisPacket.null();

    // find the main track, check if it is video. Abort if not.
    size_t mainTrack = getMainSelectedTrack();
    if (M.getType(mainTrack) != "video"){return false;}

    // we now know that mainTrack is a video track - let's do some work!
    // first, we remove all selected tracks and the buffer. Then we select only the main track.
    uint64_t currTime = currentTime();
    buffer.clear();
    userSelect.clear();
    userSelect[mainTrack].reload(streamName, mainTrack);
    // now, seek to the exact timestamp of the keyframe
    DTSC::Keys keys(M.keys(mainTrack));
    uint32_t targetKey = M.getKeyNumForTime(mainTrack, currTime);
    seek(keys.getTime(targetKey));
    // attempt to load the key into thisPacket
    bool ret = prepareNext();
    if (!ret){
      WARN_MSG("Failed to load keyframe for %" PRIu64 "ms - continuing without it", currTime);
    }

    // restore state to before the seek/load
    // most of these can simply be copied back...
    buffer = tmp_buffer;
    userSelect = tmp_userSelect;
    // but the currentPage map must also load keys as needed
    for (std::map<size_t, uint32_t>::iterator it = tmp_currentPage.begin(); it != tmp_currentPage.end(); ++it){
      loadPageForKey(it->first, it->second);
    }
    // now we are back to normal and can return safely
    return ret;
  }

  /// Attempts to prepare a new packet for output.
  /// If it returns true and thisPacket evaluates to false, playback has completed.
  /// Could be called repeatedly in a loop if you really really want a new packet.
  /// \returns true if thisPacket was filled with the next packet.
  /// \returns false if we could not reliably determine the next packet yet.
  bool Output::prepareNext(){
    if (!buffer.size()){
      thisPacket.null();
      INFO_MSG("Buffer completely played out");
      return true;
    }
    // check if we have a next seek point for every track that is selected
    if (buffer.size() != userSelect.size()){
      INFO_MSG("Buffer/select mismatch: %zu/%zu - correcting", buffer.size(), userSelect.size());
      std::set<size_t> dropTracks;
      if (buffer.size() < userSelect.size()){
        // prepare to drop any selectedTrack without buffer entry
        for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); ++it){
          bool found = false;
          for (std::set<sortedPageInfo>::iterator bi = buffer.begin(); bi != buffer.end(); ++bi){
            if (bi->tid == it->first){
              found = true;
              break;
            }
          }
          if (!found){dropTracks.insert(it->first);}
        }
      }else{
        std::set<size_t> seen;
        // prepare to drop any buffer entry without selectedTrack
        for (std::set<sortedPageInfo>::iterator bi = buffer.begin(); bi != buffer.end(); ++bi){
          if (!userSelect.count(bi->tid)){dropTracks.insert(bi->tid);}
          if (seen.count(bi->tid)){
            INFO_MSG("Dropping duplicate buffer entry for track %zu", bi->tid);
            buffer.erase(bi);
            return false;
          }
          seen.insert(bi->tid);
        }
      }
      if (!dropTracks.size()){
        FAIL_MSG("Could not equalize tracks! This is very very very bad and I am now going to shut down to prevent worse.");
        Util::logExitReason("Could not equalize tracks");
        parseData = false;
        config->is_active = false;
        return false;
      }
      // actually drop what we found.
      // if both of the above cases occur, the next prepareNext iteration will take care of that
      for (std::set<size_t>::iterator it = dropTracks.begin(); it != dropTracks.end(); ++it){
        dropTrack(*it, "seek/select mismatch");
      }
      return false;
    }

    sortedPageInfo nxt = *(buffer.begin());

    if (meta.reloadReplacedPagesIfNeeded()){return false;}
    if (!M.getValidTracks().count(nxt.tid)){
      dropTrack(nxt.tid, "disappeared from metadata");
      return false;
    }

    // if we're going to read past the end of the data page, load the next page
    // this only happens for VoD
    if (nxt.offset >= curPage[nxt.tid].len ||
        (!memcmp(curPage[nxt.tid].mapped + nxt.offset, "\000\000\000\000", 4))){
      if (M.getVod() && nxt.time >= M.getLastms(nxt.tid)){
        dropTrack(nxt.tid, "end of VoD track reached", false);
        return false;
      }
      if (M.getPageNumberForTime(nxt.tid, nxt.time) != currentPage[nxt.tid]){
        loadPageForKey(nxt.tid, M.getPageNumberForTime(nxt.tid, nxt.time));
        nxt.offset = 0;
        //Only read the next time if the page load succeeded and there is a packet to read from
        if (curPage[nxt.tid].mapped && curPage[nxt.tid].mapped[0] == 'D'){
          nxt.time = getDTSCTime(curPage[nxt.tid].mapped, 0);
        }
        buffer.erase(buffer.begin());
        buffer.insert(nxt);
        return false;
      }
      dropTrack(nxt.tid, "VoD page load failure");
      return false;
    }

    // We know this packet will be valid, pre-load it so we know its length
    DTSC::Packet preLoad(curPage[nxt.tid].mapped + nxt.offset, 0, true);

    uint64_t nextTime = 0;

    // Check if we have a next valid packet
    if (curPage[nxt.tid].len > nxt.offset+preLoad.getDataLen()+20 && memcmp(curPage[nxt.tid].mapped + nxt.offset + preLoad.getDataLen(), "\000\000\000\000", 4)){
      nextTime = getDTSCTime(curPage[nxt.tid].mapped, nxt.offset + preLoad.getDataLen());
      if (!nextTime){
        WARN_MSG("Next packet is available (offset %" PRIu64 " / %" PRIu64 " on %s), but has no time. Please warn the developers if you see this message!", nxt.offset, curPage[nxt.tid].len, curPage[nxt.tid].name.c_str());
        dropTrack(nxt.tid, "EOP: invalid next packet");
        return false;
      }
      if (nextTime < nxt.time){
        std::stringstream errMsg;
        errMsg << "next packet has timestamp " << nextTime << " but current timestamp is " << nxt.time;
        dropTrack(nxt.tid, errMsg.str().c_str());
        return false;
      }
    }else{
      //no next packet yet!
      //Check if this is the last packet of a VoD stream. Return success and drop the track.
      if (M.getVod() && nxt.time >= M.getLastms(nxt.tid)){
        thisPacket.reInit(curPage[nxt.tid].mapped + nxt.offset, 0, true);
        thisIdx = nxt.tid;
        dropTrack(nxt.tid, "end of VoD track reached", false);
        return true;
      }
      uint32_t thisKey = M.getKeyNumForTime(nxt.tid, nxt.time);
      //Check if there exists a different page for the next key
      uint32_t nextKeyPage = INVALID_KEY_NUM;
      //Make sure we only try to read the page for the next key if it actually should be available
      DTSC::Keys keys(M.keys(nxt.tid));
      if (keys.getEndValid() >= thisKey+1){nextKeyPage = M.getPageNumberForKey(nxt.tid, thisKey + 1);}
      if (nextKeyPage != INVALID_KEY_NUM && nextKeyPage != currentPage[nxt.tid]){
        // If so, the next key is our next packet
        nextTime = keys.getTime(thisKey + 1);

        //If the next packet should've been before the current packet, something is wrong. Abort, abort!
        if (nextTime < nxt.time){
          std::stringstream errMsg;
          errMsg << "next key (" << (thisKey+1) << ") time " << nextTime << " but current time " << nxt.time;
          errMsg << "; currPage=" << currentPage[nxt.tid] << ", nxtPage=" << nextKeyPage;
          errMsg << ", firstKey=" << keys.getFirstValid() << ", endKey=" << keys.getEndValid();
          dropTrack(nxt.tid, errMsg.str().c_str());
          return false;
        }
      }else{
        //Okay, there's no next page yet, and no next packet on this page either.
        //That means we're waiting for data to show up, somewhere.
        // after ~25 seconds, give up and drop the track.
        if (++emptyCount >= 2500){
          dropTrack(nxt.tid, "EOP: data wait timeout");
          return false;
        }
        //every ~1 second, check if the stream is not offline
        if (emptyCount % 100 == 0 && M.getLive() && Util::getStreamStatus(streamName) == STRMSTAT_OFF){
          Util::logExitReason("Stream source shut down");
          thisPacket.null();
          return true;
        }
        //every ~16 seconds, reconnect to metadata
        if (emptyCount % 1600 == 0){
          INFO_MSG("Reconnecting to input; track %" PRIu64 " key %" PRIu32 " is on page %" PRIu32 " and we're currently serving %" PRIu32 " from %" PRIu32, nxt.tid, thisKey+1, nextKeyPage, thisKey, currentPage[nxt.tid]);
          reconnect();
          if (!meta){
            onFail("Could not connect to stream data", true);
            thisPacket.null();
            return true;
          }
          // if we don't have a connection to the metadata here, this means the stream has gone offline in the meanwhile.
          if (!meta){
            Util::logExitReason("Attempted reconnect to source failed");
            thisPacket.null();
            return true;
          }
          return false;//no sleep after reconnect
        }
        //Fine! We didn't want a packet, anyway. Let's try again later.
        playbackSleep(10);
        return false;
      }
    }

    // we've handled all special cases - at this point the packet should exist
    // let's load it
    thisPacket.reInit(curPage[nxt.tid].mapped + nxt.offset, 0, true);
    thisIdx = nxt.tid;
    thisTime = thisPacket.getTime();
    // if it failed, drop the track and continue
    if (!thisPacket){
      dropTrack(nxt.tid, "packet load failure");
      return false;
    }
    emptyCount = 0; // valid packet - reset empty counter

    if (!userSelect[nxt.tid]){
      INFO_MSG("Track %zu is not alive!", nxt.tid);
      return false;
    }

    //Update keynum only when the second flips over in the timestamp
    //We do this because DTSC::Keys is pretty CPU-heavy
    if (nxt.time / 1000 < nextTime/1000){
      uint32_t thisKey = M.getKeyNumForTime(nxt.tid, nxt.time);
      userSelect[nxt.tid].setKeyNum(thisKey);
    }

    // we assume the next packet is the next on this same page
    nxt.offset += thisPacket.getDataLen();
    nxt.time = nextTime;
    ++nxt.partIndex;

    // exchange the current packet in the buffer for the next one
    buffer.erase(buffer.begin());
    buffer.insert(nxt);

    return true;
  }

  /// Returns the name as it should be used in statistics.
  /// Outputs used as an input should return INPUT, outputs used for automation should return OUTPUT, others should return their proper name.
  /// The default implementation is usually good enough for all the non-INPUT types.
  std::string Output::getStatsName(){
    if (isPushing()){return "INPUT:" + capa["name"].asStringRef();}
    if (config->hasOption("target") && config->getString("target").size()){
      return "OUTPUT:" + capa["name"].asStringRef();
    }
    return capa["name"].asStringRef();
  }

  void Output::stats(bool force){
    // cancel stats update if not initialized
    if (!isInitialized){return;}
    // also cancel if it has been less than a second since the last update
    // unless force is set to true
    uint64_t now = Util::bootSecs();
    if (now == lastStats && !force){return;}

    if (isRecording()){
      if(lastPushUpdate == 0){
        lastPushUpdate = now;
      }

      if (lastPushUpdate + 5 <= now){
        JSON::Value pStat;
        pStat["push_status_update"]["id"] = getpid();
        JSON::Value & pData = pStat["push_status_update"]["status"];
        pData["mediatime"] = currentTime();
        for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
          pData["tracks"].append(it->first);
        }
        pData["bytes"] = statComm.getUp();
        uint64_t pktCntNow = statComm.getPacketCount();
        if (pktCntNow){
          uint64_t pktLosNow = statComm.getPacketLostCount();
          static uint64_t prevPktCount = pktCntNow;
          static uint64_t prevLosCount = pktLosNow;
          uint64_t pktCntDiff = pktCntNow-prevPktCount;
          uint64_t pktLosDiff = pktLosNow-prevLosCount;
          if (pktCntDiff){
            pData["pkt_loss_perc"] = (pktLosDiff*100) / pktCntDiff;
          }
          pData["pkt_loss_count"] = pktLosNow;
          pData["pkt_retrans_count"] = statComm.getPacketRetransmitCount();
          prevPktCount = pktCntNow;
          prevLosCount = pktLosNow;
        }
        pData["active_seconds"] = statComm.getTime();
        Socket::UDPConnection uSock;
        uSock.SetDestination(UDP_API_HOST, UDP_API_PORT);
        uSock.SendNow(pStat.toString());
        lastPushUpdate = now;
      }
    }

    if (!statComm){statComm.reload();}
    if (!statComm){return;}

    lastStats = now;

    VERYHIGH_MSG("Writing stats: %s, %s, %u, %lu, %lu", getConnectedHost().c_str(), streamName.c_str(),
             crc & 0xFFFFFFFFu, myConn.dataUp(), myConn.dataDown());
    /*LTS-START*/
    if (statComm.getStatus() & COMM_STATUS_REQDISCONNECT){
      onFail("Shutting down on controller request");
      return;
    }
    /*LTS-END*/
    statComm.setNow(now);
    statComm.setHost(getConnectedBinHost());
    statComm.setCRC(crc);
    statComm.setStream(streamName);
    statComm.setConnector(getStatsName());
    connStats(now, statComm);
    statComm.setLastSecond(thisPacket ? thisPacket.getTime() : 0);
    statComm.setPid(getpid());

    /*LTS-START*/
    // Tag the session with the user agent
    if (newUA && ((now - myConn.connTime()) >= uaDelay || !myConn) && UA.size()){
      std::string APIcall =
          "{\"tag_sessid\":{\"" + statComm.getSessId() + "\":" + JSON::string_escape("UA:" + UA) + "}}";
      Socket::UDPConnection uSock;
      uSock.SetDestination(UDP_API_HOST, UDP_API_PORT);
      uSock.SendNow(APIcall);
      newUA = false;
    }
    /*LTS-END*/

    doSync();

    if (isPushing()){
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        if (it->second.getStatus() & COMM_STATUS_REQDISCONNECT){
          if (dropPushTrack(it->second.getTrack(), "disconnect request from buffer")){break;}
        }
        if (!it->second){
          if (dropPushTrack(it->second.getTrack(), "track mapping no longer valid")){break;}
        }
        //if (Util::bootSecs() - M.getLastUpdated(it->first) > 5){
        //  if (dropPushTrack(it->second.getTrack(), "track updates being ignored by buffer")){break;}
        //}
      }
    }
  }

  void Output::connStats(uint64_t now, Comms::Statistics &statComm){
    statComm.setUp(myConn.dataUp());
    statComm.setDown(myConn.dataDown());
    statComm.setTime(now - myConn.connTime());
  }

  bool Output::dropPushTrack(uint32_t trackId, const std::string & dropReason){
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      if (it->second.getTrack() == trackId){
        WARN_MSG("Dropping input track %" PRIu32 ": %s", trackId, dropReason.c_str());
        userSelect.erase(it);
        return true;
        break;
      }
    }
    return false;
  }

  void Output::onRequest(){
    // simply clear the buffer, we don't support any kind of input by default
    myConn.Received().clear();
    wantRequest = false;
  }

  void Output::sendHeader(){
    // just set the sentHeader bool to true, by default
    sentHeader = true;
  }

  bool Output::connectToFile(std::string file, bool append){
    int flags = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    int mode = O_RDWR | O_CREAT | (append ? O_APPEND : O_TRUNC);
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
    return true;
  }

  /// Checks if the set streamName allows pushes from this connector/IP/password combination.
  /// Runs all appropriate triggers and checks.
  /// Returns true if the push should continue, false otherwise.
  bool Output::allowPush(const std::string &passwd){
    pushing = true;
    std::string strmSource;

    // Initialize the stream source if needed, connect to it
    waitForStreamPushReady();
    // pull the source setting from metadata
    if (meta){strmSource = meta.getSource();}

    if (!strmSource.size()){
      FAIL_MSG("Push rejected - stream %s not configured or unavailable", streamName.c_str());
      pushing = false;
      return false;
    }
    if (strmSource.substr(0, 7) != "push://"){
      FAIL_MSG("Push rejected - stream %s not a push-able stream. (%s != push://*)",
               streamName.c_str(), strmSource.c_str());
      pushing = false;
      return false;
    }

    std::string source = strmSource.substr(7);
    std::string IP = source.substr(0, source.find('@'));

    /*LTS-START*/
    std::string password;
    if (source.find('@') != std::string::npos){
      password = source.substr(source.find('@') + 1);
      if (password != ""){
        if (password == passwd){
          INFO_MSG("Password accepted - ignoring IP settings.");
          IP = "";
        }else{
          INFO_MSG("Password rejected - checking IP.");
          if (IP == ""){IP = "deny-all.invalid";}
        }
      }
    }

    if (Triggers::shouldTrigger("STREAM_PUSH", streamName)){
      std::string payload = streamName + "\n" + getConnectedHost() + "\n" + capa["name"].asStringRef() + "\n" + reqUrl;
      if (!Triggers::doTrigger("STREAM_PUSH", payload, streamName)){
        WARN_MSG("Push from %s rejected by STREAM_PUSH trigger", getConnectedHost().c_str());
        pushing = false;
        return false;
      }
    }
    /*LTS-END*/

    if (IP != ""){
      if (!myConn.isAddress(IP)){
        WARN_MSG("Push from %s rejected; not whitelisted", getConnectedHost().c_str());
        pushing = false;
        return false;
      }
    }
    initialize();
    return true;
  }

  /// Attempts to wait for a stream to finish shutting down if it is, then restarts and reconnects.
  void Output::waitForStreamPushReady(){
    uint8_t streamStatus = Util::getStreamStatus(streamName);
    MEDIUM_MSG("Current status for %s buffer is %u", streamName.c_str(), streamStatus);
    if (streamStatus == STRMSTAT_READY){
      reconnect();
      std::set<size_t> vTracks = M.getValidTracks(true);
      INFO_MSG("Stream already active (%zu valid tracks) - check if it's not shutting down...", vTracks.size());
      uint64_t oneTime = 0;
      uint64_t twoTime = 0;
      for (std::set<size_t>::iterator it = vTracks.begin(); it != vTracks.end(); ++it){
        if (M.getLastms(*it) > oneTime){oneTime = M.getLastms(*it);}
      }
      Util::wait(2000);
      for (std::set<size_t>::iterator it = vTracks.begin(); it != vTracks.end(); ++it){
        if (M.getLastms(*it) > twoTime){twoTime = M.getLastms(*it);}
      }
      if (twoTime <= oneTime+500){
        disconnect();
        INFO_MSG("Waiting for stream reset before attempting push input accept (%" PRIu64 " <= %" PRIu64 "+500)", twoTime, oneTime);
        while (streamStatus != STRMSTAT_OFF && keepGoing()){
          userSelect.clear();
          Util::wait(1000);
          streamStatus = Util::getStreamStatus(streamName);
        }
        reconnect();
      }
    }
    while (((streamStatus != STRMSTAT_WAIT && streamStatus != STRMSTAT_READY) || !meta) && keepGoing()){
      INFO_MSG("Waiting for %s buffer to be ready... (%u)", streamName.c_str(), streamStatus);
      disconnect();
      userSelect.clear();
      Util::wait(1000);
      streamStatus = Util::getStreamStatus(streamName);
      if (streamStatus == STRMSTAT_OFF || streamStatus == STRMSTAT_WAIT || streamStatus == STRMSTAT_READY){
        INFO_MSG("Reconnecting to %s buffer... (%u)", streamName.c_str(), streamStatus);
        Util::wait(500);
        reconnect();
        streamStatus = Util::getStreamStatus(streamName);
      }
    }
    if (streamStatus == STRMSTAT_READY || streamStatus == STRMSTAT_WAIT){reconnect();}
    if (!meta){
      onFail("Could not connect to stream data", true);
    }
  }

  void Output::selectAllTracks(){
    std::set<size_t> tracks = getSupportedTracks();
    for (std::set<size_t>::iterator it = tracks.begin(); it != tracks.end(); it++){
      userSelect[*it].reload(streamName, *it);
    }
  }
}// namespace Mist
