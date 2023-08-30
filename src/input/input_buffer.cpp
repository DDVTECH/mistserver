#include <fcntl.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mist/bitfields.h>
#include <mist/defines.h>
#include <mist/langcodes.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <string>

#include "input_buffer.h"

#ifndef TIMEOUTMULTIPLIER
#define TIMEOUTMULTIPLIER 2
#endif

/*LTS-START*/
// We consider a stream playable when this many fragments are available.
#define FRAG_BOOT 3
/*LTS-END*/

namespace Mist{
  inputBuffer::inputBuffer(Util::Config *cfg) : Input(cfg){
    firstProcTime = 0;
    lastProcTime = 0;
    allProcsRunning = false;

    capa["optional"].removeMember("realtime");

    lastReTime = 0; /*LTS*/
    finalMillis = 0;
    liveMeta = 0;
    capa["name"] = "Buffer";
    JSON::Value option;
    option["arg"] = "integer";
    option["long"] = "buffer";
    option["short"] = "b";
    option["help"] = "DVR buffer time in ms";
    option["value"].append(50000);
    config->addOption("bufferTime", option);
    capa["optional"]["DVR"]["name"] = "Buffer time (ms)";
    capa["optional"]["DVR"]["help"] =
        "The target available buffer time for this live stream, in milliseconds. This is the time "
        "available to seek around in, and will automatically be extended to fit whole keyframes as "
        "well as the minimum duration needed for stable playback.";
    capa["optional"]["DVR"]["option"] = "--buffer";
    capa["optional"]["DVR"]["type"] = "uint";
    capa["optional"]["DVR"]["default"] = 50000;
    /*LTS-start*/
    option.null();
    option["arg"] = "integer";
    option["long"] = "cut";
    option["short"] = "c";
    option["help"] = "Any timestamps before this will be cut from the live buffer";
    option["value"].append(0);
    config->addOption("cut", option);
    capa["optional"]["cut"]["name"] = "Cut time (ms)";
    capa["optional"]["cut"]["help"] =
        "Any timestamps before this will be cut from the live buffer.";
    capa["optional"]["cut"]["option"] = "--cut";
    capa["optional"]["cut"]["type"] = "uint";
    capa["optional"]["cut"]["default"] = 0;
    option.null();

    option["arg"] = "integer";
    option["long"] = "resume";
    option["short"] = "R";
    option["help"] = "Enable resuming support (1) or disable resuming support (0, default)";
    option["value"].append(0);
    config->addOption("resume", option);
    capa["optional"]["resume"]["name"] = "Resume support";
    capa["optional"]["resume"]["help"] =
        "If enabled, the buffer will linger after source disconnect to allow resuming the stream "
        "later. If disabled, the buffer will instantly close on source disconnect.";
    capa["optional"]["resume"]["option"] = "--resume";
    capa["optional"]["resume"]["type"] = "select";
    capa["optional"]["resume"]["select"][0u][0u] = "0";
    capa["optional"]["resume"]["select"][0u][1u] = "Disabled";
    capa["optional"]["resume"]["select"][1u][0u] = "1";
    capa["optional"]["resume"]["select"][1u][1u] = "Enabled";
    capa["optional"]["resume"]["default"] = 0;
    option.null();

    option["arg"] = "integer";
    option["long"] = "maxkeepaway";
    option["short"] = "M";
    option["help"] = "Maximum distance in milliseconds to fall behind the live point for stable playback.";
    option["value"].append(45000);
    config->addOption("maxkeepaway", option);
    capa["optional"]["maxkeepaway"]["name"] = "Maximum live keep-away distance";
    capa["optional"]["maxkeepaway"]["help"] = "Maximum distance in milliseconds to fall behind the live point for stable playback.";
    capa["optional"]["maxkeepaway"]["option"] = "--resume";
    capa["optional"]["maxkeepaway"]["type"] = "uint";
    capa["optional"]["maxkeepaway"]["default"] = 45000;
    maxKeepAway = 45000;
    option.null();

    option["arg"] = "integer";
    option["long"] = "segment-size";
    option["short"] = "S";
    option["help"] = "Target time duration in milliseconds for segments";
    option["value"].append(DEFAULT_FRAGMENT_DURATION);
    config->addOption("segmentsize", option);
    capa["optional"]["segmentsize"]["name"] = "Segment size (ms)";
    capa["optional"]["segmentsize"]["help"] = "Target time duration in milliseconds for segments.";
    capa["optional"]["segmentsize"]["option"] = "--segment-size";
    capa["optional"]["segmentsize"]["type"] = "uint";
    capa["optional"]["segmentsize"]["default"] = DEFAULT_FRAGMENT_DURATION;

    capa["optional"]["fallback_stream"]["name"] = "Fallback stream";
    capa["optional"]["fallback_stream"]["help"] =
        "Alternative stream to load for playback when there is no active broadcast";
    capa["optional"]["fallback_stream"]["type"] = "str";
    capa["optional"]["fallback_stream"]["default"] = "";
    option.null();
    /*LTS-end*/

    capa["source_match"] = "push://*";
    capa["non-provider"] = true; // Indicates we don't provide data, only collect it
    capa["priority"] = 9;
    capa["desc"] =
        "This input type is both used for push- and pull-based streams. It provides a buffer for "
        "live media data. The push://[host][@password] style source allows all enabled protocols "
        "that support push input to accept a push into MistServer, where you can accept incoming "
        "streams from everyone, based on a set password, and/or use hostname/IP whitelisting.";
    bufferTime = 50000;
    cutTime = 0;
    segmentSize = DEFAULT_FRAGMENT_DURATION;
    hasPush = false;
    everHadPush = false;
    resumeMode = false;
  }

  inputBuffer::~inputBuffer(){
    config->is_active = false;
    if (liveMeta){
      liveMeta->unlink();
      delete liveMeta;
      liveMeta = 0;
    }
  }

  /// Cleans up any left-over data for the current stream
  void inputBuffer::onCrash(){
    WARN_MSG("Buffer crashed. Cleaning.");
    streamName = config->getString("streamname");

    // Scoping to clear up users page
    {
      Comms::Users cleanUsers;
      cleanUsers.reload(streamName);
      cleanUsers.finishAll();
      cleanUsers.setMaster(true);
    }
    // Delete the live stream semaphore, if any.
    if (liveMeta){liveMeta->unlink();}
    // Scoping to clear up metadata pages
    {
      DTSC::Meta cleanMeta(streamName, false);
      cleanMeta.setMaster(true);
    }
  }

  /*LTS-START*/
  static bool liveBW(const char *param, const void *bwPtr){
    if (!param || !bwPtr){return false;}
    INFO_MSG("Comparing %s to %" PRIu32, param, *((uint32_t *)bwPtr));
    return JSON::Value(param).asInt() <= *((uint32_t *)bwPtr);
  }
  /*LTS-END*/

  /// \triggers
  /// The `"STREAM_BUFFER"` trigger is stream-specific, and is ran whenever the buffer changes state
  /// between playable (FULL) or not (EMPTY). It cannot be cancelled. It is possible to receive
  /// multiple EMPTY calls without FULL calls in between, as EMPTY is always generated when a stream
  /// is unloaded from memory, even if this stream never reached playable state in the first place
  /// (e.g. a broadcast was cancelled before filling enough buffer to be playable). Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// FULL, EMPTY, DRY or RECOVER (depending on current state)
  /// Detected issues in string format, or empty string if no issues
  /// ~~~~~~~~~~~~~~~
  void inputBuffer::updateMeta(){
    if (!M){
      Util::logExitReason(ER_SHM_LOST, "Lost connection to metadata");
      return;
    }
    static bool wentDry = false;
    static uint64_t lastFragCount = 0xFFFFull;
    static uint32_t lastBPS = 0; /*LTS*/
    uint32_t currBPS = 0;
    uint64_t firstms = 0xFFFFFFFFFFFFFFFFull;
    uint64_t lastms = 0;
    uint64_t fragCount = 0xFFFFull;
    std::set<size_t> validTracks = M.getValidTracks();
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
      size_t i = *it;
      currBPS += M.getBps(i); /*LTS*/
      if (M.getType(i) == "meta" || !M.getType(i).size()){continue;}
      std::string init = M.getInit(i);
      // Prevent init data from being thrown away
      if (init.size()){
        if (!initData.count(i) || initData[i] != init){initData[i] = init;}
      }else{
        if (initData.count(i)){meta.setInit(i, initData[i]);}
      }
      DTSC::Fragments fragments(M.fragments(i));
      if (fragments.getEndValid() < fragCount){fragCount = fragments.getEndValid();}
      if (M.getFirstms(i) < firstms){firstms = M.getFirstms(i);}
      if (M.getLastms(i) > lastms){lastms = M.getLastms(i);}
    }
    /*LTS-START*/
    if (currBPS != lastBPS){
      lastBPS = currBPS;
      if (Triggers::shouldTrigger("LIVE_BANDWIDTH", streamName, liveBW, &lastBPS)){
        std::stringstream pl;
        pl << streamName << "\n" << lastBPS;
        std::string payload = pl.str();
        if (!Triggers::doTrigger("LIVE_BANDWIDTH", payload, streamName)){
          WARN_MSG("Shutting down buffer because bandwidth limit reached!");
          config->is_active = false;
          userSelect.clear();
        }
      }
    }
    if (fragCount >= FRAG_BOOT && fragCount != 0xFFFFull && Triggers::shouldTrigger("STREAM_BUFFER", streamName)){
      JSON::Value stream_details;
      M.getHealthJSON(stream_details);
      if (lastFragCount == 0xFFFFull){
        std::string payload = streamName + "\nFULL\n" + stream_details.toString();
        Triggers::doTrigger("STREAM_BUFFER", payload, streamName);
      }else{
        if (stream_details.isMember("issues") != wentDry){
          if (stream_details.isMember("issues")){
            std::string payload = streamName + "\nDRY\n" + stream_details.toString();
            Triggers::doTrigger("STREAM_BUFFER", payload, streamName);
          }else{
            std::string payload = streamName + "\nRECOVER\n" + stream_details.toString();
            Triggers::doTrigger("STREAM_BUFFER", payload, streamName);
          }
        }
      }
      wentDry = stream_details.isMember("issues");
      lastFragCount = fragCount;
    }
    /*LTS-END*/
    finalMillis = lastms;
    meta.setBufferWindow(lastms - firstms);
    meta.setLive(true);
  }

  /// Checks if removing a key from this track is allowed/safe, and if so, removes it.
  /// Returns true if a key was actually removed, false otherwise
  /// Aborts if any of the following conditions are true (while active):
  /// * no keys present
  /// * not at least 4 whole fragments present
  /// * first fragment hasn't been at least lastms-firstms ms in buffer
  /// * less than 8 times the biggest fragment duration is buffered
  /// If a key was deleted and the first buffered data page is no longer used, it is deleted also.
  bool inputBuffer::removeKey(size_t tid){
    DTSC::Keys keys(M.keys(tid));
    // If this track is empty, abort
    if (!keys.getValidCount()){return false;}
    // the following checks only run if we're not shutting down
    if (config->is_active){
      // Make sure we have at least 4 whole fragments at all times,
      DTSC::Fragments fragments(M.fragments(tid));
      if (fragments.getValidCount() < 5){return false;}
      // ensure we have each fragment buffered for at least the whole bufferTime
      if ((M.getLastms(tid) - M.getFirstms(tid)) < bufferTime){return false;}
      uint32_t firstFragment = fragments.getFirstValid();
      uint32_t endFragment = fragments.getEndValid();
      if (endFragment - firstFragment > 2){
        /// Make sure we have at least 8X the target duration.
        // The target duration is the biggest fragment, rounded up to whole seconds.
        uint64_t targetDuration = (M.biggestFragment(tid) / 1000 + 1) * 1000;
        // The start is the third fragment's begin
        uint64_t fragStart = keys.getTime(fragments.getFirstKey(firstFragment));
        // The end is the last fragment's begin
        uint64_t fragEnd = keys.getTime(fragments.getFirstKey(endFragment - 1));
        if ((fragEnd - fragStart) < (targetDuration * 8)){return false;}
      }
    }
    // Alright, everything looks good, let's delete the key and possibly also fragment
    return meta.removeFirstKey(tid);
  }

  void inputBuffer::finish(){
    if (M.getValidTracks().size()){
      /*LTS-START*/
      if (M.getBufferWindow()){
        if (Triggers::shouldTrigger("STREAM_BUFFER")){
          std::string payload =
              config->getString("streamname") + "\nEMPTY\n" + JSON::Value(finalMillis).asString();
          Triggers::doTrigger("STREAM_BUFFER", payload, config->getString("streamname"));
        }
      }
      /*LTS-END*/
    }
    Input::finish();
    updateMeta();
  }

  void inputBuffer::removeTrack(size_t tid){
    size_t lastUser = users.recordCount();
    for (size_t i = 0; i < lastUser; ++i){
      if (users.getStatus(i) == COMM_STATUS_INVALID){continue;}
      if (!(users.getStatus(i) & COMM_STATUS_SOURCE)){continue;}
      if (users.getTrack(i) != tid){continue;}
      // We have found the right track here (pid matches, and COMM_STATUS_SOURCE set)
      users.setStatus(COMM_STATUS_REQDISCONNECT | users.getStatus(i), i);
      break;
    }

    INFO_MSG("Should remove track %zu", tid);
    meta.reloadReplacedPagesIfNeeded();
    meta.removeTrack(tid);
    /*LTS-START*/
    if (!M.getValidTracks().size()){
      if (Triggers::shouldTrigger("STREAM_BUFFER")){
        std::string payload = config->getString("streamname") + "\nEMPTY";
        Triggers::doTrigger("STREAM_BUFFER", payload, config->getString("streamname"));
      }
    }
    /*LTS-END*/
  }

  void inputBuffer::removeUnused(){
    meta.reloadReplacedPagesIfNeeded();
    if (!meta){
      return;
    }
    // first remove all tracks that have not been updated for too long
    bool changed = true;
    while (changed){
      changed = false;
      uint64_t time = Util::bootSecs();
      uint64_t compareFirst = 0xFFFFFFFFFFFFFFFFull;
      uint64_t compareLast = 0;
      std::set<std::string> activeTypes;

      std::set<size_t> tracks = M.getValidTracks();
      // for tracks that were updated in the last 5 seconds, get the first and last ms edges.
      for (std::set<size_t>::iterator idx = tracks.begin(); idx != tracks.end(); idx++){
        size_t i = *idx;
        if ((time - M.getLastUpdated(i)) > 5){continue;}
        activeTypes.insert(M.getType(i));
        if (M.getLastms(i) > compareLast){compareLast = M.getLastms(i);}
        if (M.getFirstms(i) < compareFirst){compareFirst = M.getFirstms(i);}
      }
      for (std::set<size_t>::iterator idx = tracks.begin(); idx != tracks.end(); idx++){
        size_t i = *idx;
        //Don't delete idle metadata tracks
        if (M.getType(i) == "meta"){continue;}
        uint64_t lastUp = M.getLastUpdated(i);
        //Prevent issues when getLastUpdated > current time. This can happen if the second rolls over exactly during this loop.
        if (lastUp >= time){continue;}
        std::string codec = M.getCodec(i);
        std::string type = M.getType(i);
        uint64_t firstms = M.getFirstms(i);
        uint64_t lastms = M.getLastms(i);
        // if not updated for an entire buffer duration, or last updated track and this track differ
        // by an entire buffer duration, erase the track.
        if ((time - lastUp > (bufferTime / 1000) ||
             (compareLast && activeTypes.count(type) && (time - lastUp) > 5 &&
              ((compareLast < firstms && (firstms - compareLast) > bufferTime) ||
               (compareFirst > lastms && (compareFirst - lastms) > bufferTime))))){
          // erase this track
          if ((time - lastUp) > (bufferTime / 1000)){
            WARN_MSG("Erasing %s track %zu (%s/%s) because not updated for %" PRIu64 "s (> %" PRIu64 "s)",
                     streamName.c_str(), i, type.c_str(), codec.c_str(), time - lastUp,
                     bufferTime / 1000);
          }else{
            WARN_MSG("Erasing %s inactive track %zu (%s/%s) because it was inactive for 5+ seconds "
                     "and contains data (%" PRIu64 "s - %" PRIu64
                     "s), while active tracks are (%" PRIu64 "s - %" PRIu64
                     "s), which is more than %" PRIu64 "s seconds apart.",
                     streamName.c_str(), i, type.c_str(), codec.c_str(), firstms / 1000,
                     lastms / 1000, compareFirst / 1000, compareLast / 1000, bufferTime / 1000);
          }
          meta.reloadReplacedPagesIfNeeded();
          removeTrack(i);
          changed = true;
          break;
        }
      }
    }

    std::set<size_t> tracks = M.getValidTracks();

    // find the earliest video keyframe stored
    uint64_t videoFirstms = 0xFFFFFFFFFFFFFFFFull;

    for (std::set<size_t>::iterator idx = tracks.begin(); idx != tracks.end(); idx++){
      size_t i = *idx;
      if (M.getType(i) == "video"){
        if (M.getFirstms(i) < videoFirstms){videoFirstms = M.getFirstms(i);}
      }
    }
    for (std::set<size_t>::iterator idx = tracks.begin(); idx != tracks.end(); idx++){
      size_t i = *idx;
      std::string type = M.getType(i);
      DTSC::Keys keys(M.keys(i));
      // non-video tracks need to have a second keyframe that is <= firstVideo
      // firstVideo = 1 happens when there are no tracks, in which case we don't care any more
      uint32_t firstKey = keys.getFirstValid();
      uint32_t endKey = keys.getEndValid();
      if (type != "video" && videoFirstms != 0xFFFFFFFFFFFFFFFFull){
        if ((endKey - firstKey) < 2 || keys.getTime(firstKey + 1) > videoFirstms){continue;}
      }
      // Buffer cutting
      while (keys.getValidCount() > 1 && keys.getTime(keys.getFirstValid()) < cutTime){
        if (!removeKey(i)){break;}
      }
      // Buffer size management
      /// \TODO Make sure data has been in the buffer for at least bufferTime after it goes in
      while (keys.getValidCount() > 1 && (M.getLastms(i) - keys.getTime(keys.getFirstValid() + 1)) > bufferTime){
        if (!removeKey(i)){break;}
      }
    }
    updateMeta();
  }

  void inputBuffer::userLeadIn(){
    meta.reloadReplacedPagesIfNeeded();
    /*LTS-START*/
    // Reload the configuration to make sure we stay up to date with changes through the api
    if (Util::epoch() - lastReTime > 4){preRun();}
    size_t procInterval = 5000;
    if (!firstProcTime || Util::bootMS() - firstProcTime < 30000){
      if (!firstProcTime){firstProcTime = Util::bootMS();}
      if (Util::bootMS() - firstProcTime < 10000){
        procInterval = 200;
      }else{
        procInterval = 1000;
      }
    }
    if (Util::bootMS() - lastProcTime > procInterval){
      lastProcTime = Util::bootMS();
      std::string strName = config->getString("streamname");
      Util::sanitizeName(strName);
      strName = strName.substr(0, (strName.find_first_of("+ ")));
      char tmpBuf[NAME_BUFFER_SIZE];
      snprintf(tmpBuf, NAME_BUFFER_SIZE, SHM_STREAM_CONF, strName.c_str());
      Util::DTSCShmReader rStrmConf(tmpBuf);
      DTSC::Scan streamCfg = rStrmConf.getScan();
      if (streamCfg){
        JSON::Value configuredProcesses = streamCfg.getMember("processes").asJSON();
        checkProcesses(configuredProcesses);
      }else{
        //If there is no config, we assume all processes are running, since, well, there can't be any
        allProcsRunning = true;
      }
    }
    /*LTS-END*/
    connectedUsers = 0;

    //Store child process PIDs in generatePids.
    //These are controlled by the buffer (usually processes) and should not count towards incoming pushes
    generatePids.clear();
    for (std::map<std::string, pid_t>::iterator it = runningProcs.begin(); it != runningProcs.end(); it++){
      generatePids.insert(it->second);
    }
    hasPush = false;
  }
  void inputBuffer::userOnActive(size_t id){
    ///\todo Add tracing of earliest watched keys, to prevent data going out of memory for
    /// still-watching viewers
    if (!(users.getStatus(id) & COMM_STATUS_DISCONNECT) && (users.getStatus(id) & COMM_STATUS_SOURCE)){
      sourcePids[id] = users.getTrack(id);
      // GeneratePids holds the pids of the process that generate data, so ignore those for determining if a push is ingested.
      if (M.trackValid(users.getTrack(id)) && !generatePids.count(users.getPid(id))){hasPush = true;}
    }

    if (!(users.getStatus(id) & COMM_STATUS_DONOTTRACK)){++connectedUsers;}
  }
  void inputBuffer::userOnDisconnect(size_t id){
    if (sourcePids.count(id)){
      if (!resumeMode){
        INFO_MSG("Disconnected track %zu", sourcePids[id]);
        meta.reloadReplacedPagesIfNeeded();
        removeTrack(sourcePids[id]);
      }else{
        INFO_MSG("Track %zu lost its source, keeping it around for resume", sourcePids[id]);
      }
      sourcePids.erase(id);
    }
  }
  void inputBuffer::userLeadOut(){
    if (config->is_active && streamStatus){
      streamStatus.mapped[0] = (hasPush && allProcsRunning) ? STRMSTAT_READY : STRMSTAT_WAIT;
    }
    if (hasPush){everHadPush = true;}
    if (!hasPush && everHadPush && !resumeMode && config->is_active){
      Util::logExitReason(ER_CLEAN_EOF, "source disconnected for non-resumable stream");
      if (streamStatus){streamStatus.mapped[0] = STRMSTAT_SHUTDOWN;}
      config->is_active = false;
      userSelect.clear();
    }
    /*LTS-START*/
    static std::set<size_t> prevValidTracks;

    std::set<size_t> validTracks = M.getValidTracks();
    if (validTracks != prevValidTracks){
      MEDIUM_MSG("Valid tracks count changed from %zu to %zu", prevValidTracks.size(), validTracks.size());
      prevValidTracks = validTracks;
      if (Triggers::shouldTrigger("LIVE_TRACK_LIST")){
        JSON::Value triggerPayload;
        M.toJSON(triggerPayload, true, true);
        std::string payload = config->getString("streamname") + "\n" + triggerPayload.toString() + "\n";
        Triggers::doTrigger("LIVE_TRACK_LIST", payload, config->getString("streamname"));
      }
    }
    /*LTS-END*/
  }

  uint64_t inputBuffer::retrieveSetting(DTSC::Scan &streamCfg, const std::string &setting,
                                        const std::string &option){
    std::string opt = (option == "" ? setting : option);
    // If stream is not configured, use commandline option
    if (!streamCfg){return config->getOption(opt).asInt();}
    // If it is configured, and the setting is present, use it always
    if (streamCfg.getMember(setting)){return streamCfg.getMember(setting).asInt();}
    // If configured, but setting not present, fall back to default
    return config->getOption(opt, true)[0u].asInt();
  }

  bool inputBuffer::preRun(){
    // This function gets run periodically to make sure runtime updates of the config get parsed.
    Util::Procs::kill_timeout = 5;
    std::string strName = config->getString("streamname");
    Util::sanitizeName(strName);
    strName = strName.substr(0, (strName.find_first_of("+ ")));
    char tmpBuf[NAME_BUFFER_SIZE];
    snprintf(tmpBuf, NAME_BUFFER_SIZE, SHM_STREAM_CONF, strName.c_str());
    Util::DTSCShmReader rStrmConf(tmpBuf);
    DTSC::Scan streamCfg = rStrmConf.getScan();

    //Check if bufferTime setting is correct
    uint64_t tmpNum = retrieveSetting(streamCfg, "DVR", "bufferTime");
    if (tmpNum < 1000){tmpNum = 1000;}
    if (bufferTime != tmpNum){
      DEVEL_MSG("Setting bufferTime from %" PRIu64 " to new value of %" PRIu64, bufferTime, tmpNum);
      bufferTime = tmpNum;
    }

    /*LTS-START*/
    //Check if cutTime setting is correct
    tmpNum = retrieveSetting(streamCfg, "cut");
    // if the new value is different, print a message and apply it
    if (cutTime != tmpNum){
      INFO_MSG("Setting cutTime from %" PRIu64 " to new value of %" PRIu64, cutTime, tmpNum);
      cutTime = tmpNum;
    }

    //Check if resume setting is correct
    tmpNum = retrieveSetting(streamCfg, "resume");
    if (resumeMode != (bool)tmpNum){
      INFO_MSG("Setting resume mode from %s to new value of %s",
               resumeMode ? "enabled" : "disabled", tmpNum ? "enabled" : "disabled");
      resumeMode = tmpNum;
    }

    if (!meta){return true;}//abort the rest if we can't write metadata
    lastReTime = Util::epoch(); /*LTS*/

    //Check if segmentsize setting is correct
    tmpNum = retrieveSetting(streamCfg, "segmentsize");
    if (tmpNum < meta.biggestFragment() / 2){tmpNum = meta.biggestFragment() / 2;}
    segmentSize = meta.getMinimumFragmentDuration();
    if (segmentSize != tmpNum){
      INFO_MSG("Setting segmentSize from %zu to new value of %" PRIu64, segmentSize, tmpNum);
      segmentSize = tmpNum;
      meta.setMinimumFragmentDuration(segmentSize);
    }

    //Check if segmentsize setting is correct
    tmpNum = retrieveSetting(streamCfg, "maxkeepaway");
    if (M.getMaxKeepAway() != tmpNum){
      INFO_MSG("Setting maxKeepAway from %" PRIu64 " to new value of %" PRIu64, M.getMaxKeepAway(), tmpNum);
      meta.setMaxKeepAway(tmpNum);
    }

    /*LTS-END*/
    return true;
  }

  uint64_t inputBuffer::findTrack(const std::string &trackVal){
    std::set<size_t> validTracks = M.getValidTracks();
    if (!validTracks.size()){
      return INVALID_TRACK_ID;
    }// No tracks == we don't have a valid
                                                           // track
    if (!trackVal.size() || trackVal == "0"){return 0;}// don't select anything in particular
    if (trackVal.find(',') != std::string::npos){
      // Comma-separated list, recurse.
      std::stringstream ss(trackVal);
      std::string item;
      while (std::getline(ss, item, ',')){
        uint64_t r = findTrack(item);
        if (r){return r;}// return first match
      }
      return INVALID_TRACK_ID; // nothing found
    }
    uint64_t trackNo = JSON::Value(trackVal).asInt();
    if (trackVal == JSON::Value(trackNo).asString()){
      // It's an integer number
      if (!validTracks.count(trackNo)){
        return INVALID_TRACK_ID; // nothing found
      }
      return trackNo;
    }
    std::string trackLow = trackVal;
    Util::stringToLower(trackLow);
    if (trackLow == "all" || trackLow == "*"){
      // select all tracks of this type
      return *validTracks.begin();
    }
    // attempt to do language/codec matching
    // convert 2-character language codes into 3-character language codes
    if (trackLow.size() == 2){trackLow = Encodings::ISO639::twoToThree(trackLow);}
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
      std::string codecLow = M.getCodec(*it);
      Util::stringToLower(codecLow);
      if (M.getLang(*it) == trackLow || trackLow == codecLow){return *it;}
    }
    return INVALID_TRACK_ID; // nothing found
  }

  /*LTS-START*/
  /// Checks if all processes are running, starts them if needed, stops them if needed
  void inputBuffer::checkProcesses(const JSON::Value &procs){
    allProcsRunning = true;
    if (!M.getValidTracks().size()){return;}
    std::set<std::string> newProcs;

    // used for building args
    int err = fileno(stderr);
    char *argarr[5];

    // Convert to strings
    jsonForEachConst(procs, it){
      JSON::Value tmp = *it;
      tmp["source"] = streamName;
      if (!M.getValidTracks().size() &&
          (!tmp.isMember("source_track") && !tmp.isMember("track_select"))){
        continue;
      }
      if (tmp.isMember("source_track")){
        std::set<size_t> wouldSelect = Util::findTracks(M, JSON::Value(), "", tmp["source_track"].asStringRef());
        // No match - skip this process
        if (!wouldSelect.size()){continue;}
      }
      if (tmp.isMember("track_select")){
        std::set<size_t> wouldSelect = Util::wouldSelect(M, tmp["track_select"].asStringRef());
        // No match - skip this process
        if (!wouldSelect.size()){continue;}
      }
      if (tmp.isMember("track_inhibit")){
        std::set<size_t> wouldSelect = Util::wouldSelect(
            M, std::string("audio=none&video=none&subtitle=none&") + tmp["track_inhibit"].asStringRef());
        if (wouldSelect.size()){
          // Inhibit if there is a match and we're not already running.
          if (!runningProcs.count(tmp.toString())){continue;}
          bool inhibited = false;
          std::set<size_t> myTracks = M.getMySourceTracks(runningProcs[tmp.toString()]);
          // Also inhibit if there is a match with not-the-currently-running-process
          for (std::set<size_t>::iterator it = wouldSelect.begin(); it != wouldSelect.end(); ++it){
            if (!myTracks.count(*it)){inhibited = true;}
          }
          if (inhibited){continue;}
        }
      }
      newProcs.insert(tmp.toString());
    }

    // shut down deleted/changed processes
    std::map<std::string, pid_t>::iterator it;
    if (runningProcs.size()){
      for (it = runningProcs.begin(); it != runningProcs.end(); it++){
        if (!newProcs.count(it->first)){
          if (Util::Procs::isActive(it->second)){
            INFO_MSG("Stopping process %d: %s", it->second, it->first.c_str());
            Util::Procs::Stop(it->second);
          }
          runningProcs.erase(it);
          if (!runningProcs.size()){break;}
          it = runningProcs.begin();
        }
      }
    }

    std::string debugLvl;
    // start up new/changed connectors
    while (newProcs.size() && config->is_active){
      const std::string & config = (*newProcs.begin());
      JSON::Value args = JSON::fromString(config);
      if (!runningProcs.count(config) || !Util::Procs::isActive(runningProcs[config])){
        std::string procname =
            Util::getMyPath() + "MistProc" + JSON::fromString(config)["process"].asString();
        argarr[0] = (char *)procname.c_str();
        argarr[1] = (char *)config.c_str();
        argarr[2] = 0;
        if (Util::printDebugLevel != DEBUG || args.isMember("debug")){
          if (args.isMember("debug")){
            debugLvl = args["debug"].asString();
          }else{
            debugLvl = JSON::Value(Util::printDebugLevel).asString();
          }
          argarr[2] = (char*)"--debug";
          argarr[3] = (char*)debugLvl.c_str();;
          argarr[4] = 0;
        }
        allProcsRunning = false;
        INFO_MSG("Starting process: %s %s", argarr[0], argarr[1]);
        runningProcs[*newProcs.begin()] = Util::Procs::StartPiped(argarr, 0, 0, &err);
      }
      newProcs.erase(newProcs.begin());
    }
  }
  /*LTS-END*/

}// namespace Mist
