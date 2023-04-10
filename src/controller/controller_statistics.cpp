#include "controller_capabilities.h"
#include "controller_limits.h"
#include "controller_push.h"
#include "controller_statistics.h"
#include "controller_storage.h"
#include <cstdio>
#include <fstream>
#include <list>
#include <mist/bitfields.h>
#include <mist/config.h>
#include <mist/dtsc.h>
#include <mist/procs.h>
#include <mist/shared_memory.h>
#include <mist/stream.h>
#include <mist/url.h>
#include <sys/statvfs.h> //for fstatvfs
#include <mist/triggers.h>
#include <signal.h>

#ifndef KILL_ON_EXIT
#define KILL_ON_EXIT false
#endif

// These are used to store "clients" field requests in a bitfield for speedup.
#define STAT_CLI_HOST 1
#define STAT_CLI_STREAM 2
#define STAT_CLI_PROTO 4
#define STAT_CLI_CONNTIME 8
#define STAT_CLI_POSITION 16
#define STAT_CLI_DOWN 32
#define STAT_CLI_UP 64
#define STAT_CLI_BPS_DOWN 128
#define STAT_CLI_BPS_UP 256
#define STAT_CLI_SESSID 1024
#define STAT_CLI_PKTCOUNT 2048
#define STAT_CLI_PKTLOST 4096
#define STAT_CLI_PKTRETRANSMIT 8192
#define STAT_CLI_ALL 0xFFFF
// These are used to store "totals" field requests in a bitfield for speedup.
#define STAT_TOT_CLIENTS 1
#define STAT_TOT_BPS_DOWN 2
#define STAT_TOT_BPS_UP 4
#define STAT_TOT_INPUTS 8
#define STAT_TOT_OUTPUTS 16
#define STAT_TOT_PERCLOST 32
#define STAT_TOT_PERCRETRANS 64
#define STAT_TOT_ALL 0xFF

// Mapping of sessId -> session statistics
std::map<std::string, Controller::statSession> sessions;

std::map<std::string, Controller::triggerLog> Controller::triggerStats; ///< Holds prometheus stats for trigger executions
bool Controller::killOnExit = KILL_ON_EXIT;
tthread::mutex Controller::statsMutex;
uint64_t Controller::statDropoff = 0;
static uint64_t cpu_use = 0;

char noBWCountMatches[1717];
uint64_t bwLimit = 128 * 1024 * 1024; // gigabit default limit

const char nullAddress[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static Controller::statLog emptyLogEntry = {0, 0, 0, 0, 0, 0 ,0 ,0, "", nullAddress, ""};
bool notEmpty(const Controller::statLog & dta){
  return dta.time || dta.firstActive || dta.lastSecond || dta.down || dta.up || dta.streamName.size() || dta.connectors.size();
}

// For server-wide totals. Local to this file only.
struct streamTotals{
  uint64_t upBytes;
  uint64_t downBytes;
  uint64_t inputs;
  uint64_t outputs;
  uint64_t viewers;
  uint64_t unspecified;
  uint64_t currIns;
  uint64_t currOuts;
  uint64_t currViews;
  uint64_t currUnspecified;
  uint8_t status;
  uint64_t viewSeconds;
  uint64_t packSent;
  uint64_t packLoss;
  uint64_t packRetrans;
};

Comms::Sessions statComm;
bool statCommActive = false;
// Global server wide statistics
static uint64_t servUpBytes = 0;
static uint64_t servDownBytes = 0;
static uint64_t servUpOtherBytes = 0;
static uint64_t servDownOtherBytes = 0;
static uint64_t servInputs = 0;
static uint64_t servUnspecified = 0;
static uint64_t servOutputs = 0;
static uint64_t servViewers = 0;
static uint64_t servSeconds = 0;
static uint64_t servPackSent = 0;
static uint64_t servPackLoss = 0;
static uint64_t servPackRetrans = 0;
// Total time watched for all sessions which are no longer active
static uint64_t viewSecondsTotal = 0;
// Mapping of streamName -> summary of stream-wide statistics
static std::map<std::string, struct streamTotals> streamStats;

// If streamName does not exist yet in streamStats, create and init an entry for it
static void createEmptyStatsIfNeeded(const std::string & streamName){
  if (streamStats.count(streamName)){return;}
  streamTotals & sT = streamStats[streamName];
  sT.upBytes = 0;
  sT.downBytes = 0;
  sT.inputs = 0;
  sT.outputs = 0;
  sT.viewers = 0;
  sT.unspecified = 0;
  sT.currIns = 0;
  sT.currOuts = 0;
  sT.currUnspecified = 0;
  sT.currViews = 0;
  sT.status = 0;
  sT.viewSeconds = 0;
  sT.packSent = 0;
  sT.packLoss = 0;
  sT.packRetrans = 0;
}

/// Convert bandwidth config into memory format
void Controller::updateBandwidthConfig(){
  size_t offset = 0;
  bwLimit = 128 * 1024 * 1024; // gigabit default limit
  memset(noBWCountMatches, 0, 1717);
  if (Storage.isMember("bandwidth")){
    if (Storage["bandwidth"].isMember("limit")){bwLimit = Storage["bandwidth"]["limit"].asInt();}
    if (Storage["bandwidth"].isMember("exceptions")){
      jsonForEach(Storage["bandwidth"]["exceptions"], j){
        std::string newbins = Socket::getBinForms(j->asStringRef());
        if (offset + newbins.size() < 1700){
          memcpy(noBWCountMatches + offset, newbins.data(), newbins.size());
          offset += newbins.size();
        }
      }
    }
  }
  //Localhost is always excepted from counts
  {
    std::string newbins = Socket::getBinForms("::1");
    if (offset + newbins.size() < 1700){
      memcpy(noBWCountMatches + offset, newbins.data(), newbins.size());
      offset += newbins.size();
    }
  }
  {
    std::string newbins = Socket::getBinForms("127.0.0.1/8");
    if (offset + newbins.size() < 1700){
      memcpy(noBWCountMatches + offset, newbins.data(), newbins.size());
      offset += newbins.size();
    }
  }
}

/// This function is ran whenever a stream becomes active.
void Controller::streamStarted(std::string stream){
  INFO_MSG("Stream %s became active", stream.c_str());
  Controller::doAutoPush(stream);
}

/// This function is ran whenever a stream becomes active.
void Controller::streamStopped(std::string stream){
  INFO_MSG("Stream %s became inactive", stream.c_str());
}

/// Invalidates all current sessions for the given streamname
/// Updates the session cache, afterwards.
void Controller::sessions_invalidate(const std::string &streamname){
  if (!statCommActive){
    FAIL_MSG("In shutdown procedure - cannot invalidate sessions.");
    return;
  }
  unsigned int sessCount = 0;
  // Find all matching streams in statComm
  for (size_t i = 0; i < statComm.recordCount(); i++){
    if (statComm.getStatus(i) == COMM_STATUS_INVALID || (statComm.getStatus(i) & COMM_STATUS_DISCONNECT)){continue;}
    if (statComm.getStream(i) == streamname){
      sessCount++;
      // Re-trigger USER_NEW trigger for this session
      kill(statComm.getPid(i), SIGUSR1);
    }
  }
  INFO_MSG("Invalidated %u session(s) for stream %s", sessCount, streamname.c_str());
}

/// Shuts down all current sessions for the given streamname
/// Updates the session cache, afterwards. (if any action was taken)
void Controller::sessions_shutdown(JSON::Iter &i){
  if (i->isArray() || i->isObject()){
    jsonForEach(*i, it){sessions_shutdown(it);}
    return;
  }
  if (i->isString()){
    sessions_shutdown(i.key(), i->asStringRef());
    return;
  }
  // not handled, ignore
}

/// Shuts down the given session
/// Updates the session cache, afterwards.
void Controller::sessId_shutdown(const std::string &sessId){
  if (!statCommActive){
    FAIL_MSG("In controller shutdown procedure - cannot shutdown sessions.");
    return;
  }
  killConnections(sessId);
  INFO_MSG("Shut down session with session ID %s", sessId.c_str());
}

/// Tags the given session
void Controller::sessId_tag(const std::string &sessId, const std::string &tag){
  if (!statCommActive){
    FAIL_MSG("In controller shutdown procedure - cannot tag sessions.");
    return;
  }
  tthread::lock_guard<tthread::mutex> guard(statsMutex);
  for (std::map<std::string, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
    if (it->first == sessId){
      it->second.tags.insert(tag);
      return;
    }
  }
  if (tag.substr(0, 3) != "UA:"){
    WARN_MSG("Session %s not found - cannot tag with %s", sessId.c_str(), tag.c_str());
  }
}

/// Shuts down sessions with the given tag set
/// Updates the session cache, afterwards.
void Controller::tag_shutdown(const std::string &tag){
  if (!statCommActive){
    FAIL_MSG("In controller shutdown procedure - cannot shutdown sessions.");
    return;
  }
  unsigned int sessCount = 0;
  tthread::lock_guard<tthread::mutex> guard(statsMutex);
  for (std::map<std::string, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
    if (it->second.tags.count(tag)){
      sessCount++;
      killConnections(it->first);
    }
  }
  INFO_MSG("Shut down %u session(s) for tag %s", sessCount, tag.c_str());
}

/// Shuts down all current sessions for the given streamname
/// Updates the session cache, afterwards.
void Controller::sessions_shutdown(const std::string &streamname, const std::string &protocol){
  if (!statCommActive){
    FAIL_MSG("In controller shutdown procedure - cannot shutdown sessions.");
    return;
  }
  unsigned int sessCount = 0;
  tthread::lock_guard<tthread::mutex> guard(statsMutex);
  // Find all matching streams in statComm and get their sessId
  for (size_t i = 0; i < statComm.recordCount(); i++){
    if (statComm.getStatus(i) == COMM_STATUS_INVALID || (statComm.getStatus(i) & COMM_STATUS_DISCONNECT)){continue;}
    if ((!streamname.size() || statComm.getStream(i) == streamname) &&
      (!protocol.size() || statComm.hasConnector(i, protocol))){
      uint32_t pid = statComm.getPid(i);
      sessCount++;
      if (pid > 1){
        Util::Procs::Stop(pid);
        INFO_MSG("Killing PID %" PRIu32, pid);
      }
    }
  }
  INFO_MSG("Shut down %u sessions for stream %s/%s", sessCount,
           streamname.c_str(), protocol.c_str());
}

/// This function runs as a thread and roughly once per second retrieves
/// statistics from all connected clients, as well as wipes
/// old statistics that have disconnected over 10 minutes ago.
void Controller::SharedMemStats(void *config){
  HIGH_MSG("Starting stats thread");
  statComm.reload(true);
  statCommActive = true;
  std::set<std::string> inactiveStreams;
  Controller::initState();
  bool shiftWrites = true;
  bool firstRun = true;
  while (((Util::Config *)config)->is_active){
    {
      std::ifstream cpustat("/proc/stat");
      if (cpustat){
        char line[300];
        while (cpustat.getline(line, 300)){
          static uint64_t cl_total = 0, cl_idle = 0;
          uint64_t c_user, c_nice, c_syst, c_idle, c_total;
          if (sscanf(line, "cpu %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64, &c_user, &c_nice, &c_syst, &c_idle) == 4){
            c_total = c_user + c_nice + c_syst + c_idle;
            if (c_total > cl_total){
              cpu_use = (uint64_t)(1000 - ((c_idle - cl_idle) * 1000) / (c_total - cl_total));
            }else{
              cpu_use = 0;
            }
            cl_total = c_total;
            cl_idle = c_idle;
            break;
          }
        }
      }
    }
    {
      tthread::lock_guard<tthread::mutex> guard(Controller::configMutex);
      tthread::lock_guard<tthread::mutex> guard2(statsMutex);
      // parse current users
      statLeadIn();
      COMM_LOOP(statComm, statOnActive(id), statOnDisconnect(id));
      statLeadOut();

      if (firstRun){
        firstRun = false;
        servUpOtherBytes = 0;
        servDownOtherBytes = 0;
        servUpBytes = 0;
        servDownBytes = 0;
        servSeconds = 0;
        servPackSent = 0;
        servPackLoss = 0;
        servPackRetrans = 0;
        for (std::map<std::string, struct streamTotals>::iterator it = streamStats.begin();
             it != streamStats.end(); ++it){
          it->second.upBytes = 0;
          it->second.downBytes = 0;
          it->second.viewSeconds = 0;
          it->second.packSent = 0;
          it->second.packLoss = 0;
          it->second.packRetrans = 0;
        }
      }
      unsigned int tOut = Util::bootSecs() - STATS_DELAY;
      unsigned int tIn = Util::bootSecs() - STATS_INPUT_DELAY;
      if (streamStats.size()){
        for (std::map<std::string, struct streamTotals>::iterator it = streamStats.begin();
             it != streamStats.end(); ++it){
          it->second.currViews = 0;
          it->second.currIns = 0;
          it->second.currOuts = 0;
          it->second.currUnspecified = 0;
        }
      }
      // wipe old statistics and set session type counters
      if (sessions.size()){
        std::list<std::string> mustWipe;
        // Ensure cutOffPoint is either time of boot or 10 minutes ago, whichever is closer.
        // Prevents wrapping around to high values close to system boot time.
        uint64_t cutOffPoint = Util::bootSecs();
        if (cutOffPoint > STAT_CUTOFF){
          cutOffPoint -= STAT_CUTOFF;
        }else{
          cutOffPoint = 0;
        }
        for (std::map<std::string, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
          // This part handles ending sessions, keeping them in cache for now
          if (it->second.getEnd() < cutOffPoint){
            viewSecondsTotal += it->second.getConnTime();
            mustWipe.push_back(it->first);
            // Don't count this session as a viewer
            continue;
          }
          // Recount input, output and viewer type sessions
          switch (it->second.getSessType()){
          case SESS_UNSET: break;
          case SESS_VIEWER:
            if (it->second.hasDataFor(tOut)){
              streamStats[it->second.getStreamName()].currViews++;
            }
            servSeconds += it->second.getConnTime();
            break;
          case SESS_INPUT:
            if (it->second.hasDataFor(tIn)){
              streamStats[it->second.getStreamName()].currIns++;
            }
            break;
          case SESS_OUTPUT:
            if (it->second.hasDataFor(tOut)){
              streamStats[it->second.getStreamName()].currOuts++;
            }
            break;
          case SESS_UNSPECIFIED:
            if (it->second.hasDataFor(tOut)){
              streamStats[it->second.getStreamName()].currUnspecified++;
            }
            break;
          }
        }
        while (mustWipe.size()){
          sessions.erase(mustWipe.front());
          mustWipe.pop_front();
        }
      }
      Util::RelAccX *strmStats = streamsAccessor();
      if (!strmStats || !strmStats->isReady()){strmStats = 0;}
      uint64_t strmPos = 0;
      if (strmStats){
        if (shiftWrites || (strmStats->getEndPos() - strmStats->getDeleted() != streamStats.size())){
          shiftWrites = true;
          strmPos = strmStats->getEndPos();
        }else{
          strmPos = strmStats->getDeleted();
        }
      }
      if (streamStats.size()){
        for (std::map<std::string, struct streamTotals>::iterator it = streamStats.begin();
             it != streamStats.end(); ++it){
          uint8_t newState = Util::getStreamStatus(it->first);
          uint8_t oldState = it->second.status;
          if (newState != oldState){
            it->second.status = newState;
            if (newState == STRMSTAT_READY){
              streamStarted(it->first);
            }else{
              if (oldState == STRMSTAT_READY){streamStopped(it->first);}
            }
          }
          if (newState == STRMSTAT_OFF){inactiveStreams.insert(it->first);}
          if (strmStats){
            if (shiftWrites){strmStats->setString("stream", it->first, strmPos);}
            strmStats->setInt("status", it->second.status, strmPos);
            strmStats->setInt("viewers", it->second.currViews, strmPos);
            strmStats->setInt("inputs", it->second.currIns, strmPos);
            strmStats->setInt("outputs", it->second.currOuts, strmPos);
            strmStats->setInt("unspecified", it->second.currUnspecified, strmPos);
            ++strmPos;
          }
        }
      }
      if (strmStats && shiftWrites){
        shiftWrites = false;
        uint64_t prevEnd = strmStats->getEndPos();
        strmStats->setEndPos(strmPos);
        strmStats->setDeleted(prevEnd);
      }
      while (inactiveStreams.size()){
        const std::string & streamName = *inactiveStreams.begin();
        const streamTotals & stats = streamStats.at(streamName);
        if(Triggers::shouldTrigger("STREAM_END", streamName)){
          std::stringstream payload;
          payload << streamName+"\n" << stats.downBytes << "\n" << stats.upBytes << "\n" << stats.viewers << "\n" << stats.inputs << "\n" << stats.outputs << "\n" << stats.viewSeconds;
          Triggers::doTrigger("STREAM_END", payload.str(), streamName);
        }
        streamStats.erase(streamName);
        inactiveStreams.erase(inactiveStreams.begin());
        shiftWrites = true;
      }
      /*LTS-START*/
      Controller::checkServerLimits();
      /*LTS-END*/
    }
    Util::wait(1000);
  }
  statCommActive = false;
  HIGH_MSG("Stopping stats thread");
  if (Util::Config::is_restarting){
    statComm.setMaster(false);
  }else{/*LTS-START*/
    if (Controller::killOnExit){
      WARN_MSG("Killing all connected clients to force full shutdown");
      statComm.finishAll();
    }
    /*LTS-END*/
  }
  Controller::deinitState(Util::Config::is_restarting);
}

/// Gets a complete list of all streams currently in active state, with optional prefix matching
std::set<std::string> Controller::getActiveStreams(const std::string &prefix){
  std::set<std::string> ret;
  Util::RelAccX *strmStats = streamsAccessor();
  if (!strmStats || !strmStats->isReady()){return ret;}
  uint64_t endPos = strmStats->getEndPos();
  if (prefix.size()){
    for (uint64_t i = strmStats->getDeleted(); i < endPos; ++i){
      if (strmStats->getInt("status", i) != STRMSTAT_READY){continue;}
      const char *S = strmStats->getPointer("stream", i);
      if (!strncmp(S, prefix.data(), prefix.size())){ret.insert(S);}
    }
  }else{
    for (uint64_t i = strmStats->getDeleted(); i < endPos; ++i){
      if (strmStats->getInt("status", i) != STRMSTAT_READY){continue;}
      ret.insert(strmStats->getPointer("stream", i));
    }
  }
  return ret;
}

/// Kills all connection of a given session
/// Assumes the session cache will be updated separately - may not work correctly if this is forgotten!
void Controller::killConnections(std::string sessId){
  if (statCommActive){
    // Find a matching stream in statComm with a matching sessID and kill it
    for (size_t i = 0; i < statComm.recordCount(); i++){
      if (statComm.getStatus(i) == COMM_STATUS_INVALID || (statComm.getStatus(i) & COMM_STATUS_DISCONNECT)){continue;}
      if (statComm.getSessId(i) == sessId){
        uint32_t pid = statComm.getPid(i);
        if (pid > 1){
          Util::Procs::Stop(pid);
          INFO_MSG("Killing PID %" PRIu32, pid);
        }
      }
    }
  }
}

/// Updates the given active connection with new stats data.
void Controller::statSession::update(uint64_t index, Comms::Sessions &statComm){
  if (sessId == ""){
    sessId = statComm.getSessId(index);
  }

  if (sessionType == SESS_UNSET){
    if (sessId[0] == 'I'){
      sessionType = SESS_INPUT;
    }else if (sessId[0] == 'O'){
      sessionType = SESS_OUTPUT;
    }else if (sessId[0] == 'U'){
      sessionType = SESS_UNSPECIFIED;
    }else{
      sessionType = SESS_VIEWER;
    }
  }

  uint64_t prevNow = curData.log.size() ? curData.log.rbegin()->first : 0;
  // only parse last received data, if newer
  if (prevNow > statComm.getNow(index)){return;};
  long long prevDown = getDown();
  long long prevUp = getUp();
  uint64_t prevPktSent = getPktCount();
  uint64_t prevPktLost = getPktLost();
  uint64_t prevPktRetrans = getPktRetransmit();
  uint64_t prevFirstActive = getFirstActive();

  curData.update(statComm, index);
  const std::string& streamName = getStreamName();
  // Export tags to session
  if (tags.size()){
    std::stringstream tagStream;
    for (std::set<std::string>::iterator it = tags.begin(); it != tags.end(); ++it){
      tagStream << "[" << *it << "]";
    }
    statComm.setTags(tagStream.str(), index);
  } else {
    statComm.setTags("", index);
  }
  
  uint64_t secIncr = prevFirstActive ? (statComm.getNow(index) - prevNow) : 0;
  long long currDown = getDown();
  long long currUp = getUp();
  uint64_t currPktSent = getPktCount();
  uint64_t currPktLost = getPktLost();
  uint64_t currPktRetrans = getPktRetransmit();
  if (currUp - prevUp < 0 || currDown - prevDown < 0){
    INFO_MSG("Negative data usage! %lldu/%lldd (u%lld->%lld) in %s over %s, #%" PRIu64, currUp - prevUp,
             currDown - prevDown, prevUp, currUp, streamName.c_str(), curData.log.rbegin()->second.connectors.c_str(), index);
  }else{
    if (!noBWCount){
      size_t bwMatchOffset = 0;
      noBWCount = 1;
      while (noBWCountMatches[bwMatchOffset + 16] != 0 && bwMatchOffset < 1700){
        if (Socket::matchIPv6Addr(statComm.getHost(index), std::string(noBWCountMatches + bwMatchOffset, 16),
                                  noBWCountMatches[bwMatchOffset + 16])){
          noBWCount = 2;
          break;
        }
        bwMatchOffset += 17;
      }
      if (noBWCount == 2){
        MEDIUM_MSG("Not counting for main bandwidth");
      }else{
        MEDIUM_MSG("Counting connection for main bandwidth");
      }
    }
    if (noBWCount == 2){
      servUpOtherBytes += currUp - prevUp;
      servDownOtherBytes += currDown - prevDown;
    }else{
      servUpBytes += currUp - prevUp;
      servDownBytes += currDown - prevDown;
      servPackSent += currPktSent - prevPktSent;
      servPackLoss += currPktLost - prevPktLost;
      servPackRetrans += currPktRetrans - prevPktRetrans;
    }
  }
  if (!prevFirstActive && streamName.size()){
    createEmptyStatsIfNeeded(streamName);
    switch(sessionType){
      case SESS_INPUT:
        ++servInputs;
        streamStats[streamName].inputs++;
        break;
      case SESS_OUTPUT:
        ++servOutputs;
        streamStats[streamName].outputs++;
        break;
      case SESS_VIEWER:
        ++servViewers;
        streamStats[streamName].viewers++;
        break;
      case SESS_UNSPECIFIED:
        ++servUnspecified;
        streamStats[streamName].unspecified++;
        break;
      case SESS_UNSET:
        break;
    }
  }
  // Only count connections that are countable
  if (noBWCount != 2){ 
    createEmptyStatsIfNeeded(streamName);
    streamStats[streamName].upBytes += currUp - prevUp;
    streamStats[streamName].downBytes += currDown - prevDown;
    streamStats[streamName].packSent += currPktSent - prevPktSent;
    streamStats[streamName].packLoss += currPktLost - prevPktLost;
    streamStats[streamName].packRetrans += currPktRetrans - prevPktRetrans;
    if (sessionType == SESS_VIEWER){streamStats[streamName].viewSeconds += secIncr;}
  }
}

Controller::sessType Controller::statSession::getSessType(){
  return sessionType;
}

/// Ends the currently active session by inserting a null datapoint one second after the last datapoint
void Controller::statSession::finish(){
  if (!getFirstActive()){return;}
  uint64_t duration = getEnd() - getFirstActive();
  if (duration < 1){duration = 1;}
  std::stringstream tagStream;
  if (tags.size()){
    for (std::set<std::string>::iterator it = tags.begin(); it != tags.end(); ++it){
      tagStream << "[" << *it << "]";
    }
  }
  const std::string& streamName = getStreamName();
  const std::string& curConnector = getConnectors();
  const std::string& host = getStrHost();
  Controller::logAccess(sessId, streamName, curConnector, host, duration, getUp(),
                        getDown(), tagStream.str());
  if (Controller::accesslog.size()){
    if (Controller::accesslog == "LOG"){
      std::stringstream accessStr;
      accessStr << "Session <" << sessId << "> " << streamName << " (" << curConnector
                << ") from " << host << " ended after " << duration << "s, avg "
                << getUp() / duration / 1024 << "KB/s up " << getDown() / duration / 1024 << "KB/s down.";
      if (tags.size()){accessStr << " Tags: " << tagStream.str();}
      Controller::Log("ACCS", accessStr.str());
    }else{
      static std::ofstream accLogFile;
      static std::string accLogFileName;
      if (accLogFileName != Controller::accesslog || !accLogFile.good()){
        accLogFile.close();
        accLogFile.open(Controller::accesslog.c_str(), std::ios_base::app);
        if (!accLogFile.good()){
          FAIL_MSG("Could not open access log file '%s': %s", Controller::accesslog.c_str(), strerror(errno));
        }else{
          accLogFileName = Controller::accesslog;
        }
      }
      if (accLogFile.good()){
        time_t rawtime;
        struct tm *timeinfo;
        struct tm tmptime;
        char buffer[100];
        time(&rawtime);
        timeinfo = localtime_r(&rawtime, &tmptime);
        strftime(buffer, 100, "%F %H:%M:%S", timeinfo);
        accLogFile << buffer << ", " << sessId << ", " << streamName << ", "
                   << curConnector << ", " << host << ", " << duration << ", "
                   << getUp() / duration / 1024 << ", " << getDown() / duration / 1024 << ", ";
        if (tags.size()){accLogFile << tagStream.str();}
        accLogFile << std::endl;
      }
    }
  }
  tags.clear();
  // Insert null datapoint
  curData.log[curData.log.rbegin()->first + 1] = emptyLogEntry;
}

/// Constructs an empty session
Controller::statSession::statSession(){
  sessionType = SESS_UNSET;
  noBWCount = 0;
  sessId = "";
}

/// Returns the first measured timestamp in this session.
uint64_t Controller::statSession::getStart(){
  if (!curData.log.size()){return 0;}
  return curData.log.begin()->first;
}

/// Returns the last measured timestamp in this session.
uint64_t Controller::statSession::getEnd(){
  if (!curData.log.size()){return 0;}
  return curData.log.rbegin()->first;
}

/// Returns true if there is data for this session at timestamp t.
bool Controller::statSession::hasDataFor(uint64_t t){
  if (curData.hasDataFor(t)){return true;}
  return false;
}

const std::string& Controller::statSession::getSessId(){
  return sessId;
}

uint64_t Controller::statSession::getFirstActive(){
  if (curData.log.size()){
    return curData.log.rbegin()->second.firstActive;
  }
  return 0;
}

const std::string& Controller::statSession::getStreamName(uint64_t t){
  if (curData.hasDataFor(t)){
    return curData.getDataFor(t).streamName;
  }
  return emptyLogEntry.streamName;
}

const std::string& Controller::statSession::getStreamName(){
  if (curData.log.size()){
    return curData.log.rbegin()->second.streamName;
  }
  return emptyLogEntry.streamName;
}

std::string Controller::statSession::getStrHost(uint64_t t){
  std::string host;
  Socket::hostBytesToStr(getHost(t).data(), 16, host);
  return host;
}

std::string Controller::statSession::getStrHost(){
  std::string host;
  Socket::hostBytesToStr(getHost().data(), 16, host);
  return host;
}

const std::string& Controller::statSession::getHost(uint64_t t){
  if (curData.hasDataFor(t)){
    return curData.getDataFor(t).host;
  }
  return emptyLogEntry.host;
}

const std::string& Controller::statSession::getHost(){
  if (curData.log.size()){
    return curData.log.rbegin()->second.host;
  }
  return emptyLogEntry.host;
}

const std::string& Controller::statSession::getConnectors(uint64_t t){
  if (curData.hasDataFor(t)){
    return curData.getDataFor(t).connectors;
  }
  return emptyLogEntry.connectors;
}

const std::string& Controller::statSession::getConnectors(){
  if (curData.log.size()){
    return curData.log.rbegin()->second.connectors;
  }
  return emptyLogEntry.connectors;
}

/// Returns the cumulative connected time for this session at timestamp t.
uint64_t Controller::statSession::getConnTime(uint64_t t){
  if (curData.hasDataFor(t)){
    return curData.getDataFor(t).time;
  }
  return 0;
}

/// Returns the cumulative connected time for this session.
uint64_t Controller::statSession::getConnTime(){
  if (curData.log.size()){
    return curData.log.rbegin()->second.time;
  }
  return 0;
}

/// Returns the last requested media timestamp for this session at timestamp t.
uint64_t Controller::statSession::getLastSecond(uint64_t t){
  if (curData.hasDataFor(t)){
    return curData.getDataFor(t).lastSecond;
  }
  return 0;
}

/// Returns the cumulative downloaded bytes for this session at timestamp t.
uint64_t Controller::statSession::getDown(uint64_t t){
  if (curData.hasDataFor(t)){
    return curData.getDataFor(t).down;
  }
  return 0;
}

/// Returns the cumulative uploaded bytes for this session at timestamp t.
uint64_t Controller::statSession::getUp(uint64_t t){
  if (curData.hasDataFor(t)){
    return curData.getDataFor(t).up;
  }
  return 0;
}

/// Returns the cumulative downloaded bytes for this session at timestamp t.
uint64_t Controller::statSession::getDown(){
  if (curData.log.size()){
    return curData.log.rbegin()->second.down;
  }
  return 0;
}

/// Returns the cumulative uploaded bytes for this session at timestamp t.
uint64_t Controller::statSession::getUp(){
  if (curData.log.size()){
    return curData.log.rbegin()->second.up;
  }
  return 0;
}

uint64_t Controller::statSession::getPktCount(uint64_t t){
  if (curData.hasDataFor(t)){
    return curData.getDataFor(t).pktCount;
  }
  return 0;
}

/// Returns the cumulative uploaded bytes for this session at timestamp t.
uint64_t Controller::statSession::getPktCount(){
  if (curData.log.size()){
    return curData.log.rbegin()->second.pktCount;
  }
  return 0;
}

uint64_t Controller::statSession::getPktLost(uint64_t t){
  if (curData.hasDataFor(t)){
    return curData.getDataFor(t).pktLost;
  }
  return 0;
}

/// Returns the cumulative uploaded bytes for this session at timestamp t.
uint64_t Controller::statSession::getPktLost(){
  if (curData.log.size()){
    return curData.log.rbegin()->second.pktLost;
  }
  return 0;
}

uint64_t Controller::statSession::getPktRetransmit(uint64_t t){
  if (curData.hasDataFor(t)){
    return curData.getDataFor(t).pktRetransmit;
  }
  return 0;
}

/// Returns the cumulative uploaded bytes for this session at timestamp t.
uint64_t Controller::statSession::getPktRetransmit(){
  if (curData.log.size()){
    return curData.log.rbegin()->second.pktRetransmit;
  }
  return 0;
}

/// Returns the cumulative downloaded bytes per second for this session at timestamp t.
uint64_t Controller::statSession::getBpsDown(uint64_t t){
  uint64_t aTime = t - 5;
  if (aTime < curData.log.begin()->first){aTime = curData.log.begin()->first;}
  if (t <= aTime){return 0;}
  uint64_t valA = getDown(aTime);
  uint64_t valB = getDown(t);
  return (valB - valA) / (t - aTime);
}

/// Returns the cumulative uploaded bytes per second for this session at timestamp t.
uint64_t Controller::statSession::getBpsUp(uint64_t t){
  uint64_t aTime = t - 5;
  if (aTime < curData.log.begin()->first){aTime = curData.log.begin()->first;}
  if (t <= aTime){return 0;}
  uint64_t valA = getUp(aTime);
  uint64_t valB = getUp(t);
  return (valB - valA) / (t - aTime);
}

/// Returns true if there is data available for timestamp t.
bool Controller::statStorage::hasDataFor(unsigned long long t){
  if (!log.size()){return false;}
  return (t >= log.begin()->first);
}

/// Returns a reference to the most current data available at timestamp t.
Controller::statLog &Controller::statStorage::getDataFor(unsigned long long t){
  if (!log.size()){
    return emptyLogEntry;
  }
  std::map<unsigned long long, statLog>::iterator it = log.upper_bound(t);
  if (it != log.begin()){it--;}
  return it->second;
}

/// This function is called by parseStatistics.
/// It updates the internally saved statistics data.
void Controller::statStorage::update(Comms::Sessions &statComm, size_t index){
  statLog tmp;
  tmp.time = statComm.getTime(index);
  if (!log.size() || !log.rbegin()->second.firstActive){
    tmp.firstActive = statComm.getNow(index);
  } else{
    tmp.firstActive = log.rbegin()->second.firstActive;
  }
  tmp.lastSecond = statComm.getLastSecond(index);
  tmp.down = statComm.getDown(index);
  tmp.up = statComm.getUp(index);
  tmp.pktCount = statComm.getPacketCount(index);
  tmp.pktLost = statComm.getPacketLostCount(index);
  tmp.pktRetransmit = statComm.getPacketRetransmitCount(index);
  tmp.connectors = statComm.getConnector(index);
  tmp.streamName = statComm.getStream(index);
  tmp.host = statComm.getHost(index);
  log[statComm.getNow(index)] = tmp;
  // wipe data older than STAT_CUTOFF seconds
  // Ensure cutOffPoint is either time of boot or 10 minutes ago, whichever is closer.
  // Prevents wrapping around to high values close to system boot time.
  uint64_t cutOffPoint = Util::bootSecs();
  if (cutOffPoint > STAT_CUTOFF){
    cutOffPoint -= STAT_CUTOFF;
  }else{
    cutOffPoint = 0;
  }
  while (log.size() && log.begin()->first < cutOffPoint){log.erase(log.begin());}
}

void Controller::statLeadIn(){
  statDropoff = Util::bootSecs() - 3;
}

void Controller::statOnActive(size_t id){
  if (statComm.getNow(id) >= statDropoff){
    // update the session with the latest data
    sessions[statComm.getSessId(id)].update(id, statComm);
  }
}

void Controller::statOnDisconnect(size_t id){
  // Check to see if cleanup is required (when a Session binary fails)
  const std::string thisSessionId = statComm.getSessId(id);
  sessions[thisSessionId].finish();
  // Try to lock to see if the session crashed during boot
  IPC::semaphore sessionLock;
  char semName[NAME_BUFFER_SIZE];
  snprintf(semName, NAME_BUFFER_SIZE, SEM_SESSION, thisSessionId.c_str());
  sessionLock.open(semName, O_CREAT | O_RDWR, ACCESSPERMS, 1);
  if (!sessionLock.tryWaitOneSecond()){
    // Session likely crashed during boot. Remove the session lock which was created on bootup of the session
    sessionLock.unlink();
  }else if (!statComm.sessIdExists(thisSessionId)){
    // There is no running process managing this session, so check if the data page still exists
    IPC::sharedPage dataPage;
    char userPageName[NAME_BUFFER_SIZE];
    snprintf(userPageName, NAME_BUFFER_SIZE, COMMS_SESSIONS, thisSessionId.c_str());
    dataPage.init(userPageName, 1, false, false);
    if(dataPage){
      // Session likely crashed while it was running
      dataPage.init(userPageName, 1, true);
      FAIL_MSG("Session '%s' got cancelled unexpectedly. Cleaning up the leftovers...", thisSessionId.c_str());
    }
    // Finally remove the session lock which was created on bootup of the session
    sessionLock.unlink();
  }
}

void Controller::statLeadOut(){}

/// Returns true if this stream has at least one connected client.
bool Controller::hasViewers(std::string streamName){
  if (sessions.size()){
    long long currTime = Util::bootSecs();
    for (std::map<std::string, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
      if (it->second.getStreamName() == streamName &&
          (it->second.hasDataFor(currTime) || it->second.hasDataFor(currTime - 1))){
        return true;
      }
    }
  }
  return false;
}

/// This takes a "clients" request, and fills in the response data.
///
/// \api
/// `"clients"` requests take the form of:
/// ~~~~~~~~~~~~~~~{.js}
///{
///   //array of streamnames to accumulate. Empty means all.
///   "streams": ["streama", "streamb", "streamc"],
///   //array of protocols to accumulate. Empty means all.
///   "protocols": ["HLS", "HSS"],
///   //list of requested data fields. Empty means all.
///   "fields": ["host", "stream", "protocol", "conntime", "position", "down", "up", "downbps", "upbps","pktcount","pktlost","pktretransmit"],
///   //unix timestamp of measuring moment. Negative means X seconds ago. Empty means now.
///   "time": 1234567
///}
/// ~~~~~~~~~~~~~~~
/// OR
/// ~~~~~~~~~~~~~~~{.js}
/// [
///{},//request object as above
///{}//repeat the structure as many times as wanted
/// ]
/// ~~~~~~~~~~~~~~~
/// and are responded to as:
/// ~~~~~~~~~~~~~~~{.js}
///{
///   //unix timestamp of data. Always present, always absolute.
///   "time": 1234567,
///   //array of actually represented data fields.
///   "fields": [...]
///   //for all clients, the data in the order they appear in the "fields" field.
///   "data": [[x, y, z], [x, y, z], [x, y, z]]
///}
/// ~~~~~~~~~~~~~~~
/// In case of the second method, the response is an array in the same order as the requests.
void Controller::fillClients(JSON::Value &req, JSON::Value &rep){
  tthread::lock_guard<tthread::mutex> guard(statsMutex);
  // first, figure out the timestamp wanted
  int64_t reqTime = 0;
  uint64_t epoch = Util::epoch();
  uint64_t bSecs = Util::bootSecs();
  if (req.isMember("time")){reqTime = req["time"].asInt();}
  // to make sure no nasty timing business takes place, we store the case "now" as a bool.
  bool now = (reqTime == 0);
  //if in the last 600 seconds of unix time (or higher), assume unix time and subtract epoch from it
  if (reqTime > (int64_t)epoch - STAT_CUTOFF){reqTime -= Controller::systemBoot/1000;}
  // add the current time, if negative or zero.
  if (reqTime < 0){reqTime += bSecs;}
  if (reqTime == 0){
    // Ensure cutOffPoint is either time of boot or 10 minutes ago, whichever is closer.
    // Prevents wrapping around to high values close to system boot time.
    uint64_t cutOffPoint = bSecs;
    if (cutOffPoint > STAT_CUTOFF){
      cutOffPoint -= STAT_CUTOFF;
    }else{
      cutOffPoint = 0;
    }
    reqTime = cutOffPoint;
  }
  // at this point, we have the absolute timestamp in bootsecs.
  rep["time"] = reqTime + (Controller::systemBoot/1000); // fill the absolute timestamp

  unsigned int fields = 0;
  // next, figure out the fields wanted
  if (req.isMember("fields") && req["fields"].size()){
    jsonForEach(req["fields"], it){
      if ((*it).asStringRef() == "host"){fields |= STAT_CLI_HOST;}
      if ((*it).asStringRef() == "stream"){fields |= STAT_CLI_STREAM;}
      if ((*it).asStringRef() == "protocol"){fields |= STAT_CLI_PROTO;}
      if ((*it).asStringRef() == "conntime"){fields |= STAT_CLI_CONNTIME;}
      if ((*it).asStringRef() == "position"){fields |= STAT_CLI_POSITION;}
      if ((*it).asStringRef() == "down"){fields |= STAT_CLI_DOWN;}
      if ((*it).asStringRef() == "up"){fields |= STAT_CLI_UP;}
      if ((*it).asStringRef() == "downbps"){fields |= STAT_CLI_BPS_DOWN;}
      if ((*it).asStringRef() == "upbps"){fields |= STAT_CLI_BPS_UP;}
      if ((*it).asStringRef() == "sessid"){fields |= STAT_CLI_SESSID;}
      if ((*it).asStringRef() == "pktcount"){fields |= STAT_CLI_PKTCOUNT;}
      if ((*it).asStringRef() == "pktlost"){fields |= STAT_CLI_PKTLOST;}
      if ((*it).asStringRef() == "pktretransmit"){fields |= STAT_CLI_PKTRETRANSMIT;}
    }
  }
  // select all, if none selected
  if (!fields){fields = STAT_CLI_ALL;}
  // figure out what streams are wanted
  std::set<std::string> streams;
  if (req.isMember("streams") && req["streams"].size()){
    jsonForEach(req["streams"], it){streams.insert((*it).asStringRef());}
  }
  // figure out what protocols are wanted
  std::set<std::string> protos;
  if (req.isMember("protocols") && req["protocols"].size()){
    jsonForEach(req["protocols"], it){protos.insert((*it).asStringRef());}
  }
  // output the selected fields
  rep["fields"].null();
  if (fields & STAT_CLI_HOST){rep["fields"].append("host");}
  if (fields & STAT_CLI_STREAM){rep["fields"].append("stream");}
  if (fields & STAT_CLI_PROTO){rep["fields"].append("protocol");}
  if (fields & STAT_CLI_CONNTIME){rep["fields"].append("conntime");}
  if (fields & STAT_CLI_POSITION){rep["fields"].append("position");}
  if (fields & STAT_CLI_DOWN){rep["fields"].append("down");}
  if (fields & STAT_CLI_UP){rep["fields"].append("up");}
  if (fields & STAT_CLI_BPS_DOWN){rep["fields"].append("downbps");}
  if (fields & STAT_CLI_BPS_UP){rep["fields"].append("upbps");}
  if (fields & STAT_CLI_SESSID){rep["fields"].append("sessid");}
  if (fields & STAT_CLI_PKTCOUNT){rep["fields"].append("pktcount");}
  if (fields & STAT_CLI_PKTLOST){rep["fields"].append("pktlost");}
  if (fields & STAT_CLI_PKTRETRANSMIT){rep["fields"].append("pktretransmit");}
  // output the data itself
  rep["data"].null();
  // loop over all sessions
  if (sessions.size()){
    for (std::map<std::string, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
      unsigned long long time = reqTime;
      if (now && reqTime - it->second.getEnd() < 5){time = it->second.getEnd();}
      // data present and wanted? insert it!
      if ((it->second.getEnd() >= time && it->second.getStart() <= time) &&
          (!streams.size() || streams.count(it->second.getStreamName(time))) &&
          (!protos.size() || protos.count(it->second.getConnectors(time)))){
        const statLog & dta = it->second.curData.getDataFor(time);
        if (notEmpty(dta)){
          JSON::Value d;
          if (fields & STAT_CLI_HOST){d.append(it->second.getStrHost(time));}
          if (fields & STAT_CLI_STREAM){d.append(it->second.getStreamName(time));}
          if (fields & STAT_CLI_PROTO){d.append(it->second.getConnectors(time));}
          if (fields & STAT_CLI_CONNTIME){d.append(it->second.getConnTime(time));}
          if (fields & STAT_CLI_POSITION){d.append(it->second.getLastSecond(time));}
          if (fields & STAT_CLI_DOWN){d.append(it->second.getDown(time));}
          if (fields & STAT_CLI_UP){d.append(it->second.getUp(time));}
          if (fields & STAT_CLI_BPS_DOWN){d.append(it->second.getBpsDown(time));}
          if (fields & STAT_CLI_BPS_UP){d.append(it->second.getBpsUp(time));}
          if (fields & STAT_CLI_SESSID){d.append(it->second.getSessId());}
          if (fields & STAT_CLI_PKTCOUNT){d.append(it->second.getPktCount(time));}
          if (fields & STAT_CLI_PKTLOST){d.append(it->second.getPktLost(time));}
          if (fields & STAT_CLI_PKTRETRANSMIT){d.append(it->second.getPktRetransmit(time));}
          rep["data"].append(d);
        }
      }
    }
  }
  // all done! return is by reference, so no need to return anything here.
}

/// This takes a "active_streams" request, and fills in the response data.
///
/// \api
/// `"active_streams"` and `"stats_streams"` requests may either be empty, in which case the
/// response looks like this:
/// ~~~~~~~~~~~~~~~{.js}
/// [
///   //Array of stream names
///   "streamA",
///   "streamB",
///   "streamC"
/// ]
/// ~~~~~~~~~~~~~~~
/// `"stats_streams"` will list all streams that any statistics data is available for, and only
/// those. `"active_streams"` only lists streams that are currently active, and only those. If the
/// request is an array, which may contain any of the following elements:
/// ~~~~~~~~~~~~~~~{.js}
/// [
///   //Array of requested data types
///   "clients", //Current viewer count
///   "lastms" //Current position in the live buffer, if live
/// ]
/// ~~~~~~~~~~~~~~~
/// In which case the response is changed into this format:
/// ~~~~~~~~~~~~~~~{.js}
///{
///   //Object of stream names, containing arrays in the same order as the request, with the same
///   data "streamA":[
///     0,
///     60000
///   ]
///   "streamB":[
///      //....
///   ]
///   //...
///}
/// ~~~~~~~~~~~~~~~
/// All streams that any statistics data is available for are listed, and only those streams.
void Controller::fillHasStats(JSON::Value &req, JSON::Value &rep){
  // collect the data first
  std::set<std::string> streams;
  std::map<std::string, uint64_t> clients;
  // check all sessions
  {
    tthread::lock_guard<tthread::mutex> guard(statsMutex);
    if (sessions.size()){
      for (std::map<std::string, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
        if (it->second.getSessType() == SESS_INPUT){
          streams.insert(it->second.getStreamName());
        }else{
          streams.insert(it->second.getStreamName());
          if (it->second.getSessType() == SESS_VIEWER){clients[it->second.getStreamName()]++;}
        }
      }
    }
  }
  // Good, now output what we found...
  rep.null();
  for (std::set<std::string>::iterator it = streams.begin(); it != streams.end(); it++){
    if (req.isArray()){
      rep[*it].null();
      jsonForEach(req, j){
        if (j->asStringRef() == "clients"){rep[*it].append(clients[*it]);}
        if (j->asStringRef() == "lastms"){
          DTSC::Meta M(*it, false);
          if (M){
            uint64_t lms = 0;
            std::set<size_t> validTracks = M.getValidTracks();
            for (std::set<size_t>::iterator jt = validTracks.begin(); jt != validTracks.end(); jt++){
              if (M.getLastms(*jt) > lms){lms = M.getLastms(*jt);}
            }
            rep[*it].append(lms);
          }else{
            rep[*it].append(-1);
          }
        }
      }
    }else{
      rep.append(*it);
    }
  }
  // all done! return is by reference, so no need to return anything here.
}

void Controller::fillActive(JSON::Value &req, JSON::Value &rep){
  //check what values we wanted to receive
  JSON::Value fields;
  JSON::Value streams;
  bool objMode = false;
  bool longForm = false;
  if (req.isArray()){
    fields = req;
  }else if (req.isObject()){
    objMode = true;
    if (req.isMember("fields") && req["fields"].isArray()){
      fields = req["fields"];
    }
    if (req.isMember("streams") && req["streams"].isArray()){
      streams = req["streams"];
    }
    if (req.isMember("streams") && req["streams"].isString()){
      streams.append(req["streams"]);
    }
    if (req.isMember("stream") && req["stream"].isString()){
      streams.append(req["stream"]);
    }
    if (req.isMember("longform") && req["longform"].asBool()){
      longForm = true;
    }
    if (!fields.size()){
      fields.append("status");
      fields.append("viewers");
      fields.append("inputs");
      fields.append("outputs");
      fields.append("tracks");
      fields.append("views");
      fields.append("viewseconds");
      fields.append("upbytes");
      fields.append("downbytes");
      fields.append("packsent");
      fields.append("packloss");
      fields.append("packretrans");
      fields.append("firstms");
      fields.append("lastms");
      //fields.append("zerounix");
      fields.append("health");
    }
  }
  // collect the data first
  rep.null();
  if (objMode && !longForm){
    rep["fields"] = fields;
  }
  DTSC::Meta M;
  {
    tthread::lock_guard<tthread::mutex> guard(statsMutex);
    for (std::map<std::string, struct streamTotals>::iterator it = streamStats.begin(); it != streamStats.end(); ++it){
      //If specific streams were requested, match and skip non-matching
      if (streams.size()){
        bool match = false;
        jsonForEachConst(streams, s){
          if (!s->isString()){continue;}
          if (s->asStringRef() == it->first || (*(s->asStringRef().rbegin()) == '+' && it->first.substr(0, s->asStringRef().size()) == s->asStringRef())){
            match = true;
            break;
          }
        }
        if (!match){continue;}
      }
      if (!fields.size()){
        rep.append(it->first);
        continue;
      }
      JSON::Value & S = (objMode && !longForm) ? (rep["data"][it->first]) : (rep[it->first]);
      S.null();
      jsonForEachConst(fields, j){
        JSON::Value & F = longForm ? (S[j->asStringRef()]) : (S.append());
        if (j->asStringRef() == "clients"){
          F = it->second.currViews+it->second.currIns+it->second.currOuts;
        }else if (j->asStringRef() == "viewers"){
          F = it->second.currViews;
        }else if (j->asStringRef() == "inputs"){
          F = it->second.currIns;
        }else if (j->asStringRef() == "outputs"){
          F = it->second.currOuts;
        }else if (j->asStringRef() == "unspecified"){
          F = it->second.currUnspecified;
        }else if (j->asStringRef() == "views"){
          F = it->second.viewers;
        }else if (j->asStringRef() == "viewseconds"){
          F = it->second.viewSeconds;
        }else if (j->asStringRef() == "upbytes"){
          F = it->second.upBytes;
        }else if (j->asStringRef() == "downbytes"){
          F = it->second.downBytes;
        }else if (j->asStringRef() == "packsent"){
          F = it->second.packSent;
        }else if (j->asStringRef() == "packloss"){
          F = it->second.packLoss;
        }else if (j->asStringRef() == "packretrans"){
          F = it->second.packRetrans;
        }else if (j->asStringRef() == "firstms"){
          if (!M || M.getStreamName() != it->first){M.reInit(it->first, false);}
          if (M){
            uint64_t fms = 0;
            std::set<size_t> validTracks = M.getValidTracks();
            for (std::set<size_t>::iterator jt = validTracks.begin(); jt != validTracks.end(); jt++){
              if (M.getFirstms(*jt) < fms){fms = M.getFirstms(*jt);}
            }
            F = fms;
          }
        }else if (j->asStringRef() == "lastms"){
          if (!M || M.getStreamName() != it->first){M.reInit(it->first, false);}
          if (M){
            uint64_t lms = 0;
            std::set<size_t> validTracks = M.getValidTracks();
            for (std::set<size_t>::iterator jt = validTracks.begin(); jt != validTracks.end(); jt++){
              if (M.getLastms(*jt) > lms){lms = M.getLastms(*jt);}
            }
            F = lms;
          }
        }else if (j->asStringRef() == "zerounix"){
          if (!M || M.getStreamName() != it->first){M.reInit(it->first, false);}
          if (M && M.getLive()){
            F = (M.getBootMsOffset() + (Util::unixMS() - Util::bootMS())) / 1000;
          }
        }else if (j->asStringRef() == "health"){
          if (!M || M.getStreamName() != it->first){M.reInit(it->first, false);}
          if (M){M.getHealthJSON(F);}
        }else if (j->asStringRef() == "tracks"){
          if (!M || M.getStreamName() != it->first){M.reInit(it->first, false);}
          if (M){
            F = (uint64_t)M.getValidTracks().size();
          }
        }else if (j->asStringRef() == "status"){
          uint8_t ss = Util::getStreamStatus(it->first);
          switch (ss){
            case STRMSTAT_OFF: F = "Offline"; break;
            case STRMSTAT_INIT: F = "Initializing"; break;
            case STRMSTAT_BOOT: F = "Input booting"; break;
            case STRMSTAT_WAIT: F = "Waiting for data"; break;
            case STRMSTAT_READY: F = "Online"; break;
            case STRMSTAT_SHUTDOWN: F = "Shutting down"; break;
            default: F = "Invalid / Unknown"; break;
          }
        }
      }
    }
  }
  // all done! return is by reference, so no need to return anything here.
}

class totalsData{
public:
  totalsData(){
    clients = 0;
    inputs = 0;
    outputs = 0;
    unspecified = 0;
    downbps = 0;
    upbps = 0;
    pktCount = 0;
    pktLost = 0;
    pktRetransmit = 0;
  }
  void add(uint64_t down, uint64_t up, Controller::sessType sT, uint64_t pCount, uint64_t pLost, uint64_t pRetransmit){
    switch (sT){
    case Controller::SESS_VIEWER: clients++; break;
    case Controller::SESS_INPUT: inputs++; break;
    case Controller::SESS_OUTPUT: outputs++; break;
    case Controller::SESS_UNSPECIFIED: unspecified++; break;
    default: break;
    }
    downbps += down;
    upbps += up;
    pktCount += pCount;
    pktLost += pLost;
    pktRetransmit += pRetransmit;
  }
  uint64_t clients;
  uint64_t inputs;
  uint64_t outputs;
  uint64_t unspecified;
  uint64_t downbps;
  uint64_t upbps;
  uint64_t pktCount;
  uint64_t pktLost;
  uint64_t pktRetransmit;
};

/// This takes a "totals" request, and fills in the response data.
void Controller::fillTotals(JSON::Value &req, JSON::Value &rep){
  tthread::lock_guard<tthread::mutex> guard(statsMutex);
  // first, figure out the timestamps wanted
  int64_t reqStart = 0;
  int64_t reqEnd = 0;
  uint64_t epoch = Util::epoch();
  uint64_t bSecs = Util::bootSecs();
  if (req.isMember("start")){reqStart = req["start"].asInt();}
  if (req.isMember("end")){reqEnd = req["end"].asInt();}
  //if the reqStart or reqEnd is greater than current bootsecs, assume unix time and subtract epoch from it
  if (reqStart > (int64_t)epoch - STAT_CUTOFF){reqStart -= Controller::systemBoot/1000;}
  if (reqEnd > (int64_t)epoch - STAT_CUTOFF){reqEnd -= Controller::systemBoot/1000;}
  // add the current time, if negative or zero.
  if (reqStart < 0){reqStart += bSecs;}
  if (reqStart == 0){
    // Ensure cutOffPoint is either time of boot or 10 minutes ago, whichever is closer.
    // Prevents wrapping around to high values close to system boot time.
    uint64_t cutOffPoint = bSecs;
    if (cutOffPoint > STAT_CUTOFF){
      cutOffPoint -= STAT_CUTOFF;
    }else{
      cutOffPoint = 0;
    }
    reqStart = cutOffPoint;
  }
  if (reqEnd <= 0){reqEnd += bSecs;}
  // at this point, reqStart and reqEnd are the absolute timestamp in bootsecs.
  if (reqEnd < reqStart){reqEnd = reqStart;}
  if (reqEnd > bSecs){reqEnd = bSecs;}

  unsigned int fields = 0;
  // next, figure out the fields wanted
  if (req.isMember("fields") && req["fields"].size()){
    jsonForEach(req["fields"], it){
      if ((*it).asStringRef() == "clients"){fields |= STAT_TOT_CLIENTS;}
      if ((*it).asStringRef() == "inputs"){fields |= STAT_TOT_INPUTS;}
      if ((*it).asStringRef() == "outputs"){fields |= STAT_TOT_OUTPUTS;}
      if ((*it).asStringRef() == "downbps"){fields |= STAT_TOT_BPS_DOWN;}
      if ((*it).asStringRef() == "upbps"){fields |= STAT_TOT_BPS_UP;}
      if ((*it).asStringRef() == "perc_lost"){fields |= STAT_TOT_PERCLOST;}
      if ((*it).asStringRef() == "perc_retrans"){fields |= STAT_TOT_PERCRETRANS;}
    }
  }
  // select all, if none selected
  if (!fields){fields = STAT_TOT_ALL;}
  // figure out what streams are wanted
  std::set<std::string> streams;
  if (req.isMember("streams") && req["streams"].size()){
    jsonForEach(req["streams"], it){streams.insert((*it).asStringRef());}
  }
  // figure out what protocols are wanted
  std::set<std::string> protos;
  if (req.isMember("protocols") && req["protocols"].size()){
    jsonForEach(req["protocols"], it){protos.insert((*it).asStringRef());}
  }
  // output the selected fields
  rep["fields"].null();
  if (fields & STAT_TOT_CLIENTS){rep["fields"].append("clients");}
  if (fields & STAT_TOT_INPUTS){rep["fields"].append("inputs");}
  if (fields & STAT_TOT_OUTPUTS){rep["fields"].append("outputs");}
  if (fields & STAT_TOT_BPS_DOWN){rep["fields"].append("downbps");}
  if (fields & STAT_TOT_BPS_UP){rep["fields"].append("upbps");}
  if (fields & STAT_TOT_PERCLOST){rep["fields"].append("perc_lost");}
  if (fields & STAT_TOT_PERCRETRANS){rep["fields"].append("perc_retrans");}
  // start data collection
  std::map<uint64_t, totalsData> totalsCount;
  // loop over all sessions
  /// \todo Make the interval configurable instead of 1 second
  if (sessions.size()){
    for (std::map<std::string, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
      // data present and wanted? insert it!
      if ((it->second.getEnd() >= (unsigned long long)reqStart ||
           it->second.getStart() <= (unsigned long long)reqEnd) &&
          (!streams.size() || streams.count(it->second.getStreamName())) &&
          (!protos.size() || protos.count(it->second.getConnectors()))){
        for (unsigned long long i = reqStart; i <= reqEnd; ++i){
          if (it->second.hasDataFor(i)){
            totalsCount[i].add(it->second.getBpsDown(i), it->second.getBpsUp(i), it->second.getSessType(), it->second.getPktCount(), it->second.getPktLost(), it->second.getPktRetransmit());
          }
        }
      }
    }
  }
  // output the data itself
  if (!totalsCount.size()){
    // Oh noes! No data. We'll just reply with a bunch of nulls.
    rep["start"].null();
    rep["end"].null();
    rep["data"].null();
    rep["interval"].null();
    return;
  }
  // yay! We have data!
  rep["start"] = totalsCount.begin()->first + (Controller::systemBoot/1000);
  rep["end"] = totalsCount.rbegin()->first + (Controller::systemBoot/1000);
  rep["data"].null();
  rep["interval"].null();
  uint64_t prevT = 0;
  JSON::Value i;
  for (std::map<uint64_t, totalsData>::iterator it = totalsCount.begin(); it != totalsCount.end(); it++){
    JSON::Value d;
    if (fields & STAT_TOT_CLIENTS){d.append(it->second.clients);}
    if (fields & STAT_TOT_INPUTS){d.append(it->second.inputs);}
    if (fields & STAT_TOT_OUTPUTS){d.append(it->second.outputs);}
    if (fields & STAT_TOT_BPS_DOWN){d.append(it->second.downbps);}
    if (fields & STAT_TOT_BPS_UP){d.append(it->second.upbps);}
    if (fields & STAT_TOT_PERCLOST){
      if (it->second.pktCount > 0){
        d.append((it->second.pktLost*100)/it->second.pktCount);
      }else{
        d.append(0);
      }
    }
    if (fields & STAT_TOT_PERCRETRANS){
      if (it->second.pktCount > 0){
        d.append((it->second.pktRetransmit*100)/it->second.pktCount);
      }else{
        d.append(0);
      }
    }
    rep["data"].append(d);
    if (prevT){
      if (i.size() < 2){
        i.append(1u);
        i.append(it->first - prevT);
      }else{
        if (i[1u].asInt() != it->first - prevT){
          rep["interval"].append(i);
          i[0u] = 1u;
          i[1u] = it->first - prevT;
        }else{
          i[0u] = i[0u].asInt() + 1;
        }
      }
    }
    prevT = it->first;
  }
  if (i.size() > 1){
    rep["interval"].append(i);
    i.null();
  }
  // all done! return is by reference, so no need to return anything here.
}

void Controller::handlePrometheus(HTTP::Parser &H, Socket::Connection &conn, int mode){
  std::string jsonp;
  switch (mode){
  case PROMETHEUS_TEXT: H.SetHeader("Content-Type", "text/plain; version=0.0.4"); break;
  case PROMETHEUS_JSON:
    H.SetHeader("Content-Type", "text/json");
    H.setCORSHeaders();
    if (H.GetVar("callback") != ""){jsonp = H.GetVar("callback");}
    if (H.GetVar("jsonp") != ""){jsonp = H.GetVar("jsonp");}
    break;
  }
  H.SetHeader("Server", APPIDENT);
  H.StartResponse("200", "OK", H, conn, true);

  // Counters of current active viewers, inputs and outputs of the Session stats cache
  std::map<std::string, uint32_t> outputs;
  uint32_t totViewers = 0;
  uint32_t totInputs = 0;
  uint32_t totOutputs = 0;
  uint32_t totUnspecified = 0;
  for (uint64_t idx = 0; idx < statComm.recordCount(); idx++){
    if (statComm.getStatus(idx) == COMM_STATUS_INVALID || statComm.getStatus(idx) & COMM_STATUS_DISCONNECT){continue;}
    const std::string thisSessId = statComm.getSessId(idx);
    // Count active viewers, inputs, outputs and protocols
    if (thisSessId[0] == 'I'){
      totInputs++;
    }else if (thisSessId[0] == 'U'){
      totUnspecified++;
    }else if (thisSessId[0] == 'O'){
      totOutputs++;
    }else{
      totViewers++;
      outputs[statComm.getConnector(idx)]++;
    }
  }

  // Collect core server stats
  uint64_t mem_total = 0, mem_free = 0, mem_bufcache = 0;
  uint64_t bw_up_total = 0, bw_down_total = 0;
  {
    std::ifstream meminfo("/proc/meminfo");
    if (meminfo){
      char line[300];
      while (meminfo.good()){
        meminfo.getline(line, 300);
        if (meminfo.fail()){
          // empty lines? ignore them, clear flags, continue
          if (!meminfo.eof()){
            meminfo.ignore();
            meminfo.clear();
          }
          continue;
        }
        long long int i;
        if (sscanf(line, "MemTotal : %lli kB", &i) == 1){mem_total = i;}
        if (sscanf(line, "MemFree : %lli kB", &i) == 1){mem_free = i;}
        if (sscanf(line, "Buffers : %lli kB", &i) == 1){mem_bufcache += i;}
        if (sscanf(line, "Cached : %lli kB", &i) == 1){mem_bufcache += i;}
      }
    }
    std::ifstream netUsage("/proc/net/dev");
    while (netUsage){
      char line[300];
      netUsage.getline(line, 300);
      long long unsigned sent = 0;
      long long unsigned recv = 0;
      char iface[10];
      if (sscanf(line, "%9s %llu %*u %*u %*u %*u %*u %*u %*u %llu", iface, &recv, &sent) == 3){
        if (iface[0] != 'l' || iface[1] != 'o'){
          bw_down_total += recv;
          bw_up_total += sent;
        }
      }
    }
  }
  uint64_t shm_total = 0, shm_free = 0;
#if !defined(__CYGWIN__) && !defined(_WIN32)
  {
    struct statvfs shmd;
    IPC::sharedPage tmpCapa(SHM_CAPA, DEFAULT_CONF_PAGE_SIZE, false, false);
    if (tmpCapa.mapped && tmpCapa.handle){
      fstatvfs(tmpCapa.handle, &shmd);
      shm_free = (shmd.f_bfree * shmd.f_frsize) / 1024;
      shm_total = (shmd.f_blocks * shmd.f_frsize) / 1024;
    }
  }
#endif

  if (mode == PROMETHEUS_TEXT){
    std::stringstream response;
    response << "# HELP mist_logs Count of log messages since server start.\n";
    response << "# TYPE mist_logs counter\n";
    response << "mist_logs " << Controller::logCounter << "\n\n";
    response << "# HELP mist_cpu Total CPU usage in tenths of percent.\n";
    response << "# TYPE mist_cpu gauge\n";
    response << "mist_cpu " << cpu_use << "\n\n";
    response << "# HELP mist_mem_total Total memory available in KiB.\n";
    response << "# TYPE mist_mem_total gauge\n";
    response << "mist_mem_total " << mem_total << "\n\n";
    response << "# HELP mist_mem_used Total memory in use in KiB.\n";
    response << "# TYPE mist_mem_used gauge\n";
    response << "mist_mem_used " << (mem_total - mem_free - mem_bufcache) << "\n\n";
    response << "# HELP mist_shm_total Total shared memory available in KiB.\n";
    response << "# TYPE mist_shm_total gauge\n";
    response << "mist_shm_total " << shm_total << "\n\n";
    response << "# HELP mist_shm_used Total shared memory in use in KiB.\n";
    response << "# TYPE mist_shm_used gauge\n";
    response << "mist_shm_used " << (shm_total - shm_free) << "\n\n";

    response << "# HELP mist_viewseconds_total Number of seconds any media was received by a viewer.\n";
    response << "# TYPE mist_viewseconds_total counter\n";
    response << "mist_viewseconds_total " << servSeconds + viewSecondsTotal << "\n";

    response << "\n# HELP mist_sessions_count Counts of unique sessions by type since server "
                "start.\n";
    response << "# TYPE mist_sessions_count counter\n";
    response << "mist_sessions_count{sessType=\"viewers\"}" << servViewers << "\n";
    response << "mist_sessions_count{sessType=\"incoming\"}" << servInputs << "\n";
    response << "mist_sessions_count{sessType=\"unspecified\"}" << servUnspecified << "\n";
    response << "mist_sessions_count{sessType=\"outgoing\"}" << servOutputs << "\n\n";

    response << "# HELP mist_bw_total Count of bytes handled since server start, by direction.\n";
    response << "# TYPE mist_bw_total counter\n";
    response << "stat_bw_total{direction=\"up\"}" << bw_up_total << "\n";
    response << "stat_bw_total{direction=\"down\"}" << bw_down_total << "\n\n";
    response << "mist_bw_total{direction=\"up\"}" << servUpBytes << "\n";
    response << "mist_bw_total{direction=\"down\"}" << servDownBytes << "\n\n";
    response << "mist_bw_other{direction=\"up\"}" << servUpOtherBytes << "\n";
    response << "mist_bw_other{direction=\"down\"}" << servDownOtherBytes << "\n\n";
    response << "mist_bw_limit " << bwLimit << "\n\n";

    response << "# HELP mist_packets_total Total number of packets sent/received/lost over lossy protocols, server-wide.\n";
    response << "# TYPE mist_packets_total counter\n";
    response << "mist_packets_total{pkttype=\"sent\"}" << servPackSent << "\n";
    response << "mist_packets_total{pkttype=\"lost\"}" << servPackLoss << "\n";
    response << "mist_packets_total{pkttype=\"retrans\"}" << servPackRetrans << "\n";

    if (outputs.size()){
      response << "# HELP mist_outputs Number of viewers active right now, server-wide, by output type.\n";
      response << "# TYPE mist_outputs gauge\n";
      for (std::map<std::string, uint32_t>::iterator it = outputs.begin(); it != outputs.end(); ++it){
        response << "mist_outputs{output=\"" << it->first << "\"}" << it->second << "\n";
      }
      response << "\n";
    }

    {// Scope for shortest possible blocking of statsMutex
      tthread::lock_guard<tthread::mutex> guard(statsMutex);

      response << "# HELP mist_sessions_total Number of sessions active right now, server-wide, by type.\n";
      response << "# TYPE mist_sessions_total gauge\n";
      response << "mist_sessions_total{sessType=\"viewers\"}" << totViewers << "\n";
      response << "mist_sessions_total{sessType=\"incoming\"}" << totInputs << "\n";
      response << "mist_sessions_total{sessType=\"outgoing\"}" << totOutputs << "\n";
      response << "mist_sessions_total{sessType=\"unspecified\"}" << totUnspecified << "\n";
      response << "mist_sessions_total{sessType=\"cached\"}" << sessions.size() << "\n";

      response << "\n# HELP mist_viewcount Count of unique viewer sessions since stream start, per "
                  "stream.\n";
      response << "# TYPE mist_viewcount counter\n";
      response << "# HELP mist_viewseconds Number of seconds any media was received by a viewer.\n";
      response << "# TYPE mist_viewseconds counter\n";
      response << "# HELP mist_bw Count of bytes handled since stream start, by direction.\n";
      response << "# TYPE mist_bw counter\n";
      response << "# HELP mist_packets Total number of packets sent/received/lost over lossy protocols.\n";
      response << "# TYPE mist_packets counter\n";
      for (std::map<std::string, struct streamTotals>::iterator it = streamStats.begin();
            it != streamStats.end(); ++it){
        response << "mist_sessions{stream=\"" << it->first << "\",sessType=\"viewers\"}"
                  << it->second.currViews << "\n";
        response << "mist_sessions{stream=\"" << it->first << "\",sessType=\"incoming\"}"
                  << it->second.currIns << "\n";
        response << "mist_sessions{stream=\"" << it->first << "\",sessType=\"outgoing\"}"
                  << it->second.currOuts << "\n";
        response << "mist_sessions{stream=\"" << it->first << "\",sessType=\"unspecified\"}"
                  << it->second.currUnspecified << "\n";
        response << "mist_viewcount{stream=\"" << it->first << "\"}" << it->second.viewers << "\n";
        response << "mist_viewseconds{stream=\"" << it->first << "\"} " << it->second.viewSeconds << "\n";
        response << "mist_bw{stream=\"" << it->first << "\",direction=\"up\"}" << it->second.upBytes << "\n";
        response << "mist_bw{stream=\"" << it->first << "\",direction=\"down\"}" << it->second.downBytes << "\n";
        response << "mist_packets{stream=\"" << it->first << "\",pkttype=\"sent\"}" << it->second.packSent << "\n";
        response << "mist_packets{stream=\"" << it->first << "\",pkttype=\"lost\"}" << it->second.packLoss << "\n";
        response << "mist_packets{stream=\"" << it->first << "\",pkttype=\"retrans\"}" << it->second.packRetrans << "\n";
      }

      if (Controller::triggerStats.size()){
        response << "\n# HELP mist_trigger_count Total executions for the given trigger\n";
        response << "# HELP mist_trigger_time Total execution time in millis for the given trigger\n";
        response << "# HELP mist_trigger_fails Total failed executions for the given trigger\n";
        for (std::map<std::string, Controller::triggerLog>::iterator it = Controller::triggerStats.begin();
            it != Controller::triggerStats.end(); it++){
          response << "mist_trigger_count{trigger=\"" << it->first << "\"}" << it->second.totalCount << "\n";
          response << "mist_trigger_time{trigger=\"" << it->first << "\"}" << it->second.ms << "\n";
          response << "mist_trigger_fails{trigger=\"" << it->first << "\"}" << it->second.failCount << "\n";
        }
        response << "\n";
      }
    }
    H.Chunkify(response.str(), conn);
  }
  if (mode == PROMETHEUS_JSON){
    JSON::Value resp;
    resp["cpu"] = cpu_use;
    resp["mem_total"] = mem_total;
    resp["mem_used"] = (mem_total - mem_free - mem_bufcache);
    resp["shm_total"] = shm_total;
    resp["shm_used"] = (shm_total - shm_free);
    resp["logs"] = Controller::logCounter;
    resp["curr"].append(totViewers);
    resp["curr"].append(totInputs);
    resp["curr"].append(totOutputs);
    resp["curr"].append(totUnspecified);
    resp["tot"].append(servViewers);
    resp["tot"].append(servInputs);
    resp["tot"].append(servOutputs);
    resp["tot"].append(servUnspecified);
    resp["st"].append(bw_up_total);
    resp["st"].append(bw_down_total);
    resp["bw"].append(servUpBytes);
    resp["bw"].append(servDownBytes);
    resp["pkts"].append(servPackSent);
    resp["pkts"].append(servPackLoss);
    resp["pkts"].append(servPackRetrans);
    resp["bwlimit"] = bwLimit;
    {// Scope for shortest possible blocking of statsMutex
      tthread::lock_guard<tthread::mutex> guard(statsMutex);
      resp["curr"].append((uint64_t)sessions.size());

      if (Controller::triggerStats.size()){
        for (std::map<std::string, Controller::triggerLog>::iterator it = Controller::triggerStats.begin();
            it != Controller::triggerStats.end(); it++){
          JSON::Value &tVal = resp["triggers"][it->first];
          tVal["count"] = it->second.totalCount;
          tVal["ms"] = it->second.ms;
          tVal["fails"] = it->second.failCount;
        }
      }
      if (Storage["config"].isMember("location") && Storage["config"]["location"].isMember("lat") && Storage["config"]["location"].isMember("lon")){
        resp["loc"]["lat"] = Storage["config"]["location"]["lat"].asDouble();
        resp["loc"]["lon"] = Storage["config"]["location"]["lon"].asDouble();
        if (Storage["config"]["location"].isMember("name")){
          resp["loc"]["name"] = Storage["config"]["location"]["name"].asStringRef();
        }
      }
      resp["obw"].append(servUpOtherBytes);
      resp["obw"].append(servDownOtherBytes);

      for (std::map<std::string, struct streamTotals>::iterator it = streamStats.begin();
           it != streamStats.end(); ++it){
        resp["streams"][it->first]["tot"].append(it->second.viewers);
        resp["streams"][it->first]["tot"].append(it->second.inputs);
        resp["streams"][it->first]["tot"].append(it->second.outputs);
        resp["streams"][it->first]["bw"].append(it->second.upBytes);
        resp["streams"][it->first]["bw"].append(it->second.downBytes);
        resp["streams"][it->first]["curr"].append(it->second.currViews);
        resp["streams"][it->first]["curr"].append(it->second.currIns);
        resp["streams"][it->first]["curr"].append(it->second.currOuts);
        resp["streams"][it->first]["curr"].append(it->second.currUnspecified);
        resp["streams"][it->first]["pkts"].append(it->second.packSent);
        resp["streams"][it->first]["pkts"].append(it->second.packLoss);
        resp["streams"][it->first]["pkts"].append(it->second.packRetrans);
      }
      for (std::map<std::string, uint32_t>::iterator it = outputs.begin(); it != outputs.end(); ++it){
        resp["output_counts"][it->first] = it->second;
      }
    }

    jsonForEach(Storage["streams"], sIt){resp["conf_streams"].append(sIt.key());}

    {
      tthread::lock_guard<tthread::mutex> guard(Controller::configMutex);
      // add tags, if any
      if (Storage.isMember("tags") && Storage["tags"].isArray() && Storage["tags"].size()){resp["tags"] = Storage["tags"];}
      // Loop over connectors
      const JSON::Value &caps = capabilities["connectors"];
      jsonForEachConst(Storage["config"]["protocols"], prtcl){
        if (!(*prtcl).isMember("connector")){continue;}
        const std::string &cName = (*prtcl)["connector"].asStringRef();
        if (!(*prtcl).isMember("online") || (*prtcl)["online"].asInt() != 1){continue;}
        if (!caps.isMember(cName)){continue;}
        const JSON::Value &capa = caps[cName];
        if (!capa.isMember("optional") || !capa["optional"].isMember("port")){continue;}
        // We now know it's configured, online and has a listening port
        HTTP::URL outURL("HOST");
        // get the default port if none is set
        if (prtcl->isMember("port")){outURL.port = (*prtcl)["port"].asString();}
        if (!outURL.port.size()){outURL.port = capa["optional"]["port"]["default"].asString();}
        // set the protocol
        if (capa.isMember("protocol")){
          outURL.protocol = capa["protocol"].asString();
        }else{
          if (capa.isMember("methods") && capa["methods"][0u].isMember("handler")){
            outURL.protocol = capa["methods"][0u]["handler"].asStringRef();
          }
        }
        if (outURL.protocol.find(':') != std::string::npos){
          outURL.protocol.erase(outURL.protocol.find(':'));
        }
        // set the public access, if needed
        if (prtcl->isMember("pubaddr") && (*prtcl)["pubaddr"].asString().size()){
          HTTP::URL altURL((*prtcl)["pubaddr"].asString());
          outURL.protocol = altURL.protocol;
          if (altURL.host.size()){outURL.host = altURL.host;}
          outURL.port = altURL.port;
          outURL.path = altURL.path;
        }
        // Add the URL, if present
        if (capa.isMember("url_rel")){
          resp["outputs"][cName] = outURL.link("./" + capa["url_rel"].asStringRef()).getUrl();
        }

        // if this connector can be depended upon by other connectors, loop over the rest
        if (capa.isMember("provides")){
          const std::string &cProv = capa["provides"].asStringRef();
          jsonForEachConst(Storage["config"]["protocols"], chld){
            const std::string &child = (*chld)["connector"].asStringRef();
            if (!caps.isMember(child) || !caps[child].isMember("deps")){continue;}
            if (caps[child].isMember("deps") && caps[child]["deps"].asStringRef() == cProv &&
                caps[child].isMember("url_rel")){
              resp["outputs"][child] = outURL.link("./" + caps[child]["url_rel"].asStringRef()).getUrl();
            }
          }
        }
      }
    }

    if (jsonp.size()){H.Chunkify(jsonp + "(", conn);}
    H.Chunkify(resp.toString(), conn);
    if (jsonp.size()){H.Chunkify(");\n", conn);}
  }

  H.Chunkify("", conn);
  H.Clean();
}
