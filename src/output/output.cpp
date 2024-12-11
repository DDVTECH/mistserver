#include <algorithm>
#include <fcntl.h>
#include <iterator> //std::distance
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iomanip>
#include <fstream>
#include <sstream>

#include "output.h" 
#include <mist/bitfields.h>
#include <mist/defines.h>
#include <mist/h264.h>
#include <mist/http_parser.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/util.h>
#include <mist/urireader.h>
#include <sys/file.h>
#include <mist/encode.h>

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
    option.null();


    capa["optional"]["default_track_sorting"]["name"] = "Default track sorting";
    capa["optional"]["default_track_sorting"]["help"] = "What tracks are selected first when no specific track selector is used for playback.";
    capa["optional"]["default_track_sorting"]["default"] = "";
    capa["optional"]["default_track_sorting"]["type"] = "select";
    capa["optional"]["default_track_sorting"]["option"] = "--default_track_sorting";
    capa["optional"]["default_track_sorting"]["short"] = "S";
    option.append("");
    option.append("Default (last added for live, first added for VoD)");
    capa["optional"]["default_track_sorting"]["select"].append(option);
    option.null();
    option.append("bps_lth");
    option.append("Bit rate, low to high");
    capa["optional"]["default_track_sorting"]["select"].append(option);
    option.null();
    option.append("bps_htl");
    option.append("Bit rate, high to low");
    capa["optional"]["default_track_sorting"]["select"].append(option);
    option.null();
    option.append("id_lth");
    option.append("Track ID, low to high");
    capa["optional"]["default_track_sorting"]["select"].append(option);
    option.null();
    option.append("id_htl");
    option.append("Track ID, high to low");
    capa["optional"]["default_track_sorting"]["select"].append(option);
    option.null();
    option.append("res_lth");
    option.append("Resolution, low to high");
    capa["optional"]["default_track_sorting"]["select"].append(option);
    option.null();
    option.append("res_htl");
    option.append("Resolution, high to low");
    capa["optional"]["default_track_sorting"]["select"].append(option);

    config = cfg;
  }

  Output::Output(Socket::Connection &conn) : myConn(conn){
    dataWaitTimeout = 2500;
    thisBootMs = Util::bootMS();
    pushing = false;
    recursingSync = false;
    firstTime = Util::bootMS();
    thisTime = 0;
    firstPacketTime = 0xFFFFFFFFFFFFFFFFull;
    lastPacketTime = 0;
    tkn = "";
    parseData = false;
    wantRequest = true;
    sought = false;
    isInitialized = false;
    isBlocking = false;
    needsLookAhead = 0;
    lastStats = 0xFFFFFFFFFFFFFFFFull;
    maxSkipAhead = 7500;
    uaDelay = 10;
    realTime = 1000;
    emptyCount = 0;
    seekCount = 2;
    firstData = true;
    newUA = true;
    lastPushUpdate = 0;
    Util::Config::binaryType = Util::OUTPUT;

    lastRecv = Util::bootSecs();
    outputStartMs = Util::bootMS();
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
    Util::logExitReason(ER_UNKNOWN, msg.c_str());
    isInitialized = false;
    wantRequest = false;
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
    /*LTS-END*/
  }

  std::string Output::getConnectedHost(){return myConn.getHost();}

  std::string Output::getConnectedBinHost(){
    if (!prevHost.size()){
      MEDIUM_MSG("Setting prevHost to %s", getConnectedHost().c_str());
      prevHost = myConn.getBinHost();
      if (!prevHost.size()){prevHost.assign("\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000", 16);}
    }
    return prevHost;
  }

  bool Output::isReadyForPlay(){
    // If a protocol does not support any codecs, we assume you know what you're doing
    if (!capa.isMember("codecs") || !capa["codecs"].size() || !capa["codecs"].isArray() || !capa["codecs"][0u].size()){return true;}

    // Defaults for the below values are configured here, and may be overridden with query parameters:
    size_t minTracks = 2; // Supported tracks to attempt to wait for
    size_t minMs = 500; // Minimum time that must be buffered for a track to count as available
    size_t minKeys = 2; // Minimum keyframes that must be buffered for a track to count as available
    size_t maxWait = 5000; // Maximum time to wait for tracks
    size_t maxMs = 20000; // If this much time is buffered in any track, don't wait on anything else anymore

    if (targetParams.count("waittrackcount")){
      minTracks = JSON::Value(targetParams["waittrackcount"]).asInt();
      // Non-default wait times get a 2 minute default timeout instead of the usually 5 seconds default.
      // (May still be overridden)
      maxWait = 120000;
    }
    if (targetParams.count("mintrackms")){
      minMs = JSON::Value(targetParams["mintrackms"]).asInt();
    }
    if (targetParams.count("maxtrackms")){
      maxMs = JSON::Value(targetParams["maxtrackms"]).asInt();
    }
    if (targetParams.count("mintrackkeys")){
      minKeys = JSON::Value(targetParams["mintrackkeys"]).asInt();
    }
    if (targetParams.count("maxwaittrackms")){
      maxWait = JSON::Value(targetParams["maxwaittrackms"]).asInt();
    }

    if (outputStartMs + maxWait < Util::bootMS()){
      HIGH_MSG("isReadyForPlay timed out waiting for tracks (%" PRId64 " > %zu); continuing with what we have", Util::bootMS()-outputStartMs, maxWait);
      return true;
    }

    //This logic is copied from selectDefaultTracks, and should be kept in sync with it
    if (!isInitialized){initialize();}
    meta.reloadReplacedPagesIfNeeded();
    bool autoSeek = buffer.size();
    uint64_t seekTarget = buffer.getSyncMode()?currentTime():0;
    std::set<size_t> trks = Util::wouldSelect(M, targetParams, capa, UA, autoSeek ? seekTarget : 0);

    size_t trkCount = 0;
    for (std::set<size_t>::iterator it = trks.begin(); it != trks.end(); ++it){
      DTSC::Keys keys(M.keys(*it));
      if (keys.getValidCount() >= minKeys || M.getLastms(*it) - M.getFirstms(*it) > minMs){
        ++trkCount;
        if (trkCount >= minTracks){return true;}
      }
      if (M.getLastms(*it) - M.getFirstms(*it) > maxMs){
        HIGH_MSG("isReadyForPlay skipping wait because track %zu has %" PRId64 "ms buffered", *it, M.getLastms(*it) - M.getFirstms(*it));
        return true;
      }
    }

    HIGH_MSG("isReadyForPlay waiting for tracks: %zu/%zu/%zu tracks, maxWait %zu, minMs %zu, minKeys %zu", trkCount, trks.size(), minTracks, maxWait, minMs, minKeys);
    return false;
  }

  /// Disconnects from all stat/user/metadata-related shared structures.
  void Output::disconnect(){
    MEDIUM_MSG("disconnect");
    if (statComm){
      stats(true);
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
  void Output::reconnect(){
    Comms::sessionConfigCache();
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
          std::string defStrm = strCnf["fallback_stream"].asStringRef();
          std::string newStrm = defStrm;
          Util::streamVariables(newStrm, streamName, "");
          INFO_MSG("Switching to configured fallback stream '%s' -> '%s'", defStrm.c_str(), newStrm.c_str());
          streamName = newStrm;
          Util::setStreamName(streamName);
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

    //Wipe currently selected tracks; metadata unload coming up
    userSelect.clear();

    //Connect to stream metadata
    meta.reInit(streamName, false);
    unsigned int attempts = 0;
    while (!meta && ++attempts < 20 && Util::streamAlive(streamName)){
      meta.reInit(streamName, false);
    }
    //Abort if this step failed
    if (!meta){return;}

    isInitialized = true;

    //Connect to stats reporting, if not connected already
    stats(true);
    //Abort if the stats code shut us down just now
    if (!isInitialized){return;}

    //push inputs do not need to wait for stream to be ready for playback
    if (isPushing()){return;}

    //live streams that are no push outputs (recordings), wait for stream to be ready
    if (!isRecording() && M && M.getLive() && !isReadyForPlay()){
      uint64_t waitUntil = Util::bootSecs() + 45;
      while (M && M.getLive() && !isReadyForPlay()){
        if (Util::bootSecs() > waitUntil || (!userSelect.size() && Util::bootSecs() > waitUntil)){
          INFO_MSG("Giving up waiting for playable tracks. IP: %s", getConnectedHost().c_str());
          break;
        }
        Util::wait(500);
        meta.reloadReplacedPagesIfNeeded();
        if (!meta){
          isInitialized = true;
          return;
        }
        stats();
      }
    }
    if (!M){return;}
    //Finally, select the default tracks
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
    if (!M){
      userSelect.clear();
      buffer.clear();
      return true;
    }

    bool autoSeek = buffer.size();
    uint64_t seekTarget = buffer.getSyncMode()?thisTime:0;
    std::set<size_t> newSelects =
        Util::wouldSelect(M, targetParams, capa, UA, autoSeek ? seekTarget : 0);

    if (autoSeek){
      std::set<size_t> toRemove;
      for (std::set<size_t>::iterator it = newSelects.begin(); it != newSelects.end(); it++){
        // autoSeeking and target not in bounds? Drop it too.
        if (M.getNowms(*it) < std::max(seekTarget, (uint64_t)6000lu) - 6000){
          toRemove.insert(*it);
        }
      }
      // remove those from selectedtracks
      for (std::set<size_t>::iterator it = toRemove.begin(); it != toRemove.end(); it++){
        newSelects.erase(*it);
      }
    }

    std::set<size_t> oldSelects;
    buffer.getTrackList(oldSelects);
    std::map<size_t, uint64_t> seekTargets;
    buffer.getTrackList(seekTargets);

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
        continue;
      }
    }

    newSelects.clear();
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); ++it){
      newSelects.insert(it->first);
    }

    //After attempting to add/remove tracks, now no changes? Abort and return false;
    if (oldSelects == newSelects){return false;}

    if (autoSeek){
      buffer.clear();
      INFO_MSG("Automatically seeking to resume playback");
      if (!seekTargets.size()){
        initialSeek();
      }else{
        for (std::set<size_t>::iterator it = newSelects.begin(); it != newSelects.end(); it++){
          if (seekTargets.count(*it)){
            seek(*it, seekTargets[*it], false);
          }else{
            if (buffer.getSyncMode()){
              seek(*it, seekTargets.begin()->second, false);
            }else{
              seek(*it, 0, false);
            }
          }
        }
      }
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
    const DTSC::Keys &keys = M.getKeys(trk);
    //Abort if there are no keys
    if (!keys.getValidCount()){return 0;}
    //Get the key for the current time
    size_t keyNum = M.getKeyNumForTime(trk, thisTime);
    if (keyNum == INVALID_KEY_NUM){return 0;}
    if (keys.getEndValid() <= keyNum+1){return 0;}
    //Return the next key
    return keys.getTime(keyNum+1);
  }
  
  uint64_t Output::pageNumForKey(size_t trackId, size_t keyNum){
    const Util::RelAccX &tPages = M.pages(trackId);
    for (uint64_t i = tPages.getDeleted(); i < tPages.getEndPos(); i++){
      uint64_t pageNum = tPages.getInt("firstkey", i);
      if (pageNum > keyNum) continue;
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

  /// \brief Removes entries in the playlist based on age or a maximum number of segments allowed
  /// \param playlistBuffer: the contents of the playlist file. Will be edited to contain fewer entries if applicable
  /// \param targetAge: maximum age of a segment in seconds. If 0, will not remove segments based on age
  /// \param maxEntries: maximum amount of segments that are allowed to appear in the playlist
  ///                    If 0, will not remove segments based on the segment count
  /// \param segmentCount: current counter of segments that have been segmented as part of this stream
  /// \param segmentsRemoved: counter of segments that have been removed previously from the playlist
  /// \param curTime: the current local timestamp in milliseconds 
  /// \param targetDuration: value to fill in for the EXT-X-TARGETDURATION entry in the playlist
  /// \param playlistLocation: the location of the playlist, used to find the path to segments when removing them
  void Output::reinitPlaylist(std::string &playlistBuffer, uint64_t &targetAge, uint64_t &maxEntries,
                              uint64_t &segmentCount, uint64_t &segmentsRemoved, uint64_t &curTime,
                              std::string targetDuration, HTTP::URL &playlistLocation){
    std::string newBuffer;
    std::istringstream stream(playlistBuffer);
    std::string line;
    std::string curDateString;
    std::string curDurationString;
    // Quits early if we have no more segments we need to remove
    bool done = false;
    bool hasSegment = false;
    while (std::getline(stream, line)){
      if (!line.size()){continue;}
      // Copy the rest of the file as is
      if (done){
        newBuffer += line + "\n";
        continue;
      }
      // Ignore init fields
      if (strncmp(line.c_str(), "#EXTM3U", 7) == 0){continue;}
      if (strncmp(line.c_str(), "#EXT-X-VERSION", 14) == 0){continue;}
      if (strncmp(line.c_str(), "#EXT-X-PLAYLIST-TYPE", 20) == 0){continue;}
      if (strncmp(line.c_str(), "#EXT-X-TARGETDURATION", 21) == 0){continue;}
      if (strncmp(line.c_str(), "#EXT-X-MEDIA-SEQUENCE", 21) == 0){continue;}
      if (!hasSegment && strncmp(line.c_str(), "#EXT-X-DISCONTINUITY", 20) == 0){continue;}
      // Save current segment info
      if (strncmp(line.c_str(), "#EXTINF", 7) == 0){
        curDurationString = line;
        continue;
      }
      if (strncmp(line.c_str(), "#EXT-X-PROGRAM-DATE-TIME", 21) == 0){
        curDateString = line;
        continue;
      }
      // Pass along any other lines starting with a # character as is
      if (line[0] == '#'){
        newBuffer += line + "\n";
        continue;
      }
      // The current line should be a segment path at this point
      // If we are above the max segment count or age, ignore this segment and reset info fields
      if (maxEntries && (segmentCount - segmentsRemoved >= maxEntries)){
        HIGH_MSG("Dropping segment #%" PRIu64 " from the playlist due to the playlist reaching it's max size of %" PRIu64 " segments", segmentsRemoved, maxEntries);
        curDateString = "";
        curDurationString = "";
        segmentsRemoved++;
        std::string segPath = playlistLocation.link(line).getFilePath();
        if(unlink(segPath.c_str())){
          FAIL_MSG("Failed to remove segment at '%s'. Error: '%s'", segPath.c_str(), strerror(errno));
        }else{
          INFO_MSG("Removed segment at '%s'", segPath.c_str());
        }
        continue;
      }
      if (targetAge && curTime && curDateString.size() > 25){
        uint64_t segmentDiff = Util::getUTCTimeDiff(curDateString.substr(25), curTime);
        if (segmentDiff > targetAge){
          HIGH_MSG("Dropping segment #%" PRIu64 " from the playlist due to old age (%" PRIu64 " s)", segmentsRemoved, segmentDiff);
          // If the segment is too old, ignore and reset fields
          curDurationString = "";
          curDateString = "";
          segmentsRemoved++;
          std::string segPath = playlistLocation.link(line).getFilePath();
          if(unlink(segPath.c_str())){
            FAIL_MSG("Failed to remove segment at '%s'. Error: '%s'", segPath.c_str(), strerror(errno));
          }else{
            INFO_MSG("Removed segment at '%s'", segPath.c_str());
          }
          continue;
        }
      }
      hasSegment = true;
      // Write segment info to the new buffer
      if (curDateString.size()){
        newBuffer += curDateString + "\n";
        curDateString = "";
      }
      if (curDurationString.size()){
        newBuffer += curDurationString + "\n";
        curDurationString = "";
      }
      newBuffer += line + "\n";
      // If we reach this point, the conditions of max age and entries have been met
      done = true;
    }
    // Write out new init data to the playlist buffer
    playlistBuffer = "#EXTM3U\n#EXT-X-VERSION:3\n";
    // Set the playlist as immutable when segmenting a non-live input
    if (!M.getLive()){
      playlistBuffer += "#EXT-X-PLAYLIST-TYPE:VOD\n";
    // Set the playlist as append only when segmenting a live input without removing older entries
    } else if (!maxEntries && !targetAge){
      playlistBuffer += "#EXT-X-PLAYLIST-TYPE:EVENT\n";
    }
    // Else don't add a playlist type at all, as the playlist will not be append only or immutable
    playlistBuffer += "#EXT-X-TARGETDURATION:" + targetDuration + "\n#EXT-X-MEDIA-SEQUENCE:" + JSON::Value(segmentsRemoved).asString() + "\n";
    // Finally append the rest of the playlist
    playlistBuffer += newBuffer;
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
    // Note: specifically uses `keys` rather than `getKeys` because pages must not be limited-based
    DTSC::Keys keys(M.keys(trackId));
    if (!keys.getValidCount()){
      WARN_MSG("Load for track %zu key %zu aborted - track is empty", trackId, keyNum);
      return;
    }
    size_t lastAvailKey = keys.getEndValid() - 1;
    if (!meta.getLive() && keyNum > lastAvailKey){
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
      meta.reloadReplacedPagesIfNeeded();
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

  /// Return the intended target current time of the media buffer (as opposed to actual)
  /// This takes into account the current playback speed as well as the maxSkipAhead setting.
  uint64_t Output::targetTime(){
    if (!realTime){return currentTime();}
    return (((Util::bootMS() - firstTime) * 1000) / realTime + maxSkipAhead);
  }

  /// Return the start time of the selected tracks.
  /// Returns the start time of earliest track if nothing is selected.
  /// Returns zero if no tracks exist.
  uint64_t Output::startTime(){
    std::set<size_t> validTracks = M.getValidTracks();
    if (!validTracks.size()){return 0;}
    uint64_t start = 0xFFFFFFFFFFFFFFFFull;
    uint64_t nonMetaStart = 0xFFFFFFFFFFFFFFFFull;
    if (userSelect.size()){
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        if (M.trackValid(it->first) && start > M.getFirstms(it->first)){
          start = M.getFirstms(it->first);
        }
        if (M.trackValid(it->first) && M.getType(it->first) != "meta" && nonMetaStart > M.getFirstms(it->first)){
          nonMetaStart = M.getFirstms(it->first);
        }
      }
    }else{
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
        if (start > M.getFirstms(*it)){start = M.getFirstms(*it);}
      }
    }
    return nonMetaStart != 0xFFFFFFFFFFFFFFFFull ? nonMetaStart: start;
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
    if (!M){return false;}
    if (toKey){
      size_t mainTrack = getMainSelectedTrack();
      if (mainTrack == INVALID_TRACK_ID){
        WARN_MSG("Sync-seeking impossible (main track invalid); performing regular seek instead");
        return seek(pos);
      }
      if (M.getType(mainTrack) == "video"){
        DTSC::Keys keys(M.getKeys(mainTrack));
        if (!keys.getValidCount()){
          FAIL_MSG("Attempted seek on empty track %zu", mainTrack);
          return false;
        }
        // Snap to the keyframe's exact starting time
        pos = keys.getTime(keys.getIndexForTime(pos));
      }
    }
    MEDIUM_MSG("Seeking to %" PRIu64 "ms (%s)", pos, toKey ? "sync" : "direct");
    std::set<size_t> seekTracks;
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      if (M.trackValid(it->first)){seekTracks.insert(it->first);}
    }
    //Seek all seek positions, first
    for (std::set<size_t>::iterator it = seekTracks.begin(); it != seekTracks.end(); it++){
      if (userSelect[*it]){
        userSelect[*it].setKeyNum(M.getKeyNumForTime(*it, pos));
      }
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
    if (meta.getLive() && meta.getNowms(tid) < pos){
      unsigned int maxTime = 0;
      while (meta.getNowms(tid) < pos && myConn && ++maxTime <= 20 && keepGoing()){
        Util::wait(500);
        stats();
      }
    }
    if (meta.getNowms(tid) < pos){
      WARN_MSG("Aborting seek to %" PRIu64 "ms in track %zu: past end of track (= %" PRIu64 "ms).",
               pos, tid, meta.getNowms(tid));
      userSelect.erase(tid);
      return false;
    }
    DTSC::Keys keys(M.getKeys(tid));
    if (M.getLive() && !pos && !buffer.getSyncMode()){
      uint64_t tmpTime = (M.getFirstms(tid) + M.getLastms(tid))/2;
      uint32_t tmpKey = M.getKeyNumForTime(tid, tmpTime);
      pos = keys.getTime(tmpKey);
    }
    uint32_t keyNum = M.getKeyNumForTime(tid, pos);
    if (keyNum == INVALID_KEY_NUM){
      FAIL_MSG("Attempted seek on empty track %zu", tid);
      return false;
    }
    uint64_t actualKeyTime = keys.getTime(keyNum);
    HIGH_MSG("Seeking to track %zu key %" PRIu32 " => time %" PRIu64, tid, keyNum, pos);
    emptyCount = 0;
    if (actualKeyTime > pos){
      pos = actualKeyTime;
      userSelect[tid].setKeyNum(keyNum);
    }
    if (M.hasEmbeddedFrames(tid)){
      Util::sortedPageInfo tmp;
      tmp.tid = tid;
      tmp.offset = 0;
      tmp.partIndex = keyNum;
      tmp.time = actualKeyTime;
      tmp.ghostPacket = false;
      buffer.insert(tmp);
      INFO_MSG("Sought to #%zu, T %" PRIu64, tmp.partIndex, tmp.time);
      return true;
    }
    loadPageForKey(tid, keyNum + (getNextKey ? 1 : 0));
    if (!curPage.count(tid) || !curPage[tid].mapped){
      //Sometimes the page load fails because of a connection loss to the user. This is fine.
      if (keepGoing()){
        WARN_MSG("Aborting seek to %" PRIu64 "ms in track %zu: not available.", pos, tid);
        userSelect.erase(tid);
      }
      return false;
    }
    Util::sortedPageInfo tmp;
    tmp.tid = tid;
    tmp.offset = 0;
    tmp.partIndex = 0;
    DTSC::Packet tmpPack;
    tmpPack.reInit(curPage[tid].mapped + tmp.offset, 0, true);
    tmp.time = tmpPack.getTime();
    char *mpd = curPage[tid].mapped;
    uint64_t nowMs = M.getNowms(tid);
    while (tmp.time < pos && tmpPack){
      tmp.offset += tmpPack.getDataLen();
      tmpPack.reInit(mpd + tmp.offset, 0, true);
      if (!tmpPack){
        nowMs = M.getNowms(tid);
        if (M.getLastms(tid) <= tmp.time && nowMs > tmp.time){
          // Okay, we're awaiting more data, let's insert a ghost packet instead.
          break;
        }
        uint64_t timeOut = Util::bootMS() + 10000;
        while (Util::bootMS() < timeOut && !tmpPack){
          Util::sleep(50);
          tmpPack.reInit(mpd + tmp.offset, 0, true);
        }
        if (!tmpPack){
          WARN_MSG("Aborting seek to %" PRIu64 "ms in track %zu: timeout", pos, tid);
          userSelect.erase(tid);
          return false;
        }
      }
      tmp.time = tmpPack.getTime();
    }
    if (tmpPack){
      tmp.ghostPacket = false;
      HIGH_MSG("Sought to time %" PRIu64 " in %s", tmp.time, curPage[tid].name.c_str());
      tmp.partIndex = M.getPartIndex(tmpPack.getTime(), tmp.tid);
      buffer.insert(tmp);
      return true;
    }
    tmp.partIndex = M.getPartIndex(nowMs, tmp.tid);
    tmp.ghostPacket = true;
    tmp.time = nowMs;
    buffer.insert(tmp);
    return true;
  }

  /// This function decides where in the stream initial playback starts.
  /// The default implementation calls seek(0) for VoD.
  /// For live, it seeks to the last sync'ed keyframe of the main track, no closer than
  /// needsLookAhead+minKeepAway ms from the end. Unless lastms < 5000, then it seeks to the first
  /// keyframe of the main track. Aborts if there is no main track or it has no keyframes.
  void Output::initialSeek(bool dryRun){
    if (!meta){return;}
    meta.removeLimiter();
    uint64_t seekPos = 0;
    if (meta.getLive() && buffer.getSyncMode()){
      size_t mainTrack = getMainSelectedTrack();
      if (mainTrack == INVALID_TRACK_ID){return;}
      DTSC::Keys keys(M.getKeys(mainTrack));
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
          if (meta.getNowms(ti->first) < seekPos + needsLookAhead){
            good = false;
            break;
          }
          if (mainTrack == ti->first){continue;}// skip self
          if (!M.trackValid(ti->first)){
            HIGH_MSG("Skipping track %zu, not in tracks", ti->first);
            continue;
          }// ignore missing tracks
          if (M.getNowms(ti->first) < seekPos + needsLookAhead + M.getMinKeepAway(ti->first)){
            good = false;
            break;
          }
          if (meta.getNowms(ti->first) == M.getFirstms(ti->first)){
            HIGH_MSG("Skipping track %zu, last equals first", ti->first);
            continue;
          }// ignore point-tracks
          if (meta.getNowms(ti->first) < seekPos){
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
      if (M.getLive()){
        MEDIUM_MSG("Stream currently contains data from %" PRIu64 " ms to %" PRIu64 " ms", startTime(), endTime());
      }
      // Overwrite recstart/recstop with recstartunix/recstopunix if set
      if (M.getLive() && (
          targetParams.count("recstartunix") || targetParams.count("recstopunix") ||
          targetParams.count("startunix") || targetParams.count("stopunix") ||
          targetParams.count("unixstart") || targetParams.count("unixstop")
      )){
        uint64_t zUTC = M.getUTCOffset();
        if (!zUTC){
          if (!M.getLive()){
            WARN_MSG("Attempting to set unix-based start/stop time for a VoD asset without known UTC timestamp! This will likely not work as you expect, since we have nothing to base the timestamps on");
          }else{
            zUTC = M.getBootMsOffset() + Util::getGlobalConfig("systemBoot").asInt();
          }
        }
        if (targetParams.count("recstartunix")){
          int64_t startUnix = atoll(targetParams["recstartunix"].c_str()) * 1000;
          // If the time is before the first 10 hours of unix epoch, assume relative time
          if (startUnix <= 36000000){startUnix += Util::unixMS();}
          targetParams["recstart"] = JSON::Value(startUnix - zUTC).asString();
        }
        if (targetParams.count("recstopunix")){
          int64_t stopUnix = atoll(targetParams["recstopunix"].c_str()) * 1000;
          // If the time is before the first 10 hours of unix epoch, assume relative time
          if (stopUnix <= 36000000){stopUnix += Util::unixMS();}
          targetParams["recstop"] = JSON::Value(stopUnix - zUTC).asString();
        }
        if (targetParams.count("unixstart")){
          int64_t startUnix = atoll(targetParams["unixstart"].c_str()) * 1000;
          // If the time is before the first 10 hours of unix epoch, assume relative time
          if (startUnix <= 36000000){startUnix += Util::unixMS();}
          targetParams["recstart"] = JSON::Value(startUnix - zUTC).asString();
        }
        if (targetParams.count("unixstop")){
          int64_t stopUnix = atoll(targetParams["unixstop"].c_str()) * 1000;
          // If the time is before the first 10 hours of unix epoch, assume relative time
          if (stopUnix <= 36000000){stopUnix += Util::unixMS();}
          targetParams["recstop"] = JSON::Value(stopUnix - zUTC).asString();
        }
        if (targetParams.count("startunix")){
          int64_t startUnix = atoll(targetParams["startunix"].c_str()) * 1000;
          // If the time is before the first 10 hours of unix epoch, assume relative time
          if (startUnix <= 36000000){startUnix += Util::unixMS();}
          targetParams["recstart"] = JSON::Value(startUnix - zUTC).asString();
        }
        if (targetParams.count("stopunix")){
          int64_t stopUnix = atoll(targetParams["stopunix"].c_str()) * 1000;
          // If the time is before the first 10 hours of unix epoch, assume relative time
          if (stopUnix <= 36000000){stopUnix += Util::unixMS();}
          targetParams["recstop"] = JSON::Value(stopUnix - zUTC).asString();
        }
      }
      //Autoconvert start/stop to recstart/recstop to improve usability
      if (targetParams.count("start") && !targetParams.count("recstart")){targetParams["recstart"] = targetParams["start"];}
      if (targetParams.count("stop") && !targetParams.count("recstop")){targetParams["recstop"] = targetParams["stop"];}
      // Check recstart/recstop for correctness
      if (targetParams.count("recstop")){
        uint64_t endRec = atoll(targetParams["recstop"].c_str());
        if (endRec < startTime()){
          onFail("Entire recording range is in the past", true);
          return;
        }
      }
      if (targetParams.count("recstart") && atoll(targetParams["recstart"].c_str()) != 0){
        int64_t startRec = atoll(targetParams["recstart"].c_str());
        if (startRec < 0){startRec = 0;}
        if (startRec > endTime()){
          if (!M.getLive()){
            onFail("Recording start past end of non-live source", true);
            return;
          }
        }
        if (startRec < startTime()){
          startRec = startTime();
          WARN_MSG("Record begin at %lld ms not available, starting at %" PRIu64
                   " ms instead", atoll(targetParams["recstart"].c_str()), startRec);
          targetParams["recstart"] = JSON::Value(startRec).asString();
        }
        size_t mainTrack = getMainSelectedTrack();
        if (mainTrack != INVALID_TRACK_ID && M.getType(mainTrack) == "video"){
          seekPos = M.getTimeForKeyIndex(mainTrack, M.getKeyIndexForTime(mainTrack, startRec));
          if (seekPos != startRec){
            INFO_MSG("Shifting recording start from %" PRIu64 " to %" PRIu64 " so that it starts with a keyframe", startRec, seekPos);
          }
        }else{
          seekPos = startRec;
        }
      }
      
      // Wait for the stream to catch up to the starttime
      uint64_t streamAvail = endTime();
      uint64_t lastUpdated = Util::getMS();
      if (seekPos > streamAvail){
        INFO_MSG("Waiting for stream to reach recording starting point. Recording will start in " PRETTY_PRINT_TIME, PRETTY_ARG_TIME((atoll(targetParams["recstart"].c_str()) - streamAvail) / 1000));
        while (Util::getMS() - lastUpdated < 10000 && seekPos > streamAvail && keepGoing()){
          Util::sleep(250);
          meta.reloadReplacedPagesIfNeeded();
          if (endTime() > streamAvail){
            stats();
            streamAvail = endTime();
            lastUpdated = Util::getMS();
          }
        }
      }
      {
        size_t mainTrack = getMainSelectedTrack();
        if (mainTrack != INVALID_TRACK_ID && M.getType(mainTrack) == "video"){
          uint64_t preSeekPos = seekPos;
          seekPos = M.getTimeForKeyIndex(mainTrack, M.getKeyIndexForTime(mainTrack, seekPos));
          if (seekPos != preSeekPos){
            INFO_MSG("Shifting recording start from %" PRIu64 " to %" PRIu64 " so that it starts with a keyframe", preSeekPos, seekPos);
          }
        }
      }
      if (targetParams.count("split")){
        long long endRec = atoll(targetParams["split"].c_str()) * 1000;
        INFO_MSG("Will split recording every %lld seconds", atoll(targetParams["split"].c_str()));
        targetParams["nxt-split"] = JSON::Value((int64_t)(seekPos + endRec)).asString();
      }
      // Duration to record in seconds. Overrides recstop.
      if (targetParams.count("duration")){
        int64_t endRec;
        endRec = seekPos + atoll(targetParams["duration"].c_str()) * 1000;
        targetParams["recstop"] = JSON::Value(endRec).asString();
        if (endRec < 0 || endRec < startTime()){
          onFail("Entire recording range is in the past", true);
          return;
        }
      }
      // Print calculated start and stop time
      INFO_MSG("Recording will start at timestamp %" PRIu64 " ms", seekPos); 
      if (targetParams.count("recstop")){
        INFO_MSG("Recording will stop at timestamp %llu ms", atoll(targetParams["recstop"].c_str()));
      }
      // If we have a stop position and it's within available range,
      // apply a limiter to the stream to make it appear like a VoD asset
      if (targetParams.count("recstop") || !M.getLive()){
        size_t mainTrack = getMainSelectedTrack();
        if (mainTrack != INVALID_TRACK_ID){
          uint64_t stopPos = M.getLastms(mainTrack);
          if (targetParams.count("recstop")){stopPos = atoll(targetParams["recstop"].c_str());}
          if (!M.getLive() || stopPos <= M.getLastms(mainTrack)){meta.applyLimiter(seekPos, stopPos);}
        }
      }
    }else{
      if (M.getLive() && targetParams.count("pushdelay")){
        INFO_MSG("Converting pushdelay syntax into corresponding recstart+realtime options");

        uint64_t delayTime = JSON::Value(targetParams["pushdelay"]).asInt()*1000; 
        if (endTime() - startTime() < delayTime){
          uint64_t waitTime = delayTime - (endTime() - startTime());
          uint64_t waitTarget = Util::bootMS() + waitTime;
          INFO_MSG("Waiting for buffer to fill up: waiting %" PRIu64 "ms", waitTime);
          while (Util::bootMS() < waitTarget && keepGoing()){
            Util::sleep(250);
            meta.reloadReplacedPagesIfNeeded();
            stats();
          }
          if (endTime() - startTime() < delayTime){
            WARN_MSG("Waited for %" PRIu64 "ms, but buffer still too small for a push delay of %" PRIu64 "ms. Doing the best we can.", waitTime, delayTime);
          }
        }
        if (endTime() < delayTime){
          INFO_MSG("Waiting for stream to reach playback starting point. Current last ms is '%" PRIu64 "'", endTime());
          while (endTime() < delayTime && keepGoing()){Util::wait(250);}
        }
        targetParams["start"] = JSON::Value(endTime() - delayTime).asString();
        targetParams["realtime"] = "1"; //force real-time speed
        maxSkipAhead = 1;
      }
      if (targetParams.count("startunix") || targetParams.count("stopunix")){
        uint64_t zUTC = M.getUTCOffset();
        if (!zUTC){
          if (!M.getLive()){
            WARN_MSG("Attempting to set unix-based start/stop time for a VoD asset without known UTC timestamp! This will likely not work as you expect, since we have nothing to base the timestamps on");
          }else{
            zUTC = M.getBootMsOffset() + Util::getGlobalConfig("systemBoot").asInt();
          }
        }
        if (targetParams.count("startunix")){
          int64_t startUnix = atoll(targetParams["startunix"].c_str()) * 1000;
          // If the time is before the first 10 hours of unix epoch, assume relative time
          if (startUnix <= 36000000){startUnix += Util::unixMS();}
          targetParams["start"] = JSON::Value(startUnix - zUTC).asString();
        }
        if (targetParams.count("stopunix")){
          int64_t stopUnix = atoll(targetParams["stopunix"].c_str()) * 1000;
          // If the time is before the first 10 hours of unix epoch, assume relative time
          if (stopUnix <= 36000000){stopUnix += Util::unixMS();}
          targetParams["stop"] = JSON::Value(stopUnix - zUTC).asString();
        }
      }
      if (targetParams.count("start") && atoll(targetParams["start"].c_str()) != 0){
        size_t mainTrack = getMainSelectedTrack();
        if (mainTrack != INVALID_TRACK_ID){
          int64_t startRec = atoll(targetParams["start"].c_str());
          if (startRec > M.getNowms(mainTrack)){
            if (!M.getLive()){
              onFail("Playback start past end of non-live source", true);
              return;
            }
            int64_t streamAvail = M.getNowms(mainTrack);
            int64_t lastUpdated = Util::getMS();
            INFO_MSG("Waiting for stream to reach playback starting point (%" PRIu64 " -> %" PRIu64 "). Time left: " PRETTY_PRINT_MSTIME, startRec, streamAvail, PRETTY_ARG_MSTIME(startRec - streamAvail));
            while (Util::getMS() - lastUpdated < 5000 && startRec > streamAvail && keepGoing()){
              Util::sleep(500);
              if (M.getNowms(mainTrack) > streamAvail){
                HIGH_MSG("Waiting for stream to reach playback starting point (%" PRIu64 " -> %" PRIu64 "). Time left: " PRETTY_PRINT_MSTIME, startRec, streamAvail, PRETTY_ARG_MSTIME(startRec - streamAvail));
                stats();
                streamAvail = M.getNowms(mainTrack);
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
          if (M.getType(mainTrack) == "video"){
            seekPos = M.getTimeForKeyIndex(mainTrack, M.getKeyIndexForTime(mainTrack, startRec));
            if (seekPos != startRec){
              INFO_MSG("Shifting recording start from %" PRIu64 " to %" PRIu64 " so that it starts with a keyframe", startRec, seekPos);
            }
          }else{
            seekPos = startRec;
          }
          INFO_MSG("Playback will start at %" PRIu64, seekPos);
        }
      }
      // Duration to record in seconds. Overrides stop.
      if (targetParams.count("duration")){
        int64_t endRec;
        if (targetParams.count("start")){
          endRec = atoll(targetParams["start"].c_str()) + atoll(targetParams["duration"].c_str()) * 1000;
        }else{
          endRec = seekPos + atoll(targetParams["duration"].c_str()) * 1000;
        }
        targetParams["stop"] = JSON::Value(endRec).asString();
      }
      if (targetParams.count("stop")){
        int64_t endRec = atoll(targetParams["stop"].c_str());
        if (endRec < 0 || endRec < startTime()){
          onFail("Entire range is in the past", true);
          return;
        }
        INFO_MSG("Playback will stop at %" PRIu64, endRec);
      }
      // If we have a stop position and it's within available range,
      // apply a limiter to the stream to make it appear like a VoD asset
      if (targetParams.count("stop") || !M.getLive()){
        size_t mainTrack = getMainSelectedTrack();
        if (mainTrack != INVALID_TRACK_ID){
          uint64_t stopPos = M.getLastms(mainTrack);
          if (targetParams.count("stop")){stopPos = atoll(targetParams["stop"].c_str());}
          if (!M.getLive() || stopPos <= M.getLastms(mainTrack)){
            meta.applyLimiter(seekPos, stopPos);
          }else{
            // End point past end of track? Don't limit the end point.
            meta.applyLimiter(seekPos, 0xFFFFFFFFFFFFFFFFull);
          }
        }
      }else{
        // No stop point, only apply limiter if a start point is set, and never limit the end point.
        if (targetParams.count("start")){
          meta.applyLimiter(seekPos, 0xFFFFFFFFFFFFFFFFull);
        }
      }
    }
    if (dryRun){return;}
    /*LTS-END*/
    if (!keepGoing()){
      ERROR_MSG("Aborting seek to %" PRIu64 " since the stream is no longer active", seekPos);
      return;
    }
    if (endTime() >= atoll(targetParams["recstart"].c_str())) {
      MEDIUM_MSG("Initial seek to %" PRIu64 "ms", seekPos);
      seek(seekPos);
    }else{
      ERROR_MSG("Aborting seek to %" PRIu64 " since stream only has available from %" PRIu64 " ms to %" PRIu64 " ms", seekPos, startTime(), endTime());
    }
  }

  /// Returns the highest getMinKeepAway of all selected tracks
  uint64_t Output::getMinKeepAway(){
    uint64_t r = 0;
    for (std::map<size_t, Comms::Users>::iterator ti = userSelect.begin(); ti != userSelect.end(); ++ti){
      if (ti->first == INVALID_TRACK_ID){continue;}
      if (M.getMinKeepAway(ti->first) > r){r = M.getMinKeepAway(ti->first);}
    }
    //Limit the value to the maxKeepAway setting
    uint64_t maxKeepAway = M.getMaxKeepAway();
    if (maxKeepAway){
      if (r > maxKeepAway){r = maxKeepAway;}
    }
    return r;
  }

  /// This function attempts to forward playback in live streams to a more live point.
  /// It seeks to the last sync'ed keyframe of the main track, no closer than needsLookAhead+minKeepAway ms from the end.
  /// Aborts if not live, there is no main track or it has no keyframes.
  bool Output::liveSeek(bool rateOnly){
    if (!realTime){return false;}//Makes no sense when playing in turbo mode
    if (targetParams.count("start")){return false;} //disable seeking forward if start is given
    if (targetParams.count("recstart")){return false;} //disable seeking forward for pushes with start time
    if (maxSkipAhead == 1){return false;}//A skipAhead of 1 signifies disabling the skipping/rate control system entirely.
    if (!meta.getLive()){return false;}
    uint64_t seekPos = 0;
    size_t mainTrack = getMainSelectedTrack();
    if (mainTrack == INVALID_TRACK_ID){return false;}
    uint64_t lMs = meta.getLastms(mainTrack);
    uint64_t cTime = thisPacket.getTime();
    uint64_t mKa = getMinKeepAway();
    if (!maxSkipAhead){
      bool noReturn = false;
      uint64_t newSpeed = 1000;
      if (lMs - mKa - needsLookAhead > cTime + 50){
        // We need to speed up!
        uint64_t diff = (lMs - mKa - needsLookAhead) - cTime;
        if (!rateOnly && diff > 3000){
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
        HIGH_MSG("Changing playback speed from %" PRIu64 " to %" PRIu64 "(%" PRIu64 " ms LA, %" PRIu64 " ms mKA)", realTime, newSpeed, needsLookAhead, mKa);
        firstTime = Util::bootMS() - (cTime * newSpeed / 1000);
        realTime = newSpeed;
      }
      if (!noReturn){return false;}
    }
    // cancel if there are no keys in the main track
    if (mainTrack == INVALID_TRACK_ID){return false;}
    DTSC::Keys mainKeys(meta.getKeys(mainTrack));
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
        if (meta.getLastms(ti->first) < seekPos + needsLookAhead + mKa){
          good = false;
          break;
        }
        if (mainTrack == ti->first){continue;}// skip self
        if (meta.getLastms(ti->first) == meta.getFirstms(ti->first)){
          HIGH_MSG("Skipping track %zu, last equals first", ti->first);
          continue;
        }// ignore point-tracks
        HIGH_MSG("Track %zu is good", ti->first);
      }
      // if yes, seek here
      if (good){
        HIGH_MSG("Skipping forward %" PRIu64 "ms (%" PRIu64 " ms LA, %" PRIu64
                 " ms mKA, > %" PRIu32 "ms, mSa %" PRIu64 " ms)",
                 seekPos - cTime, needsLookAhead, mKa, seekCount * 100, maxSkipAhead);
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
    if (realTime && M.getLive() && buffer.getSyncMode()){
      firstTime += millis;
    }
    Util::wait(millis);
  }

  /// Called right before sendNext(). Should return true if this is a stopping point.
  bool Output::reachedPlannedStop(){
    // If we're recording to file and reached the target position, stop
    if (isRecordingToFile && targetParams.count("recstop") &&
        atoll(targetParams["recstop"].c_str()) <= thisTime){
      INFO_MSG("End of planned recording reached");
      return true;
    }
    // Regardless of playback method, if we've reached the wanted stop point, stop
    if (targetParams.count("stop") && atoll(targetParams["stop"].c_str()) <= thisTime){
      INFO_MSG("End of planned playback reached");
      return true;
    }
    // check if we need to split here
    if (inlineRestartCapable() && targetParams.count("split")){
      // Make sure that inlineRestartCapable outputs with splitting enabled only stop right before
      // keyframes This works because this function is executed right BEFORE sendNext(), causing
      // thisPacket to be the next packet in the newly splitted file.
      if (thisIdx != getMainSelectedTrack() || (!thisPacket.getFlag("keyframe") && M.getType(thisIdx) == "video")){
        return false;
      }
      // If splitting would result in a tiny segment at the end, do not split
      if (!M.getLive() && (endTime() - lastPacketTime) < (atoll(targetParams["split"].c_str()) * 500)){
        return false;
      }
      // is this a split point?
      if (targetParams.count("nxt-split") && atoll(targetParams["nxt-split"].c_str()) <= thisTime){
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
    // Variables used for segmenting the output
    uint64_t segmentCount = 0;
    uint64_t segmentsRemoved = 0;
    HTTP::URL playlistLocation;
    std::string playlistLocationString;
    std::string playlistBuffer;
    std::string currentTarget;
    uint64_t currentStartTime = 0;
    uint64_t maxEntries = 0;
    uint64_t targetAge = 0;
    std::string targetDuration;
    bool reInitPlaylist = false;
    bool autoAdjustSplit = false;
    Socket::Connection plsConn;
    uint64_t systemBoot;
    bool addEndlist = true;

    std::string origTarget; 
    const char* origTargetPtr = getenv("MST_ORIG_TARGET");
    if (origTargetPtr){
      origTarget = origTargetPtr;
      if (origTarget.rfind('?') != std::string::npos){
        std::map<std::string, std::string> tmpParams;
        HTTP::parseVars(origTarget.substr(origTarget.rfind('?') + 1), tmpParams);
        origTarget.erase(origTarget.rfind('?'));
        if (tmpParams.count("m3u8")){
          targetParams["m3u8"] = tmpParams["m3u8"];
        }
        if (tmpParams.count("segment")){
          targetParams["segment"] = tmpParams["segment"];
        }
      }
    }else if (config->hasOption("target")){
      origTarget = config->getString("target");
    }
    // Check if the target segment contains any of the required variables
    if (targetParams.count("m3u8")){
      std::string tmpTarget;
      if (targetParams.count("segment")){
        tmpTarget = targetParams["segment"];
      }else{
        tmpTarget = origTarget;
      }
      if (tmpTarget.find("$currentMediaTime") == std::string::npos && tmpTarget.find("$segmentCounter") == std::string::npos){
        FAIL_MSG("Target segmented output does not contain a currentMediaTime or segmentCounter: %s", tmpTarget.c_str());
        Util::logExitReason("Target segmented output does not contain a currentMediaTime or segmentCounter: %s", tmpTarget.c_str());
        return 1;
      }
    }
    if (targetParams.count("maxEntries")){
      maxEntries = atoll(targetParams["maxEntries"].c_str());
    }
    if (targetParams.count("targetAge")){
      targetAge = atoll(targetParams["targetAge"].c_str());
    }
    if (targetParams.count("adjustSplit")){
      autoAdjustSplit = true;
    }
    if (targetParams.count("noendlist")){
      addEndlist = false;
    }
    // When segmenting to a playlist, handle any existing files and init some data
    if (targetParams.count("m3u8")){
      // Load system boot time from the global config
      systemBoot = Util::getGlobalConfig("systemBoot").asInt();
      // fall back to local calculation if loading from global config fails
      if (!systemBoot){systemBoot = (Util::unixMS() - Util::bootMS());}
      // Create a new or connect to an existing playlist file
      if (!plsConn){
        std::string plsRel = targetParams["m3u8"];
        Util::streamVariables(plsRel, streamName);
        playlistLocation = HTTP::localURIResolver().link(config->getString("target")).link(plsRel);
        if (playlistLocation.isLocalPath()){
          playlistLocationString = playlistLocation.getFilePath();
          INFO_MSG("Segmenting to local playlist '%s'", playlistLocationString.c_str());
          // Check if we already have a playlist at the target location
          std::ifstream inFile(playlistLocationString.c_str());
          if (inFile.good()){
            std::string line;
            // If appending, remove endlist and count segments
            if (targetParams.count("append")){
              while (std::getline(inFile, line)) {
                if (strncmp("#EXTINF", line.c_str(), 7) == 0){
                  segmentCount++;
                }else if (strcmp("#EXT-X-ENDLIST", line.c_str()) == 0){
                  INFO_MSG("Stripping line `#EXT-X-ENDLIST`");
                  continue;
                }
                playlistBuffer += line + '\n';
              }
              playlistBuffer += "#EXT-X-DISCONTINUITY\n";
              INFO_MSG("Appending to existing local playlist file '%s'", playlistLocationString.c_str());
              INFO_MSG("Found %" PRIu64 " prior segments", segmentCount);
            }else{
              // Remove all segments referenced in the playlist
              while (std::getline(inFile, line)) {
                if (line[0] == '#'){
                  continue;
                }else{
                  std::string segPath = playlistLocation.link(line).getFilePath();
                  if(unlink(segPath.c_str())){
                    FAIL_MSG("Failed to remove segment at '%s'. Error: '%s'", segPath.c_str(), strerror(errno));
                  }else{
                    INFO_MSG("Removed segment at '%s'", segPath.c_str());
                  }
                }
              }
              INFO_MSG("Overwriting existing local playlist file '%s'", playlistLocationString.c_str());
              reInitPlaylist = true;
            }
          }else{
            INFO_MSG("Creating new local playlist file '%s'", playlistLocationString.c_str());
            reInitPlaylist = true;
          }
          config->getOption("target", true).append(playlistLocationString);
        }else{
          playlistLocationString = playlistLocation.getUrl();
          // Disable sliding window playlists, as the current external writer 
          // implementation requires us to keep a single connection to the playlist open
          maxEntries = 0;
          targetAge = 0;
          // Check if there is an existing playlist at the target location
          HTTP::URIReader outFile(playlistLocationString);
          if (outFile){
            // If so, init the buffer with remote data
            if (targetParams.count("append")){
              char *dataPtr;
              size_t dataLen;
              outFile.readAll(dataPtr, dataLen);
              std::string existingBuffer(dataPtr, dataLen);
              std::istringstream inFile(existingBuffer);
              std::string line;
              while (std::getline(inFile, line)) {
                if (strncmp("#EXTINF", line.c_str(), 7) == 0){
                  segmentCount++;
                }else if (strcmp("#EXT-X-ENDLIST", line.c_str()) == 0){
                  INFO_MSG("Stripping line `#EXT-X-ENDLIST`");
                  continue;
                }
                playlistBuffer += line + '\n';
              }
              playlistBuffer += "#EXT-X-DISCONTINUITY\n";
              INFO_MSG("Found %" PRIu64 " prior segments", segmentCount);
              INFO_MSG("Appending to existing remote playlist file '%s'", playlistLocationString.c_str());
            }else{
              WARN_MSG("Overwriting existing remote playlist file '%s'", playlistLocationString.c_str());
              reInitPlaylist = true;
            }
          }else{
            INFO_MSG("Creating new remote playlist file '%s'", playlistLocationString.c_str());
            reInitPlaylist = true;
          }
        }
      }
      // By default split into a new segment after 60 seconds
      if (!targetParams.count("split")){
        targetParams["split"] = "60";
      }
      targetDuration = targetParams["split"];
    }
    Comms::sessionConfigCache();
    /*LTS-START*/
    // Connect to file target, if needed
    if (isFileTarget()){
      isRecordingToFile = true;
      if (!streamName.size()){
        WARN_MSG("Recording unconnected %s output to file! Cancelled.", capa["name"].asString().c_str());
        onFail("Unconnected recording output", true);
        recEndTrigger();
        return 2;
      }
      initialize();
      if (!M.getValidTracks().size() || !userSelect.size() || !keepGoing()){
        INFO_MSG("Stream not available - aborting");
        onFail("Stream not available for recording", true);
        recEndTrigger();
        return 3;
      }
      initialSeek();
      // Initialises the playlist if we are segmenting the output with a playlist
      if (targetParams.count("m3u8")){
        if (reInitPlaylist){
          uint64_t unixMs = M.packetTimeToUnixMs(currentStartTime, systemBoot);
          reinitPlaylist(playlistBuffer, targetAge, maxEntries, segmentCount, segmentsRemoved, unixMs, targetDuration, playlistLocation);
        }
        // Do not open the playlist just yet if this is a non-live source
        if (M.getLive()){
          connectToFile(playlistLocationString, false, &plsConn);
          // Write initial contents to the playlist file
          if (!plsConn){
            FAIL_MSG("Failed to open a connection to playlist file `%s` for segmenting", playlistLocationString.c_str());
            Util::logExitReason("Failed to open a connection to playlist file `%s`  for segmenting", playlistLocationString.c_str());
            return 1;
          }else if (playlistBuffer.size()){
            // Do not write to the playlist intermediately if we are outputting a VOD playlist
            plsConn.SendNow(playlistBuffer);
            // Clear the buffer if we will only be appending lines instead of overwriting the entire playlist file
            if (!maxEntries && !targetAge) {playlistBuffer = "";}
          }
        }
      }
      currentStartTime = currentTime();
      std::string newTarget = origTarget;
      Util::replace(newTarget, "$currentMediaTime", JSON::Value(currentStartTime).asString());
      Util::replace(newTarget, "$segmentCounter", JSON::Value(segmentCount).asString());
      Util::streamVariables(newTarget, streamName);
      currentTarget = newTarget;
      if (newTarget == "-"){
        INFO_MSG("Outputting %s to stdout with %s format", streamName.c_str(),
                 capa["name"].asString().c_str());
      }else{
        if (!connectToFile(newTarget, targetParams.count("append"))){
          onFail("Could not connect to the target for recording", true);
          recEndTrigger();
          return 3;
        }
        INFO_MSG("Recording %s to %s with %s format", streamName.c_str(),
                 newTarget.c_str(), capa["name"].asString().c_str());
      }
      parseData = true;
      wantRequest = false;
      if (!targetParams.count("realtime")){
        realTime = 0;
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
      thisBootMs = Util::bootMS();
      Comms::sessionConfigCache(thisBootMs);
      if (wantRequest){requestHandler();}
      if (parseData){
        if (!isInitialized){
          initialize();
          if (!isInitialized){
            onFail("Stream initialization failed");
            break;
          }
        }
        if (!sought){initialSeek();}
        if (!sentHeader && keepGoing()){
          DONTEVEN_MSG("sendHeader");
          sendHeader();
        }
        if (prepareNext()){
          if (thisPacket){
            lastPacketTime = thisTime;
            if (firstPacketTime == 0xFFFFFFFFFFFFFFFFull){
              firstPacketTime = lastPacketTime;
            }

            // slow down processing, if real time speed is wanted
            if (realTime && buffer.getSyncMode()){
              uint64_t amount = thisTime - targetTime();
              size_t i = (amount / 1000) + 6;
              while (--i && thisTime > targetTime() &&
                     keepGoing()){
                amount = thisTime - targetTime();
                if (amount > 1000){amount = 1000;}
                idleTime(amount);
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
              uint32_t sleepTime = std::min((uint64_t)20, needsLookAhead);
              // wait at most double the look ahead time, plus ten seconds
              uint64_t timeoutTries = (needsLookAhead / sleepTime) * 2 + (10000 / sleepTime);
              uint64_t needsTime = thisTime + needsLookAhead;
              bool firstLookahead = true;
              while (--timeoutTries && keepGoing()){
                bool lookReady = true;
                for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin();
                     it != userSelect.end(); it++){
                  if (meta.getNowms(it->first) <= needsTime){
                    if (timeoutTries == 1){
                      WARN_MSG("Track %zu: %" PRIu64 " <= %" PRIu64, it->first,
                               meta.getNowms(it->first), needsTime);
                    }
                    lookReady = false;
                    break;
                  }
                }
                if (lookReady){break;}
                if (firstLookahead){
                  firstLookahead = false;
                }else{
                  playbackSleep(sleepTime);
                }
                //Make sure we stay responsive to requests and stats while waiting
                if (wantRequest){requestHandler();}
                stats();
                meta.reloadReplacedPagesIfNeeded();
              }
              if (!timeoutTries){
                WARN_MSG("Waiting for lookahead (%" PRIu64 "ms in %zu tracks) timed out - resetting lookahead!", needsLookAhead, userSelect.size());
                needsLookAhead = 0;
              }
            }

            if (reachedPlannedStop()){
              targetParams.erase("nxt-split");
              if (inlineRestartCapable() && !reachedPlannedStop()){
                // Write the segment to the playlist if applicable
                if (targetParams.count("m3u8")){
                  // We require an active connection to the playlist
                  // except for VOD, where we connect and write at the end of segmenting
                  if (!plsConn && M.getLive()){
                    FAIL_MSG("Lost connection to playlist file `%s` during segmenting", playlistLocationString.c_str());
                    Util::logExitReason("Lost connection to playlist file `%s` during segmenting", playlistLocationString.c_str());
                    break;
                  }
                  std::string segment = HTTP::localURIResolver().link(currentTarget).getLinkFrom(playlistLocation);
                  {
                    uint64_t unixMs = M.packetTimeToUnixMs(currentStartTime, systemBoot);
                    if (unixMs){
                      INFO_MSG("Adding segment #%" PRIu64 " @ %" PRIu64 " => %s", segmentCount, currentStartTime, Util::getUTCStringMillis(unixMs).c_str());
                      playlistBuffer += "#EXT-X-PROGRAM-DATE-TIME:" + Util::getUTCStringMillis(unixMs) + "\n";
                    }
                  }
                  INFO_MSG("Adding new segment `%s` of %" PRIu64 "ms to playlist '%s'", segment.c_str(), lastPacketTime - currentStartTime, playlistLocationString.c_str());
                  // Append duration & TS filename to playlist file
                  std::stringstream tmp;
                  double segmentDuration = (lastPacketTime - currentStartTime) / 1000.0;
                  tmp << "#EXTINF:" << std::fixed << std::setprecision(3) << segmentDuration <<  ",\n"+ segment + "\n";
                  playlistBuffer += tmp.str();
                  // Adjust split time up to half a segment duration
                  if (autoAdjustSplit && (segmentDuration / 2) > atoll(targetParams["split"].c_str())){
                    targetParams["split"] = JSON::Value(segmentDuration / 2).asString();
                  }
                  // Always adjust the targetDuration in the playlist upwards
                  if (segmentDuration > JSON::Value(targetDuration).asDouble()){
                    // Set the new targetDuration to the ceil of the segment duration
                    WARN_MSG("Segment #%" PRIu64 " is longer than the target duration. Adjusting the targetDuration from %s to %f s", segmentCount, targetDuration.c_str(), segmentDuration);
                    targetDuration = JSON::Value((uint64_t)segmentDuration + 1).asString();
                    // Modify the buffer to contain the new targetDuration
                    if (!M.getLive()){
                      uint64_t unixMs = M.packetTimeToUnixMs(currentStartTime, systemBoot);
                      reinitPlaylist(playlistBuffer, targetAge, maxEntries, segmentCount, segmentsRemoved, unixMs, targetDuration, playlistLocation);
                    }else if (!maxEntries && !targetAge && playlistLocation.isLocalPath()){
                      // If we are appending to an existing playlist, we need to recover the playlistBuffer and reopen the playlist
                      HTTP::URIReader inFile(playlistLocationString);
                      char *newBuffer;
                      size_t bytesRead;
                      inFile.readAll(newBuffer, bytesRead);
                      playlistBuffer = std::string(newBuffer, bytesRead) + playlistBuffer;
                      // Reinit the playlist with the new targetDuration
                      uint64_t unixMs = M.packetTimeToUnixMs(currentStartTime, systemBoot);
                      reinitPlaylist(playlistBuffer, targetAge, maxEntries, segmentCount, segmentsRemoved, unixMs, targetDuration, playlistLocation);
                      connectToFile(playlistLocationString, false, &plsConn);
                    }
                    // Else we are in a sliding window playlist, so it will automatically get overwritten
                  }
                  // Remove older entries in the playlist
                  if (maxEntries || targetAge){
                    uint64_t unixMs = M.packetTimeToUnixMs(currentStartTime, systemBoot);
                    reinitPlaylist(playlistBuffer, targetAge, maxEntries, segmentCount, segmentsRemoved, unixMs, targetDuration, playlistLocation);
                  }
                  // Do not write to the playlist intermediately if we are outputting a VOD playlist
                  if (M.getLive()){
                    // Clear the buffer if we will only be appending lines instead of overwriting the entire playlist file
                    if (!maxEntries && !targetAge) {
                      plsConn.SendNow(playlistBuffer);
                      playlistBuffer = "";
                    // Else re-open the file to force an overwrite
                    }else if(connectToFile(playlistLocationString, false, &plsConn)){
                      plsConn.SendNow(playlistBuffer);
                    }
                  }
                }

                // Keep track of filenames written, so that they can be added to the playlist file
                std::string newTarget;
                if (targetParams.count("segment")){
                  HTTP::URL targetUrl = HTTP::URL(config->getString("target")).link(targetParams["segment"]);
                  if (targetUrl.isLocalPath()){
                    newTarget = targetUrl.getFilePath();
                  }else{
                    newTarget = targetUrl.getUrl();
                  }
                }else{
                  newTarget = origTarget;
                }
                currentStartTime = lastPacketTime;
                segmentCount++;
                Util::replace(newTarget, "$currentMediaTime", JSON::Value(currentStartTime).asString());
                Util::replace(newTarget, "$segmentCounter", JSON::Value(segmentCount).asString());
                Util::streamVariables(newTarget, streamName);
                if (newTarget.rfind('?') != std::string::npos){
                  newTarget.erase(newTarget.rfind('?'));
                }
                currentTarget = newTarget;
                INFO_MSG("Switching to next push target filename: %s", newTarget.c_str());
                if (!connectToFile(newTarget)){
                  FAIL_MSG("Failed to open file, aborting: %s", newTarget.c_str());
                  Util::logExitReason(ER_WRITE_FAILURE, "failed to open file, aborting: %s", newTarget.c_str());
                  onFinish();
                  break;
                }
                uint64_t endRec = thisTime + atoll(targetParams["split"].c_str()) * 1000;
                targetParams["nxt-split"] = JSON::Value(endRec).asString();
                sentHeader = false;
                sendHeader();
              }else{
                if (!onFinish()){
                  INFO_MSG("Shutting down because planned stopping point reached");
                  Util::logExitReason(ER_CLEAN_INTENDED_STOP, "planned stopping point reached");
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
              Util::logExitReason(ER_CLEAN_EOF, "end of stream");
              break;
            }
          }
        }
        if (!meta){
          Util::logExitReason(ER_SHM_LOST, "lost internal connection to stream data");
          break;
        }
      }
      stats();
    }
    if (!config->is_active){Util::logExitReason(ER_UNKNOWN, "set inactive");}
    if (!myConn){Util::logExitReason(ER_CLEAN_REMOTE_CLOSE, "connection closed");}
    if (strncmp(Util::exitReason, "connection closed", 17) == 0){
      MEDIUM_MSG("Client handler shutting down, exit reason: %s", Util::exitReason);
    }else{
      INFO_MSG("Client handler shutting down, exit reason: %s", Util::exitReason);
    }
    onFinish();
    // Write last segment
    if (targetParams.count("m3u8") && (firstPacketTime != 0xFFFFFFFFFFFFFFFFull) && (lastPacketTime - firstPacketTime > 0)){
      // If this is a non-live source, we can finally open up the connection to the playlist file
      if (!M.getLive()){connectToFile(playlistLocationString, false, &plsConn);}
      if (plsConn){

        if (lastPacketTime - currentStartTime > 0){
          std::string segment = HTTP::localURIResolver().link(currentTarget).getLinkFrom(playlistLocation);
          INFO_MSG("Adding final segment `%s` of %" PRIu64 "ms to playlist '%s'", segment.c_str(), lastPacketTime - currentStartTime, playlistLocationString.c_str());
          {
            uint64_t unixMs = M.packetTimeToUnixMs(currentStartTime, systemBoot);
            if (unixMs){playlistBuffer += "#EXT-X-PROGRAM-DATE-TIME:" + Util::getUTCStringMillis(unixMs) + "\n";}
          }
          // Append duration & TS filename to playlist file
          std::stringstream tmp;
          tmp << "#EXTINF:" << std::fixed << std::setprecision(3) << (lastPacketTime - currentStartTime) / 1000.0 <<  ",\n"+ segment + "\n";
          playlistBuffer += tmp.str();
        }
        // Omit the ENDLIST tag for sliding window live playlists (or when explicitly requested to omit it)
        if ((!M.getLive() || (!maxEntries && !targetAge)) && addEndlist){playlistBuffer += "#EXT-X-ENDLIST\n";}
        // Remove older entries in the playlist
        if (maxEntries || targetAge){
          uint64_t unixMs = M.packetTimeToUnixMs(currentStartTime, systemBoot);
          reinitPlaylist(playlistBuffer, targetAge, maxEntries, segmentCount, segmentsRemoved, unixMs, targetDuration, playlistLocation);
        }
        // Append the final contents to the playlist
        if (!maxEntries && !targetAge) {
          plsConn.SendNow(playlistBuffer);
        // Else re-open the file to force an overwrite
        }else if(connectToFile(playlistLocationString, false, &plsConn)){
          plsConn.SendNow(playlistBuffer);
        }
        playlistBuffer.clear();
      }else{
        FAIL_MSG("Lost connection to the playlist file `%s` during segmenting", playlistLocationString.c_str());
        Util::logExitReason("Lost connection to the playlist file `%s` during segmenting", playlistLocationString.c_str());
      }
    }

    /*LTS-START*/
    if (Triggers::shouldTrigger("CONN_CLOSE", streamName)){
      std::string payload =
          streamName + "\n" + getConnectedHost() + "\n" + capa["name"].asStringRef() + "\n" + reqUrl;
      Triggers::doTrigger("CONN_CLOSE", payload, streamName);
    }
    if (isRecordingToFile){
      recEndTrigger();
    }
    outputEndTrigger();
    /*LTS-END*/

    disconnect();
    stats(true);
    userSelect.clear();
    myConn.close();
    return 0;
  }

  void Output::dropTrack(size_t trackId, const std::string &reason, bool probablyBad){
    //We can drop from the buffer without any checks, it's a no-op if no entry exists
    buffer.dropTrack(trackId);
    // depending on whether this is probably bad and the current debug level, print a message
    size_t printLevel = (probablyBad ? DLVL_WARN : DLVL_INFO);
    //The rest of the operations depends on userSelect, so we ignore it if it doesn't exist.
    if (!userSelect.count(trackId)){
      if (!M || !M.getValidTracks().count(trackId)){
        DEBUG_MSG(printLevel, "Dropping invalid track %zu: %s", trackId, reason.c_str());
      }else if (M.hasEmbeddedFrames(trackId)){
        DEBUG_MSG(printLevel, "Dropping %s track %zu (raw): %s", meta.getCodec(trackId).c_str(), trackId, reason.c_str());
      }else{
        DEBUG_MSG(printLevel, "Dropping %s track %zu (lastP=%" PRIu64 "): %s",
                  meta.getCodec(trackId).c_str(), trackId, pageNumMax(trackId), reason.c_str());
      }
      return;
    }
    const Comms::Users &usr = userSelect.at(trackId);
    if (M.hasEmbeddedFrames(trackId)){
      DEBUG_MSG(printLevel, "Dropping %s track %zu (raw): %s", meta.getCodec(trackId).c_str(), trackId, reason.c_str());
    }else{
      if (!usr){
        if (!M || !M.getValidTracks().count(trackId)){
          DEBUG_MSG(printLevel, "Dropping invalid track %zu: %s", trackId, reason.c_str());
        }else{
          DEBUG_MSG(printLevel, "Dropping %s track %zu (lastP=%" PRIu64 "): %s",
                    meta.getCodec(trackId).c_str(), trackId, pageNumMax(trackId), reason.c_str());
        }
      }else{
        if (!M || !M.getValidTracks().count(trackId)){
          DEBUG_MSG(printLevel, "Dropping invalid track %zu: %s", trackId, reason.c_str());
        }else{
          DEBUG_MSG(printLevel, "Dropping %s track %zu@k%zu (nextP=%" PRIu64 ", lastP=%" PRIu64 "): %s",
                    meta.getCodec(trackId).c_str(), trackId, usr.getKeyNum() + 1,
                    pageNumForKey(trackId, usr.getKeyNum() + 1), pageNumMax(trackId), reason.c_str());
        }
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
    Util::packetSorter tmp_buffer = buffer;
    std::map<size_t, Comms::Users> tmp_userSelect = userSelect;
    std::map<size_t, uint32_t> tmp_currentPage = currentPage;

    // reset the current packet to null, assuming failure
    thisPacket.null();

    // find the main track, check if it is video. Abort if not.
    size_t mainTrack = getMainSelectedTrack();
    if (mainTrack == INVALID_TRACK_ID || M.getType(mainTrack) != "video"){return false;}

    // we now know that mainTrack is a video track - let's do some work!
    // first, we remove all selected tracks and the buffer. Then we select only the main track.
    uint64_t currTime = currentTime();
    buffer.clear();
    userSelect.clear();
    userSelect[mainTrack].reload(streamName, mainTrack);
    // now, seek to the exact timestamp of the keyframe
    DTSC::Keys keys(M.getKeys(mainTrack));
    bool ret = false;
    if (!keys.getValidCount()){
      FAIL_MSG("No keyframes available on track %zu", mainTrack);
    }else{
      seek(keys.getTime(keys.getIndexForTime(currTime)));
      // attempt to load the key into thisPacket
      ret = prepareNext();
      if (!ret){
        WARN_MSG("Failed to load keyframe for %" PRIu64 "ms - continuing without it", currTime);
      }
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
          if (!buffer.hasEntry(it->first)){dropTracks.insert(it->first);}
        }
      }else{
        // prepare to drop any buffer entry without selectedTrack
        buffer.getTrackList(dropTracks);
        for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); ++it){
          dropTracks.erase(it->first);
        }
      }
      if (!dropTracks.size()){
        FAIL_MSG("Could not equalize tracks! This is very very very bad and I am now going to shut down to prevent worse.");
        Util::logExitReason(ER_INTERNAL_ERROR, "Could not equalize tracks");
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

    Util::sortedPageInfo nxt;

    uint64_t nextTime;
    size_t trackTries = 0;
    //In case we're not in sync mode, we might have to retry a few times
    for (; trackTries < buffer.size(); ++trackTries){

      nxt = *(buffer.begin());

      if (meta.reloadReplacedPagesIfNeeded()){return false;}
      if (!M.trackLoaded(nxt.tid)){
        dropTrack(nxt.tid, "disappeared from metadata");
        return false;
      }

      if (M.hasEmbeddedFrames(nxt.tid)){
        if (nxt.ghostPacket){
          if (M.getEmbeddedTime(nxt.tid, nxt.partIndex, nxt.time)){
            nxt.ghostPacket = false;
          }else{
            nxt.time = M.getNowms(nxt.tid);
            uint64_t newPart = M.getKeyNumForTime(nxt.tid, nxt.time);
            if (newPart >= nxt.partIndex){
              seek(nxt.time);
            }else{
              buffer.replaceFirst(nxt);
              playbackSleep(5);
            }
            return false;
          }
        }
        thisTime = nxt.time;
        thisIdx = nxt.tid;
        char * d = 0;
        size_t dz = 0;
        M.getEmbeddedData(nxt.tid, nxt.partIndex, d, dz);
        thisPacket.genericFill(thisTime, 0, thisIdx, d, dz, 0, true);

        userSelect[nxt.tid].setKeyNum(nxt.partIndex);
        ++nxt.partIndex;

        // exchange the current packet in the buffer for the next one
        if (M.getEmbeddedTime(nxt.tid, nxt.partIndex, nxt.time)){
          nxt.ghostPacket = false;
        }else{
          nxt.time = M.getNowms(nxt.tid);
          uint64_t newPart = M.getKeyNumForTime(nxt.tid, nxt.time);
          if (newPart >= nxt.partIndex){
            seek(nxt.time);
            return false;
          }else{
            nxt.ghostPacket = true;
          }
        }
        buffer.replaceFirst(nxt);
        return true;
      }

      // if we're going to read past the end of the data page...
      if (nxt.offset >= curPage[nxt.tid].len ||
          (!memcmp(curPage[nxt.tid].mapped + nxt.offset, "\000\000\000\000", 4))){
        // For non-live, we may have just reached the end of the track. That's normal and fine, drop it.
        if (!M.getLive() && nxt.time >= M.getLastms(nxt.tid)){
          dropTrack(nxt.tid, "end of VoD track reached", false);
          return false;
        }
        // Check if there is a next page for the timestamp we're looking for.
        if (M.getPageNumberForTime(nxt.tid, nxt.time) != currentPage[nxt.tid]){
          loadPageForKey(nxt.tid, M.getPageNumberForTime(nxt.tid, nxt.time));
          nxt.offset = 0;
          //Only read the next time if the page load succeeded and there is a packet to read from
          if (curPage[nxt.tid].mapped && curPage[nxt.tid].mapped[0] == 'D'){
            nxt.time = getDTSCTime(curPage[nxt.tid].mapped, 0);
            nxt.ghostPacket = false;
          }else{
            nxt.ghostPacket = true;
          }
          buffer.replaceFirst(nxt);
          return false;
        }
        // We're still on the same page; ghost packets should update their time and retry later
        if (nxt.ghostPacket){
          nxt.time = M.getNowms(nxt.tid);
          buffer.replaceFirst(nxt);
          playbackSleep(5);
          return false;
        }
        if (nxt.offset >= curPage[nxt.tid].len){
          INFO_MSG("Reading past end of page %s: %" PRIu64 " > %" PRIu64 " for time %" PRIu64 " on track %zu", curPage[nxt.tid].name.c_str(), nxt.offset, curPage[nxt.tid].len, nxt.time, nxt.tid);
          dropTrack(nxt.tid, "reading past end of page");
        }else{
          INFO_MSG("Invalid packet: no data @%" PRIu64 " in %s for time %" PRIu64 " on track %zu", nxt.offset, curPage[nxt.tid].name.c_str(), nxt.time, nxt.tid);
          dropTrack(nxt.tid, "zero packet");
        }
        return false;
      }
      // We know this packet will be valid, pre-load it so we know its length
      DTSC::Packet preLoad(curPage[nxt.tid].mapped + nxt.offset, 0, true);
      nxt.time = preLoad.getTime();

      nextTime = 0;

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
        break;//Packet valid!
      }

      //no next packet on the current page

      //Check if this is the last packet of a VoD stream. Return success and drop the track.
      if (!M.getLive() && nxt.time >= M.getLastms(nxt.tid)){
        thisPacket.reInit(curPage[nxt.tid].mapped + nxt.offset, 0, true);
        thisIdx = nxt.tid;
        thisTime = nxt.time;
        dropTrack(nxt.tid, "end of non-live track reached", false);
        return true;
      }

      //Check if there exists a different page for the next key
      uint32_t thisKey = M.getKeyNumForTime(nxt.tid, nxt.time);
      uint32_t nextKeyPage = INVALID_KEY_NUM;
      // Make sure we only try to read the page for the next key if it actually should be available
      // Note: specifically uses `keys` instead of `getKeys` because these are page-related operations
      DTSC::Keys keys(M.keys(nxt.tid));
      if (keys.getEndValid() >= thisKey+1){nextKeyPage = M.getPageNumberForKey(nxt.tid, thisKey + 1);}
      if (nextKeyPage != INVALID_KEY_NUM && nextKeyPage != currentPage[nxt.tid]){
        // If so, the next key is our next packet
        nextTime = keys.getTime(thisKey + 1);
        userSelect[nxt.tid].setKeyNum(thisKey + 1);

        //If the next packet should've been before the current packet, something is wrong. Abort, abort!
        if (nextTime < nxt.time){
          //Re-try the read in ~50ms, hoping this is a race condition we missed somewhere.
          Util::sleep(50);
          meta.reloadReplacedPagesIfNeeded();
          // Note: specifically uses `keys` instead of `getKeys` because these are page-related operations
          DTSC::Keys keys(M.keys(nxt.tid));
          nextTime = keys.getTime(thisKey + 1);
          //Still wrong? Abort, abort!
          if (nextTime < nxt.time){
            std::stringstream errMsg;
            errMsg << "next key (" << (thisKey+1) << ") time " << nextTime << " but current time " << nxt.time;
            errMsg << "; currPage=" << currentPage[nxt.tid] << ", nxtPage=" << nextKeyPage;
            errMsg << ", firstKey=" << keys.getFirstValid() << ", endKey=" << keys.getEndValid();
            dropTrack(nxt.tid, errMsg.str().c_str());
            return false;
          }else{
            WARN_MSG("Recovered from race condition");
          }
        }
        break;//Valid packet!
      }

      // Force valid packet if nowMs is higher than current packet time
      if (!M.getVod() && M.getNowms(nxt.tid) > nxt.time){break;}

      //Okay, there's no next page yet, and no next packet on this page either.
      //That means we're waiting for data to show up, somewhere.
      
      //In non-sync mode, shuffle the just-tried packet to the end of queue and retry
      if (!buffer.getSyncMode()){
        buffer.moveFirstToEnd();
        continue;
      }

      // in sync mode, after ~25 seconds, give up and drop the track.
      if (++emptyCount >= dataWaitTimeout){
        //curPage[nxt.tid].mapped + nxt.offset + preLoad.getDataLen()
        WARN_MSG("Waiting at %s byte %zu", curPage[nxt.tid].name.c_str(), nxt.offset + preLoad.getDataLen());
        dropTrack(nxt.tid, "EOP: data wait timeout");
        return false;
      }
      //every ~1 second, check if the stream is not offline
      if (emptyCount % 100 == 0 && Util::getStreamStatus(streamName) == STRMSTAT_OFF){
        if (M.getLive()){
          Util::logExitReason(ER_CLEAN_EOF, "Live stream source shut down");
          thisPacket.null();
          return true;
        }else if (!Util::startInput(streamName)){
          Util::logExitReason(ER_UNKNOWN, "VoD stream source shut down and could not be restarted");
          thisPacket.null();
          return true;
        }
      }
      
      //Fine! We didn't want a packet, anyway. Let's try again later.
      playbackSleep(10);
      return false;
    }

    if (trackTries == buffer.size()){
      //Fine! We didn't want a packet, anyway. Let's try again later.
      playbackSleep(10);
      return false;
    }

    // we've handled all special cases - at this point the packet should exist
    // let's load it
    thisPacket.reInit(curPage[nxt.tid].mapped + nxt.offset, 0, true);
    // if it failed, drop the track and continue
    if (!thisPacket){
      dropTrack(nxt.tid, "packet load failure");
      return false;
    }
    emptyCount = 0; // valid packet - reset empty counter
    thisIdx = nxt.tid;
    thisTime = nxt.time;

    if (!userSelect[nxt.tid]){
      dropTrack(nxt.tid, "track is not alive!");
      return false;
    }

    //Update keynum only when the second flips over in the timestamp
    //We do this because DTSC::Keys is pretty CPU-heavy
    if (nxt.time / 5000 < nextTime/5000){
      uint32_t thisKey = M.getKeyNumForTime(nxt.tid, nxt.time);
      userSelect[nxt.tid].setKeyNum(thisKey);
    }

    // we assume the next packet is the next on this same page
    nxt.offset += thisPacket.getDataLen();
    if (!nextTime){
      // If time is not known yet, insert a ghostPacket with a known safe time
      nxt.time = M.getNowms(nxt.tid);
      nxt.ghostPacket = true;
    }else{
      nxt.time = nextTime;
      nxt.ghostPacket = false;
    }
    ++nxt.partIndex;

    // exchange the current packet in the buffer for the next one
    buffer.replaceFirst(nxt);
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

  /// Writes data to statConn once per second, or more often if force==true.
  /// Also handles push status updates
  void Output::stats(bool force){
    // cancel stats update if not initialized
    if (!isInitialized){return;}
    // also cancel if it has been less than a second since the last update
    // unless force is set to true
    uint64_t now = thisBootMs / 1000;
    if (now <= lastStats && !force){return;}

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
          pData["tracks"].append((uint64_t)it->first);
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
        Util::sendUDPApi(pStat);
        lastPushUpdate = now;
      }
    }

    // Disable stats for HTTP internal output
    if (Comms::sessionStreamInfoMode == SESS_HTTP_DISABLED && capa["name"].asStringRef() == "HTTP"){return;}

    // Set the token to the pid for outputs which do not generate it in the requestHandler
    if (!tkn.size()){ tkn = JSON::Value(getpid()).asString(); }

    if (!statComm){
      statComm.reload(streamName, getConnectedBinHost(), tkn, getStatsName(), reqUrl);
    }
    if (!statComm){
      Util::logExitReason(ER_SHM_LOST, "could not connect to session");
      onFail("Shutting down since this session is not allowed to view this stream");
      statComm.unload();
      return;
    }
    lastStats = now;
    VERYHIGH_MSG("Writing stats: %s, %s, %s, %" PRIu64 ", %" PRIu64, getConnectedHost().c_str(), streamName.c_str(),
             tkn.c_str(), myConn.dataUp(), myConn.dataDown());
    statComm.setNow(now);
    connStats(now, statComm);
    statComm.setLastSecond(thisPacket ? thisPacket.getTime()/1000 : 0);
    statComm.setPid(getpid());
    if (statComm.getExit() || statComm.getStatus() & COMM_STATUS_REQDISCONNECT){
      Util::logExitReason(ER_CLEAN_INTENDED_STOP, "shutdown due to session end");
      onFail("Shutting down due to session end");
      statComm.unload();
      return;
    }


    if (newUA && ((now - myConn.connTime()) >= uaDelay || !myConn) && UA.size()){
      JSON::Value APIcall;
      APIcall["tag_sessid"][statComm.sessionId] = "UA:"+UA;
      Util::sendUDPApi(APIcall);
      newUA = false;
    }

    if (isPushing()){
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        if (it->second.getStatus() & COMM_STATUS_REQDISCONNECT){
          if (dropPushTrack(it->second.getTrack(), "disconnect request from buffer")){break;}
        }
        if (!it->second){
          if (dropPushTrack(it->second.getTrack(), "track mapping no longer valid")){break;}
        }
      }
    }
  }

  void Output::connStats(uint64_t now, Comms::Connections &statComm){
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

  /// \brief Makes the generic writer available to output classes
  /// \param file target URL or filepath
  /// \param append whether to open this connection in truncate or append mode
  /// \param conn connection which will be used to send data. Will use Output's internal myConn if not initialised
  bool Output::connectToFile(std::string file, bool append, Socket::Connection *conn){
    int outFile = -1;
    if (!conn) {conn = &myConn;}
    bool isFileTarget = HTTP::localURIResolver().link(file).isLocalPath();
    if (!Util::externalWriter(file, outFile, append)){return false;}
    if (*conn && isFileTarget) {
      flock(conn->getSocket(), LOCK_UN | LOCK_NB);
    }
    // Lock the file in exclusive mode to ensure no other processes write to it
    if(isFileTarget && flock(outFile, LOCK_EX | LOCK_NB)){
      ERROR_MSG("Failed to lock file %s, error: %s", file.c_str(), strerror(errno));
      return false;
    }

    //Ensure the Socket::Connection is valid before we overwrite the socket
    if (!*conn){
      static int tmpFd = open("/dev/null", O_RDWR);
      conn->open(tmpFd);
      //We always want to close sockets opened in this way on fork
      Util::Procs::socketList.insert(tmpFd);
    }

    int r = dup2(outFile, conn->getSocket());
    if (r == -1){
      ERROR_MSG("Failed to create an alias for the socket %d -> %d using dup2: %s.", outFile, conn->getSocket(), strerror(errno));
      return false;
    }
    close(outFile);
    return true;
  }

  std::string Output::getExitTriggerPayload(){
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
    payl << Util::mRExitReason << '\n';
    payl << Util::exitReason << '\n';
    return payl.str();
  }
  
  void Output::recEndTrigger(){
    if (Util::Config::binaryType == Util::OUTPUT && config->hasOption("target") && Triggers::shouldTrigger("RECORDING_END", streamName)){
      Triggers::doTrigger("RECORDING_END", getExitTriggerPayload(), streamName);
    }
  }

  void Output::outputEndTrigger(){
    if (Util::Config::binaryType == Util::OUTPUT && config->hasOption("target") && Triggers::shouldTrigger("OUTPUT_END", streamName)){
      Triggers::doTrigger("OUTPUT_END", getExitTriggerPayload(), streamName);
    }
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
        if (M.getNowms(*it) > oneTime){oneTime = M.getNowms(*it);}
      }
      Util::wait(2000);
      for (std::set<size_t>::iterator it = vTracks.begin(); it != vTracks.end(); ++it){
        if (M.getNowms(*it) > twoTime){twoTime = M.getNowms(*it);}
      }
      if (twoTime <= oneTime+500){
        disconnect();
        INFO_MSG("Waiting for stream reset before attempting push input accept (%" PRIu64 " <= %" PRIu64 "+500)", twoTime, oneTime);
        while (streamStatus != STRMSTAT_OFF && keepGoing()){
          userSelect.clear();
          Util::wait(250);
          streamStatus = Util::getStreamStatus(streamName);
        }
        reconnect();
      }
    }
    while (((streamStatus != STRMSTAT_WAIT && streamStatus != STRMSTAT_READY) || !meta) && keepGoing()){
      INFO_MSG("Waiting for %s buffer to be ready... (%u)", streamName.c_str(), streamStatus);
      disconnect();
      streamStatus = Util::getStreamStatus(streamName);
      if (streamStatus == STRMSTAT_OFF || streamStatus == STRMSTAT_WAIT || streamStatus == STRMSTAT_READY){
        INFO_MSG("Reconnecting to %s buffer... (%u)", streamName.c_str(), streamStatus);
        reconnect();
        streamStatus = Util::getStreamStatus(streamName);
      }
      if (((streamStatus != STRMSTAT_WAIT && streamStatus != STRMSTAT_READY) || !meta) && keepGoing()){
        Util::wait(100);
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
