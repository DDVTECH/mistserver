#include <cstdio>
#include <list>
#include <fstream>
#include <sys/statvfs.h>//for fstatvfs
#include <mist/config.h>
#include <mist/shared_memory.h>
#include <mist/dtsc.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include "controller_statistics.h"
#include "controller_limits.h"
#include "controller_push.h"
#include "controller_storage.h"
#include "controller_capabilities.h"

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
#define STAT_CLI_CRC 512
#define STAT_CLI_ALL 0xFFFF
// These are used to store "totals" field requests in a bitfield for speedup.
#define STAT_TOT_CLIENTS 1
#define STAT_TOT_BPS_DOWN 2
#define STAT_TOT_BPS_UP 4
#define STAT_TOT_INPUTS 8
#define STAT_TOT_OUTPUTS 16
#define STAT_TOT_ALL 0xFF

#define COUNTABLE_BYTES 128*1024


std::map<Controller::sessIndex, Controller::statSession> Controller::sessions; ///< list of sessions that have statistics data available
std::map<unsigned long, Controller::sessIndex> Controller::connToSession; ///< Map of socket IDs to session info.
bool Controller::killOnExit = KILL_ON_EXIT;
tthread::mutex Controller::statsMutex;
unsigned int Controller::maxConnsPerIP = 0;
char noBWCountMatches[1717];
uint64_t bwLimit = 128*1024*1024;//gigabit default limit

/// Session cache shared memory page
IPC::sharedPage * shmSessions = 0;
/// Lock for the session cache shared memory page
IPC::semaphore * cacheLock = 0;

/// Convert bandwidth config into memory format
void Controller::updateBandwidthConfig(){
  size_t offset = 0;
  bwLimit = 128*1024*1024;//gigabit default limit
  memset(noBWCountMatches, 0, 1717);
  if (Storage.isMember("bandwidth")){
    if (Storage["bandwidth"].isMember("limit")){
      bwLimit = Storage["bandwidth"]["limit"].asInt();
    }
    if (Storage["bandwidth"].isMember("exceptions")){
      jsonForEach(Storage["bandwidth"]["exceptions"], j){
        std::string newbins = Socket::getBinForms(j->asStringRef());
        if (offset + newbins.size() < 1700){
          memcpy(noBWCountMatches+offset, newbins.data(), newbins.size());
          offset += newbins.size();
        }
      }
    }
  }
}

//For server-wide totals. Local to this file only.
struct streamTotals {
  unsigned long long upBytes;
  unsigned long long downBytes;
  unsigned long long inputs;
  unsigned long long outputs;
  unsigned long long viewers;
  unsigned long long currIns;
  unsigned long long currOuts;
  unsigned long long currViews;
  uint8_t status;
};
static std::map<std::string, struct streamTotals> streamStats;
static unsigned long long servUpBytes = 0;
static unsigned long long servDownBytes = 0;
static unsigned long long servUpOtherBytes = 0;
static unsigned long long servDownOtherBytes = 0;
static unsigned long long servInputs = 0;
static unsigned long long servOutputs = 0;
static unsigned long long servViewers = 0;

Controller::sessIndex::sessIndex(std::string dhost, unsigned int dcrc, std::string dstreamName, std::string dconnector){
  ID = "UNSET";
  host = dhost;
  crc = dcrc;
  streamName = dstreamName;
  connector = dconnector;
}

Controller::sessIndex::sessIndex(){
  crc = 0;
}

std::string Controller::sessIndex::toStr(){
  std::stringstream s;
  s << ID << "(" << host << " " << crc << " " << streamName << " " << connector << ")";
  return s.str();
}

/// Initializes a sessIndex from a statExchange object, converting binary format IP addresses into strings.
/// This extracts the host, stream name, connector and crc field, ignoring everything else.
Controller::sessIndex::sessIndex(IPC::statExchange & data){
  Socket::hostBytesToStr(data.host().c_str(), 16, host);
  streamName = data.streamName();
  connector = data.connector();
  crc = data.crc();
  ID = data.getSessId();
}


bool Controller::sessIndex::operator== (const Controller::sessIndex &b) const{
  return (host == b.host && crc == b.crc && streamName == b.streamName && connector == b.connector);
}

bool Controller::sessIndex::operator!= (const Controller::sessIndex &b) const{
  return !(*this == b);
}

bool Controller::sessIndex::operator> (const Controller::sessIndex &b) const{
  return host > b.host || (host == b.host && (crc > b.crc || (crc == b.crc && (streamName > b.streamName || (streamName == b.streamName && connector > b.connector)))));
}

bool Controller::sessIndex::operator< (const Controller::sessIndex &b) const{
  return host < b.host || (host == b.host && (crc < b.crc || (crc == b.crc && (streamName < b.streamName || (streamName == b.streamName && connector < b.connector)))));
}

bool Controller::sessIndex::operator<= (const Controller::sessIndex &b) const{
  return !(*this > b);
}

bool Controller::sessIndex::operator>= (const Controller::sessIndex &b) const{
  return !(*this < b);
}

///This function is ran whenever a stream becomes active.
void Controller::streamStarted(std::string stream){
  INFO_MSG("Stream %s became active", stream.c_str());
  Controller::doAutoPush(stream);
}

///This function is ran whenever a stream becomes active.
void Controller::streamStopped(std::string stream){
  INFO_MSG("Stream %s became inactive", stream.c_str());
}

/// \todo Make this prettier.
IPC::sharedServer * statPointer = 0;

///Invalidates all current sessions for the given streamname
///Updates the session cache, afterwards.
void Controller::sessions_invalidate(const std::string & streamname){
  if (!statPointer){
    FAIL_MSG("In shutdown procedure - cannot invalidate sessions.");
    return;
  }
  if (cacheLock){cacheLock->wait();}
  unsigned int invalidated = 0;
  unsigned int sessCount = 0;
  tthread::lock_guard<tthread::mutex> guard(statsMutex);
  for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
    if (it->first.streamName == streamname){
      sessCount++;
      invalidated += it->second.invalidate();
    }
  }
  Controller::writeSessionCache();
  if (cacheLock){cacheLock->post();}
  INFO_MSG("Invalidated %u connections in %u sessions for stream %s", invalidated, sessCount, streamname.c_str());
}


///Shuts down all current sessions for the given streamname
///Updates the session cache, afterwards. (if any action was taken)
void Controller::sessions_shutdown(JSON::Iter & i){
  if (i->isArray() || i->isObject()){
    jsonForEach(*i, it){
      sessions_shutdown(it);
    }
    return;
  }
  if (i->isString()){
    sessions_shutdown(i.key(), i->asStringRef());
    return;
  }
  //not handled, ignore
}

///Shuts down the given session
///Updates the session cache, afterwards.
void Controller::sessId_shutdown(const std::string & sessId){
  if (!statPointer){
    FAIL_MSG("In controller shutdown procedure - cannot shutdown sessions.");
    return;
  }
  if (cacheLock){cacheLock->wait();}
  unsigned int murdered = 0;
  unsigned int sessCount = 0;
  tthread::lock_guard<tthread::mutex> guard(statsMutex);
  for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
    if (it->first.ID == sessId){
      sessCount++;
      murdered += it->second.kill();
      break;
    }
  }
  Controller::writeSessionCache();
  if (cacheLock){cacheLock->post();}
  INFO_MSG("Shut down %u connections in %u session(s) for ID %s", murdered, sessCount, sessId.c_str());
}

///Tags the given session
void Controller::sessId_tag(const std::string & sessId, const std::string & tag){
  if (!statPointer){
    FAIL_MSG("In controller shutdown procedure - cannot tag sessions.");
    return;
  }
  tthread::lock_guard<tthread::mutex> guard(statsMutex);
  for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
    if (it->first.ID == sessId){
      it->second.tags.insert(tag);
      return;
    }
  }
  if (tag.substr(0, 3) != "UA:"){
    WARN_MSG("Session %s not found - cannot tag with %s", sessId.c_str(), tag.c_str());
  }
}

///Shuts down sessions with the given tag set
///Updates the session cache, afterwards.
void Controller::tag_shutdown(const std::string & tag){
  if (!statPointer){
    FAIL_MSG("In controller shutdown procedure - cannot shutdown sessions.");
    return;
  }
  if (cacheLock){cacheLock->wait();}
  unsigned int murdered = 0;
  unsigned int sessCount = 0;
  tthread::lock_guard<tthread::mutex> guard(statsMutex);
  for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
    if (it->second.tags.count(tag)){
      sessCount++;
      murdered += it->second.kill();
    }
  }
  Controller::writeSessionCache();
  if (cacheLock){cacheLock->post();}
  INFO_MSG("Shut down %u connections in %u session(s) for tag %s", murdered, sessCount, tag.c_str());
}

///Shuts down all current sessions for the given streamname
///Updates the session cache, afterwards.
void Controller::sessions_shutdown(const std::string & streamname, const std::string & protocol){
  if (!statPointer){
    FAIL_MSG("In controller shutdown procedure - cannot shutdown sessions.");
    return;
  }
  if (cacheLock){cacheLock->wait();}
  unsigned int murdered = 0;
  unsigned int sessCount = 0;
  tthread::lock_guard<tthread::mutex> guard(statsMutex);
  for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
    if ((!streamname.size() || it->first.streamName == streamname) && (!protocol.size() || it->first.connector == protocol)){
      sessCount++;
      murdered += it->second.kill();
    }
  }
  Controller::writeSessionCache();
  if (cacheLock){cacheLock->post();}
  INFO_MSG("Shut down %u connections in %u sessions for stream %s/%s", murdered, sessCount, streamname.c_str(), protocol.c_str());
}

/// Writes the session cache to shared memory.
/// Assumes the config mutex, stats mutex and session cache semaphore are already locked.
/// Does nothing if the session cache could not be initialized on the first try
/// Does no error checking after first open attempt (fails silently)!
void Controller::writeSessionCache(){
  uint32_t shmOffset = 0;
  if (shmSessions && shmSessions->mapped){
    if (sessions.size()){
      for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
        if (it->second.hasData()){
          //store an entry in the shmSessions page, if it fits
          if (it->second.sync > 2 && shmOffset + SHM_SESSIONS_ITEM < SHM_SESSIONS_SIZE){
            *((uint32_t*)(shmSessions->mapped+shmOffset)) = it->first.crc;
            strncpy(shmSessions->mapped+shmOffset+4, it->first.streamName.c_str(), 100);
            strncpy(shmSessions->mapped+shmOffset+104, it->first.connector.c_str(), 20);
            strncpy(shmSessions->mapped+shmOffset+124, it->first.host.c_str(), 40);
            shmSessions->mapped[shmOffset+164] = it->second.sync;
            shmOffset += SHM_SESSIONS_ITEM;
          }
        }
      }
    }
    //set a final shmSessions entry to all zeroes
    memset(shmSessions->mapped+shmOffset, 0, SHM_SESSIONS_ITEM);
  }
}

/// This function runs as a thread and roughly once per second retrieves
/// statistics from all connected clients, as well as wipes
/// old statistics that have disconnected over 10 minutes ago.
void Controller::SharedMemStats(void * config){
  DEBUG_MSG(DLVL_HIGH, "Starting stats thread");
  IPC::sharedServer statServer(SHM_STATISTICS, STAT_EX_SIZE, true);
  statPointer = &statServer;
  shmSessions = new IPC::sharedPage(SHM_SESSIONS, SHM_SESSIONS_SIZE, true);
  cacheLock = new IPC::semaphore(SEM_SESSCACHE, O_CREAT | O_RDWR, ACCESSPERMS, 1);
  cacheLock->unlink();
  cacheLock->open(SEM_SESSCACHE, O_CREAT | O_RDWR, ACCESSPERMS, 1);
  std::set<std::string> inactiveStreams;
  Controller::initState();
  bool shiftWrites = true;
  while(((Util::Config*)config)->is_active){
    {
      tthread::lock_guard<tthread::mutex> guard(Controller::configMutex);
      tthread::lock_guard<tthread::mutex> guard2(statsMutex);
      cacheLock->wait(); /*LTS*/
      //parse current users
      statServer.parseEach(parseStatistics);
      //wipe old statistics
      if (sessions.size()){
        std::list<sessIndex> mustWipe;
        unsigned long long cutOffPoint = Util::epoch() - STAT_CUTOFF;
        unsigned long long disconnectPointIn = Util::epoch() - STATS_INPUT_DELAY;
        unsigned long long disconnectPointOut = Util::epoch() - STATS_DELAY;
        for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
          unsigned long long dPoint = it->second.getSessType() == SESS_INPUT ? disconnectPointIn : disconnectPointOut; 
          it->second.ping(it->first, dPoint);
          if (it->second.sync == 100){
            it->second.wipeOld(dPoint);
          }else{
            it->second.wipeOld(cutOffPoint);
          }
          if (!it->second.hasData()){
            mustWipe.push_back(it->first);
          }
        }
        while (mustWipe.size()){
          sessions.erase(mustWipe.front());
          mustWipe.pop_front();
        }
      }
      Util::RelAccX * strmStats = streamsAccessor();
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
        for (std::map<std::string, struct streamTotals>::iterator it = streamStats.begin(); it != streamStats.end(); ++it){
          uint8_t newState = Util::getStreamStatus(it->first);
          uint8_t oldState = it->second.status;
          if (newState != oldState){
            it->second.status = newState;
            if (newState == STRMSTAT_READY){
              streamStarted(it->first);
            }else{
              if (oldState == STRMSTAT_READY){
                streamStopped(it->first);
              }
            }
          }
          if (newState == STRMSTAT_OFF){
            inactiveStreams.insert(it->first);
          }
          if (strmStats){
            if (shiftWrites){
              strmStats->setString("stream", it->first, strmPos);
            }
            strmStats->setInt("status", it->second.status, strmPos);
            strmStats->setInt("viewers", it->second.currViews, strmPos);
            strmStats->setInt("inputs", it->second.currIns, strmPos);
            strmStats->setInt("outputs", it->second.currOuts, strmPos);
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
        streamStats.erase(*inactiveStreams.begin());
        inactiveStreams.erase(inactiveStreams.begin());
        shiftWrites = true;
      }
      /*LTS-START*/
      Controller::writeSessionCache();
      Controller::checkServerLimits();
      cacheLock->post();
      /*LTS-END*/
    }
    Util::wait(1000);
  }
  statPointer = 0;
  HIGH_MSG("Stopping stats thread");
  if (Controller::restarting){
    statServer.abandon();
    shmSessions->master = false;
  }else{/*LTS-START*/
    if (Controller::killOnExit){
      DEBUG_MSG(DLVL_WARN, "Killing all connected clients to force full shutdown");
      statServer.finishEach();
    }
    /*LTS-END*/
  }
  Controller::deinitState(Controller::restarting);
  delete shmSessions;
  shmSessions = 0;
  delete cacheLock;
  cacheLock = 0;
}

/// Gets a complete list of all streams currently in active state, with optional prefix matching
std::set<std::string> Controller::getActiveStreams(const std::string & prefix){
  std::set<std::string> ret;
  Util::RelAccX * strmStats = streamsAccessor();
  if (!strmStats || !strmStats->isReady()){return ret;}
  uint64_t endPos = strmStats->getEndPos();
  if (prefix.size()){
    for (uint64_t i = strmStats->getDeleted(); i < endPos; ++i){
      if (strmStats->getInt("status", i) != STRMSTAT_READY){continue;}
      const char * S = strmStats->getPointer("stream", i);
      if (!strncmp(S, prefix.data(), prefix.size())){
        ret.insert(S);
      }
    }
  }else{
    for (uint64_t i = strmStats->getDeleted(); i < endPos; ++i){
      if (strmStats->getInt("status", i) != STRMSTAT_READY){continue;}
      ret.insert(strmStats->getPointer("stream", i));
    }
  }
  return ret;
}

/// Forces a re-sync of the session
/// Assumes the session cache will be updated separately - may not work correctly if this is forgotten!
uint32_t Controller::statSession::invalidate(){
  uint32_t ret = 0;
  sync = 1;
  if (curConns.size() && statPointer){
    for (std::map<unsigned long, statStorage>::iterator jt = curConns.begin(); jt != curConns.end(); ++jt){
      char * data = statPointer->getIndex(jt->first);
      if (data){
        IPC::statExchange tmpEx(data);
        tmpEx.setSync(2);
        ret++;
      }
    }
  }
  return ret;
}

/// Kills all active connections, sets the session state to denied (sync=100).
/// Assumes the session cache will be updated separately - may not work correctly if this is forgotten!
uint32_t Controller::statSession::kill(){
  uint32_t ret = 0;
  sync = 100;
  if (curConns.size() && statPointer){
    for (std::map<unsigned long, statStorage>::iterator jt = curConns.begin(); jt != curConns.end(); ++jt){
      char * data = statPointer->getIndex(jt->first);
      if (data){
        IPC::statExchange tmpEx(data);
        tmpEx.setSync(100);
        uint32_t pid = tmpEx.getPID();
        if (pid > 1){
          Util::Procs::Stop(pid);
          INFO_MSG("Killing PID %lu", pid);
        }
        ret++;
      }
    }
  }
  return ret;
}

/// Updates the given active connection with new stats data.
void Controller::statSession::update(unsigned long index, IPC::statExchange & data){
  //update the sync byte: 0 = requesting fill, 2 = requesting refill, 1 = needs checking, > 1 = state known (100=denied, 10=accepted)
  if (!data.getSync()){
    sessIndex tmpidx(data);
    std::string myHost = tmpidx.host;
    //if we have a maximum connection count per IP, enforce it
    if (maxConnsPerIP && !data.getSync()){
      unsigned int currConns = 1;
      long long shortly = Util::epoch();
      for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){

        if (&it->second != this && it->first.host == myHost && (it->second.hasDataFor(shortly-STATS_DELAY) || it->second.hasDataFor(shortly) || it->second.hasDataFor(shortly-1) || it->second.hasDataFor(shortly-2) || it->second.hasDataFor(shortly-3) || it->second.hasDataFor(shortly-4) || it->second.hasDataFor(shortly-5)) && ++currConns > maxConnsPerIP){break;}
      }
      if (currConns > maxConnsPerIP){
        WARN_MSG("Disconnecting session from %s: exceeds max connection count of %u", myHost.c_str(), maxConnsPerIP);
        data.setSync(100);
      }
    }
    if (data.getSync() != 100){
      //only set the sync if this is the first connection in the list
      //we also catch the case that there are no connections, which is an error-state
      if (!sessions[tmpidx].curConns.size() || sessions[tmpidx].curConns.begin()->first == index){
        MEDIUM_MSG("Requesting sync to %u for %s, %s, %s, %lu", sync, data.streamName().c_str(), data.connector().c_str(), myHost.c_str(), data.crc() & 0xFFFFFFFFu);
        data.setSync(sync);
      }
      //and, always set the sync if it is > 2
      if (sync > 2){
        MEDIUM_MSG("Setting sync to %u for %s, %s, %s, %lu", sync, data.streamName().c_str(), data.connector().c_str(), myHost.c_str(), data.crc() & 0xFFFFFFFFu);
        data.setSync(sync);
      }
    }
  }else{
    if (sync < 2 && data.getSync() > 2){
      sync = data.getSync();
    }
  }
  long long prevDown = getDown();
  long long prevUp = getUp();
  curConns[index].update(data);
  //store timestamp of first received data, if older
  if (firstSec > data.now()){
    firstSec = data.now();
  }
  //store timestamp of last received data, if newer
  if (data.now() > lastSec){
    lastSec = data.now();
    if (!tracked){
      tracked = true;
      firstActive = firstSec;
    }
  }
  long long currDown = getDown();
  long long currUp = getUp();
  if (currUp - prevUp < 0 || currDown-prevDown < 0){
    INFO_MSG("Negative data usage! %lldu/%lldd (u%lld->%lld) in %s over %s, #%lu", currUp-prevUp, currDown-prevDown, prevUp, currUp, data.streamName().c_str(), data.connector().c_str(), index);
  }else{
    if (!noBWCount){
      size_t bwMatchOffset = 0;
      noBWCount = 1;
      while (noBWCountMatches[bwMatchOffset+16] != 0 && bwMatchOffset < 1700){
        if (Socket::matchIPv6Addr(data.host(), std::string(noBWCountMatches+bwMatchOffset, 16), noBWCountMatches[bwMatchOffset+16])){
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
    }
  }
  if (currDown + currUp > COUNTABLE_BYTES){
    std::string streamName = data.streamName();
    if (prevUp + prevDown < COUNTABLE_BYTES){
      if (data.connector() == "INPUT"){
        ++servInputs;
        streamStats[streamName].inputs++;
        streamStats[streamName].currIns++;
        sessionType = SESS_INPUT;
      }else if (data.connector() == "OUTPUT"){
        ++servOutputs;
        streamStats[streamName].outputs++;
        streamStats[streamName].currOuts++;
        sessionType = SESS_OUTPUT;
      }else{
        ++servViewers;
        streamStats[streamName].viewers++;
        streamStats[streamName].currViews++;
        sessionType = SESS_VIEWER;
      }
      if (!streamName.size() || streamName[0] == 0){
        if (streamStats.count(streamName)){streamStats.erase(streamName);}
      }else{
        streamStats[streamName].upBytes += currUp;
        streamStats[streamName].downBytes += currDown;
      }
    }else{
      if (!streamName.size() || streamName[0] == 0){
        if (streamStats.count(streamName)){streamStats.erase(streamName);}
      }else{
        streamStats[streamName].upBytes += currUp - prevUp;
        streamStats[streamName].downBytes += currDown - prevDown;
      }
      if (sessionType == SESS_UNSET){
        if (data.connector() == "INPUT"){
          sessionType = SESS_INPUT;
        }else if (data.connector() == "OUTPUT"){
          sessionType = SESS_OUTPUT;
        }else{
          sessionType = SESS_VIEWER;
        }
      }
    }
  }
}

Controller::sessType Controller::statSession::getSessType(){
  return sessionType;
}

/// Archives the given connection.
void Controller::statSession::wipeOld(unsigned long long cutOff){
  if (firstSec > cutOff){
    return;
  }
  firstSec = 0xFFFFFFFFFFFFFFFFull;
  if (oldConns.size()){
    for (std::deque<statStorage>::iterator it = oldConns.begin(); it != oldConns.end(); ++it){
      while (it->log.size() && it->log.begin()->first < cutOff){
        if (it->log.size() == 1){
          wipedDown += it->log.begin()->second.down;
          wipedUp += it->log.begin()->second.up;
        }
        it->log.erase(it->log.begin());
      }
      if (it->log.size()){
        if (firstSec > it->log.begin()->first){
          firstSec = it->log.begin()->first;
        }
      }
    }
    while (oldConns.size() && !oldConns.begin()->log.size()){
      oldConns.pop_front();
    }
  }
  if (curConns.size()){
    for (std::map<unsigned long, statStorage>::iterator it = curConns.begin(); it != curConns.end(); ++it){
      while (it->second.log.size() > 1 && it->second.log.begin()->first < cutOff){
        it->second.log.erase(it->second.log.begin());
      }
      if (it->second.log.size()){
        if (firstSec > it->second.log.begin()->first){
          firstSec = it->second.log.begin()->first;
        }
      }
    }
  }
}

void Controller::statSession::ping(const Controller::sessIndex & index, unsigned long long disconnectPoint){
  if (!tracked){return;}
  if (lastSec < disconnectPoint){
    switch (sessionType){
      case SESS_INPUT:
        streamStats[index.streamName].currIns--;
        break;
      case SESS_OUTPUT:
        streamStats[index.streamName].currOuts--;
        break;
      case SESS_VIEWER:
        streamStats[index.streamName].currViews--;
        break;
    }
    uint64_t duration = lastSec - firstActive;
    if (duration < 1){duration = 1;}
    std::stringstream tagStream;
    if (tags.size()){
      for (std::set<std::string>::iterator it = tags.begin(); it != tags.end(); ++it){
        tagStream << "[" << *it << "]";
      }
    }
    Controller::logAccess(index.ID, index.streamName, index.connector, index.host, duration, getUp(), getDown(), tagStream.str());
    if (Controller::accesslog.size()){
      if (Controller::accesslog == "LOG"){
        std::stringstream accessStr;
        accessStr << "Session <" << index.ID << "> " << index.streamName << " (" << index.connector << ") from " << index.host << " ended after " << duration << "s, avg " << getUp()/duration/1024 << "KB/s up " << getDown()/duration/1024 << "KB/s down.";
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
          char buffer[100];
          time(&rawtime);
          timeinfo = localtime(&rawtime);
          strftime(buffer, 100, "%F %H:%M:%S", timeinfo);
          accLogFile << buffer << ", " << index.ID << ", " << index.streamName << ", " << index.connector << ", " << index.host << ", " << duration << ", " << getUp()/duration/1024 << ", " << getDown()/duration/1024 << ", ";
          if (tags.size()){accLogFile << tagStream.str();}
          accLogFile << std::endl;
        }
      }
    }
    tracked = false;
  }
}

/// Archives the given connection.
void Controller::statSession::finish(unsigned long index){
  oldConns.push_back(curConns[index]);
  curConns.erase(index);
}

/// Constructs an empty session
Controller::statSession::statSession(){
  firstActive = 0;
  tracked = false;
  firstSec = 0xFFFFFFFFFFFFFFFFull;
  lastSec = 0;
  sync = 1;
  wipedUp = 0;
  wipedDown = 0;
  sessionType = SESS_UNSET;
  noBWCount = 0;
}

/// Moves the given connection to the given session
void Controller::statSession::switchOverTo(statSession & newSess, unsigned long index){
  //add to the given session first
  newSess.curConns[index] = curConns[index];
  //if this connection has data, update firstSec/lastSec if needed
  if (curConns[index].log.size()){
    if (newSess.firstSec > curConns[index].log.begin()->first){
      newSess.firstSec = curConns[index].log.begin()->first;
    }
    if (newSess.lastSec < curConns[index].log.rbegin()->first){
      newSess.lastSec = curConns[index].log.rbegin()->first;
    }
  }
  //remove from current session
  curConns.erase(index);
  //if there was any data, recalculate this session's firstSec and lastSec.
  if (newSess.curConns[index].log.size()){
    firstSec = 0xFFFFFFFFFFFFFFFFull;
    lastSec = 0;
    if (oldConns.size()){
      for (std::deque<statStorage>::iterator it = oldConns.begin(); it != oldConns.end(); ++it){
        if (it->log.size()){
          if (firstSec > it->log.begin()->first){
            firstSec = it->log.begin()->first;
          }
          if (lastSec < it->log.rbegin()->first){
            lastSec = it->log.rbegin()->first;
          }
        }
      }
    }
    if (curConns.size()){
      for (std::map<unsigned long, statStorage>::iterator it = curConns.begin(); it != curConns.end(); ++it){
        if (it->second.log.size()){
          if (firstSec > it->second.log.begin()->first){
            firstSec = it->second.log.begin()->first;
          }
          if (lastSec < it->second.log.rbegin()->first){
            lastSec = it->second.log.rbegin()->first;
          }
        }
      }
    }
  }
}

/// Returns the first measured timestamp in this session.
unsigned long long Controller::statSession::getStart(){
  return firstSec;
}

/// Returns the last measured timestamp in this session.
unsigned long long Controller::statSession::getEnd(){
  return lastSec;
}

/// Returns true if there is data for this session at timestamp t.
bool Controller::statSession::hasDataFor(unsigned long long t){
  if (lastSec < t){return false;}
  if (firstSec > t){return false;}
  if (oldConns.size()){
    for (std::deque<statStorage>::iterator it = oldConns.begin(); it != oldConns.end(); ++it){
      if (it->hasDataFor(t)){return true;}
    }
  }
  if (curConns.size()){
    for (std::map<unsigned long, statStorage>::iterator it = curConns.begin(); it != curConns.end(); ++it){
      if (it->second.hasDataFor(t)){return true;}
    }
  }
  return false;
}

/// Returns true if there is any data for this session.
bool Controller::statSession::hasData(){
  if (!firstSec && !lastSec){return false;}
  if (oldConns.size()){
    for (std::deque<statStorage>::iterator it = oldConns.begin(); it != oldConns.end(); ++it){
      if (it->log.size()){return true;}
    }
  }
  if (curConns.size()){
    for (std::map<unsigned long, statStorage>::iterator it = curConns.begin(); it != curConns.end(); ++it){
      if (it->second.log.size()){return true;}
    }
  }
  return false;
}

/// Returns true if this session should count as a viewer on the given timestamp.
bool Controller::statSession::isViewerOn(unsigned long long t){
  return getUp(t) + getDown(t) > COUNTABLE_BYTES;
}

/// Returns true if this session should count as a viewer
bool Controller::statSession::isViewer(){
  long long upTotal = wipedUp+wipedDown;
  if (oldConns.size()){
    for (std::deque<statStorage>::iterator it = oldConns.begin(); it != oldConns.end(); ++it){
      if (it->log.size()){
        upTotal += it->log.rbegin()->second.up + it->log.rbegin()->second.down;
        if (upTotal > COUNTABLE_BYTES){return true;}
      }
    }
  }
  if (curConns.size()){
    for (std::map<unsigned long, statStorage>::iterator it = curConns.begin(); it != curConns.end(); ++it){
      if (it->second.log.size()){
        upTotal += it->second.log.rbegin()->second.up + it->second.log.rbegin()->second.down;
        if (upTotal > COUNTABLE_BYTES){return true;}
      }
    }
  }
  return false;
}

/// Returns the cumulative connected time for this session at timestamp t.
long long Controller::statSession::getConnTime(unsigned long long t){
  long long retVal = 0;
  if (oldConns.size()){
    for (std::deque<statStorage>::iterator it = oldConns.begin(); it != oldConns.end(); ++it){
      if (it->hasDataFor(t)){
        retVal += it->getDataFor(t).time;
      }
    }
  }
  if (curConns.size()){
    for (std::map<unsigned long, statStorage>::iterator it = curConns.begin(); it != curConns.end(); ++it){
      if (it->second.hasDataFor(t)){
        retVal += it->second.getDataFor(t).time;
      }
    }
  }
  return retVal;
}

/// Returns the last requested media timestamp for this session at timestamp t.
long long Controller::statSession::getLastSecond(unsigned long long t){
  if (curConns.size()){
    for (std::map<unsigned long, statStorage>::iterator it = curConns.begin(); it != curConns.end(); ++it){
      if (it->second.hasDataFor(t)){
        return it->second.getDataFor(t).lastSecond;
      }
    }
  }
  if (oldConns.size()){
    for (std::deque<statStorage>::reverse_iterator it = oldConns.rbegin(); it != oldConns.rend(); ++it){
      if (it->hasDataFor(t)){
        return it->getDataFor(t).lastSecond;
      }
    }
  }
  return 0;
}

/// Returns the cumulative downloaded bytes for this session at timestamp t.
long long Controller::statSession::getDown(unsigned long long t){
  long long retVal = wipedDown;
  if (oldConns.size()){
    for (std::deque<statStorage>::iterator it = oldConns.begin(); it != oldConns.end(); ++it){
      if (it->hasDataFor(t)){
        retVal += it->getDataFor(t).down;
      }
    }
  }
  if (curConns.size()){
    for (std::map<unsigned long, statStorage>::iterator it = curConns.begin(); it != curConns.end(); ++it){
      if (it->second.hasDataFor(t)){
        retVal += it->second.getDataFor(t).down;
      }
    }
  }
  return retVal;
}

/// Returns the cumulative uploaded bytes for this session at timestamp t.
long long Controller::statSession::getUp(unsigned long long t){
  long long retVal = wipedUp;
  if (oldConns.size()){
    for (std::deque<statStorage>::iterator it = oldConns.begin(); it != oldConns.end(); ++it){
      if (it->hasDataFor(t)){
        retVal += it->getDataFor(t).up;
      }
    }
  }
  if (curConns.size()){
    for (std::map<unsigned long, statStorage>::iterator it = curConns.begin(); it != curConns.end(); ++it){
      if (it->second.hasDataFor(t)){
        retVal += it->second.getDataFor(t).up;
      }
    }
  }
  return retVal;
}

/// Returns the cumulative downloaded bytes for this session at timestamp t.
long long Controller::statSession::getDown(){
  long long retVal = wipedDown;
  if (oldConns.size()){
    for (std::deque<statStorage>::iterator it = oldConns.begin(); it != oldConns.end(); ++it){
      if (it->log.size()){
        retVal += it->log.rbegin()->second.down;
      }
    }
  }
  if (curConns.size()){
    for (std::map<unsigned long, statStorage>::iterator it = curConns.begin(); it != curConns.end(); ++it){
      if (it->second.log.size()){
        retVal += it->second.log.rbegin()->second.down;
      }
    }
  }
  return retVal;
}

/// Returns the cumulative uploaded bytes for this session at timestamp t.
long long Controller::statSession::getUp(){
  long long retVal = wipedUp;
  if (oldConns.size()){
    for (std::deque<statStorage>::iterator it = oldConns.begin(); it != oldConns.end(); ++it){
      if (it->log.size()){
        retVal += it->log.rbegin()->second.up;
      }
    }
  }
  if (curConns.size()){
    for (std::map<unsigned long, statStorage>::iterator it = curConns.begin(); it != curConns.end(); ++it){
      if (it->second.log.size()){
        retVal += it->second.log.rbegin()->second.up;
      }
    }
  }
  return retVal;
}

/// Returns the cumulative downloaded bytes per second for this session at timestamp t.
long long Controller::statSession::getBpsDown(unsigned long long t){
  unsigned long long aTime = t - 5;
  if (aTime < firstSec){
    aTime = firstSec;
  }
  long long valA = getDown(aTime);
  long long valB = getDown(t);
  if (t > aTime){
    //INFO_MSG("Saying the speed from time %lli to %lli (being %lli - %lli) is %lli.", aTime, t, valB, valA, (valB - valA) / (t - aTime));
    return (valB - valA) / (t - aTime);
  }else{
    //INFO_MSG("Saying the speed from time %lli to %lli (being %lli - %lli) is %lli.", aTime, t, valB, valA, 0);
    return 0;
  }
}

/// Returns the cumulative uploaded bytes per second for this session at timestamp t.
long long Controller::statSession::getBpsUp(unsigned long long t){
  unsigned long long aTime = t - 5;
  if (aTime < firstSec){
    aTime = firstSec;
  }
  long long valA = getUp(aTime);
  long long valB = getUp(t);
  if (t > aTime){
    return (valB - valA) / (t - aTime);
  }else{
    return 0;
  }
}

/// Returns true if there is data available for timestamp t.
bool Controller::statStorage::hasDataFor(unsigned long long t) {
  if (!log.size()){return false;}
  return (t >= log.begin()->first);
}

/// Returns a reference to the most current data available at timestamp t.
Controller::statLog & Controller::statStorage::getDataFor(unsigned long long t) {
  static statLog empty;
  if (!log.size()){
    empty.time = 0;
    empty.lastSecond = 0;
    empty.down = 0;
    empty.up = 0;
    return empty;
  }
  std::map<unsigned long long, statLog>::iterator it = log.upper_bound(t);
  if (it != log.begin()){
    it--;
  }
  return it->second;
}

/// This function is called by parseStatistics.
/// It updates the internally saved statistics data.
void Controller::statStorage::update(IPC::statExchange & data) {
  statLog tmp;
  tmp.time = data.time();
  tmp.lastSecond = data.lastSecond();
  tmp.down = data.down();
  tmp.up = data.up();
  log[data.now()] = tmp;
  //wipe data older than approx. STAT_CUTOFF seconds
  /// \todo Remove least interesting data first.
  if (log.size() > STAT_CUTOFF){
    log.erase(log.begin());
  }
}
  
/// This function is called by the shared memory page that holds statistics.
/// It updates the internally saved statistics data, moving across sessions or archiving when necessary.
void Controller::parseStatistics(char * data, size_t len, unsigned int id){
  //retrieve stats data
  IPC::statExchange tmpEx(data);
  //calculate the current session index, store as idx.
  sessIndex idx(tmpEx);
  //if the connection was already indexed and it has changed, move it
  if (connToSession.count(id) && connToSession[id] != idx){
    if (sessions[connToSession[id]].getSessType() != SESS_UNSET){
      INFO_MSG("Switching connection %lu from active session %s over to %s", id, connToSession[id].toStr().c_str(), idx.toStr().c_str());
    }else{
      INFO_MSG("Switching connection %lu from inactive session %s over to %s", id, connToSession[id].toStr().c_str(), idx.toStr().c_str());
    }
    sessions[connToSession[id]].switchOverTo(sessions[idx], id);
    if (!sessions[connToSession[id]].hasData()){
      sessions.erase(connToSession[id]);
    }
  }
  if (!connToSession.count(id)){
    INSANE_MSG("New connection: %lu as %s", id, idx.toStr().c_str());
  }
  //store the index for later comparison
  connToSession[id] = idx;
  //update the session with the latest data
  sessions[idx].update(id, tmpEx);
  //check validity of stats data
  char counter = (*(data - 1)) & 0x7F;
  if (counter == 126 || counter == 127){
    //the data is no longer valid - connection has gone away, store for later
    INSANE_MSG("Ended connection: %lu as %s", id, idx.toStr().c_str());
    sessions[idx].finish(id);
    connToSession.erase(id);
  }else{
    if (sessions[idx].getSessType() != SESS_OUTPUT && sessions[idx].getSessType() != SESS_UNSET){
      std::string strmName = tmpEx.streamName();
    }
  }
}

/// Returns true if this stream has at least one connected client.
bool Controller::hasViewers(std::string streamName){
  if (sessions.size()){
    long long currTime = Util::epoch();
    for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
      if (it->first.streamName == streamName && (it->second.hasDataFor(currTime) || it->second.hasDataFor(currTime-1))){
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
/// {
///   //array of streamnames to accumulate. Empty means all.
///   "streams": ["streama", "streamb", "streamc"],
///   //array of protocols to accumulate. Empty means all.
///   "protocols": ["HLS", "HSS"],
///   //list of requested data fields. Empty means all.
///   "fields": ["host", "stream", "protocol", "conntime", "position", "down", "up", "downbps", "upbps"],
///   //unix timestamp of measuring moment. Negative means X seconds ago. Empty means now.
///   "time": 1234567
/// }
/// ~~~~~~~~~~~~~~~
/// OR
/// ~~~~~~~~~~~~~~~{.js}
/// [
///   {},//request object as above
///   {}//repeat the structure as many times as wanted
/// ]
/// ~~~~~~~~~~~~~~~
/// and are responded to as:
/// ~~~~~~~~~~~~~~~{.js}
/// {
///   //unix timestamp of data. Always present, always absolute.
///   "time": 1234567,
///   //array of actually represented data fields.
///   "fields": [...]
///   //for all clients, the data in the order they appear in the "fields" field.
///   "data": [[x, y, z], [x, y, z], [x, y, z]]
/// }
/// ~~~~~~~~~~~~~~~
/// In case of the second method, the response is an array in the same order as the requests.
void Controller::fillClients(JSON::Value & req, JSON::Value & rep){
  tthread::lock_guard<tthread::mutex> guard(statsMutex);
  //first, figure out the timestamp wanted
  long long int reqTime = 0;
  if (req.isMember("time")){
    reqTime = req["time"].asInt();
  }
  //to make sure no nasty timing business takes place, we store the case "now" as a bool.
  bool now = (reqTime == 0);
  //add the current time, if negative or zero.
  if (reqTime <= 0){
    reqTime += Util::epoch();
  }
  //at this point, reqTime is the absolute timestamp.
  rep["time"] = reqTime; //fill the absolute timestamp
  
  unsigned int fields = 0;
  //next, figure out the fields wanted
  if (req.isMember("fields") && req["fields"].size()){
    jsonForEach(req["fields"], it) {
      if ((*it).asStringRef() == "host"){fields |= STAT_CLI_HOST;}
      if ((*it).asStringRef() == "stream"){fields |= STAT_CLI_STREAM;}
      if ((*it).asStringRef() == "protocol"){fields |= STAT_CLI_PROTO;}
      if ((*it).asStringRef() == "conntime"){fields |= STAT_CLI_CONNTIME;}
      if ((*it).asStringRef() == "position"){fields |= STAT_CLI_POSITION;}
      if ((*it).asStringRef() == "down"){fields |= STAT_CLI_DOWN;}
      if ((*it).asStringRef() == "up"){fields |= STAT_CLI_UP;}
      if ((*it).asStringRef() == "downbps"){fields |= STAT_CLI_BPS_DOWN;}
      if ((*it).asStringRef() == "upbps"){fields |= STAT_CLI_BPS_UP;}
    }
  }
  //select all, if none selected
  if (!fields){fields = STAT_CLI_ALL;}
  //figure out what streams are wanted
  std::set<std::string> streams;
  if (req.isMember("streams") && req["streams"].size()){
    jsonForEach(req["streams"], it) {
      streams.insert((*it).asStringRef());
    }
  }
  //figure out what protocols are wanted
  std::set<std::string> protos;
  if (req.isMember("protocols") && req["protocols"].size()){
    jsonForEach(req["protocols"], it) {
      protos.insert((*it).asStringRef());
    }
  }
  //output the selected fields
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
  if (fields & STAT_CLI_CRC){rep["fields"].append("crc");}
  //output the data itself
  rep["data"].null();
  //loop over all sessions
  if (sessions.size()){
    for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
      unsigned long long time = reqTime;
      if (now && reqTime - it->second.getEnd() < 5){time = it->second.getEnd();}
      //data present and wanted? insert it!
      if ((it->second.getEnd() >= time && it->second.getStart() <= time) && (!streams.size() || streams.count(it->first.streamName)) && (!protos.size() || protos.count(it->first.connector))){
        if (it->second.hasDataFor(time)){
          JSON::Value d;
          if (fields & STAT_CLI_HOST){d.append(it->first.host);}
          if (fields & STAT_CLI_STREAM){d.append(it->first.streamName);}
          if (fields & STAT_CLI_PROTO){d.append(it->first.connector);}
          if (fields & STAT_CLI_CONNTIME){d.append(it->second.getConnTime(time));}
          if (fields & STAT_CLI_POSITION){d.append(it->second.getLastSecond(time));}
          if (fields & STAT_CLI_DOWN){d.append(it->second.getDown(time));}
          if (fields & STAT_CLI_UP){d.append(it->second.getUp(time));}
          if (fields & STAT_CLI_BPS_DOWN){d.append(it->second.getBpsDown(time));}
          if (fields & STAT_CLI_BPS_UP){d.append(it->second.getBpsUp(time));}
          if (fields & STAT_CLI_CRC){d.append((long long)it->first.crc);}
          rep["data"].append(d);
        }
      }
    }
  }
  //all done! return is by reference, so no need to return anything here.
}

/// This takes a "active_streams" request, and fills in the response data.
/// 
/// \api
/// `"active_streams"` and `"stats_streams"` requests may either be empty, in which case the response looks like this:
/// ~~~~~~~~~~~~~~~{.js}
/// [
///   //Array of stream names
///   "streamA",
///   "streamB",
///   "streamC"
/// ]
/// ~~~~~~~~~~~~~~~
/// `"stats_streams"` will list all streams that any statistics data is available for, and only those. `"active_streams"` only lists streams that are currently active, and only those.
/// If the request is an array, which may contain any of the following elements:
/// ~~~~~~~~~~~~~~~{.js}
/// [
///   //Array of requested data types
///   "clients", //Current viewer count
///   "lastms" //Current position in the live buffer, if live
/// ]
/// ~~~~~~~~~~~~~~~
/// In which case the response is changed into this format:
/// ~~~~~~~~~~~~~~~{.js}
/// {
///   //Object of stream names, containing arrays in the same order as the request, with the same data
///   "streamA":[
///     0,
///     60000
///   ]
///   "streamB":[
///      //....
///   ]
///   //...
/// }
/// ~~~~~~~~~~~~~~~
/// All streams that any statistics data is available for are listed, and only those streams.
void Controller::fillActive(JSON::Value & req, JSON::Value & rep, bool onlyNow){
  //collect the data first
  std::set<std::string> streams;
  std::map<std::string, unsigned long> clients;
  unsigned int tOut = Util::epoch() - STATS_DELAY;
  unsigned int tIn = Util::epoch() - STATS_INPUT_DELAY;
  //check all sessions
  {
    tthread::lock_guard<tthread::mutex> guard(statsMutex);
    if (sessions.size()){
      for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
        if (it->second.getSessType() == SESS_INPUT){
          if (!onlyNow || (it->second.hasDataFor(tIn) && it->second.isViewerOn(tIn))){
            streams.insert(it->first.streamName);
          }
        }else{
          if (!onlyNow || (it->second.hasDataFor(tOut) && it->second.isViewerOn(tOut))){
            streams.insert(it->first.streamName);
            if (it->second.getSessType() == SESS_VIEWER){
              clients[it->first.streamName]++;
            }
          }
        }
      }
    }
  }
  //Good, now output what we found...
  rep.null();
  for (std::set<std::string>::iterator it = streams.begin(); it != streams.end(); it++){
    if (req.isArray()){
      rep[*it].null();
      jsonForEach(req, j){
        if (j->asStringRef() == "clients"){
          rep[*it].append((long long)clients[*it]);
        }
        if (j->asStringRef() == "lastms"){
          char pageId[NAME_BUFFER_SIZE];
          IPC::sharedPage streamIndex;
          snprintf(pageId, NAME_BUFFER_SIZE, SHM_STREAM_INDEX, it->c_str());
          streamIndex.init(pageId, DEFAULT_STRM_PAGE_SIZE, false, false);
          if (streamIndex.mapped){
            static char liveSemName[NAME_BUFFER_SIZE];
            snprintf(liveSemName, NAME_BUFFER_SIZE, SEM_LIVE, it->c_str());
            IPC::semaphore metaLocker(liveSemName, O_CREAT | O_RDWR, (S_IRWXU|S_IRWXG|S_IRWXO), 1);
            metaLocker.wait();
            DTSC::Scan strm = DTSC::Packet(streamIndex.mapped, streamIndex.len, true).getScan();
            long long lms = 0;
            DTSC::Scan trcks = strm.getMember("tracks");
            unsigned int trcks_ctr = trcks.getSize();
            for (unsigned int i = 0; i < trcks_ctr; ++i){
              if (trcks.getIndice(i).getMember("lastms").asInt() > lms){
                lms = trcks.getIndice(i).getMember("lastms").asInt();
              }
            }
            rep[*it].append(lms);
            metaLocker.post();
          }else{
            rep[*it].append(-1ll);
          }
        }
      }
    }else{
      rep.append(*it);
    }
  }
  //all done! return is by reference, so no need to return anything here.
}

class totalsData {
  public:
    totalsData(){
      clients = 0;
      inputs = 0;
      outputs = 0;
      downbps = 0;
      upbps = 0;
    }
    void add(unsigned int down, unsigned int up, Controller::sessType sT){
      switch (sT){
        case Controller::SESS_VIEWER: clients++; break;
        case Controller::SESS_INPUT: inputs++; break;
        case Controller::SESS_OUTPUT: outputs++; break;
      }
      clients++;
      downbps += down;
      upbps += up;
    }
    long long clients;
    long long inputs;
    long long outputs;
    long long downbps;
    long long upbps;
};

/// This takes a "totals" request, and fills in the response data.
void Controller::fillTotals(JSON::Value & req, JSON::Value & rep){
  tthread::lock_guard<tthread::mutex> guard(statsMutex);
  //first, figure out the timestamps wanted
  long long int reqStart = 0;
  long long int reqEnd = 0;
  if (req.isMember("start")){
    reqStart = req["start"].asInt();
  }
  if (req.isMember("end")){
    reqEnd = req["end"].asInt();
  }
  //add the current time, if negative or zero.
  if (reqStart < 0){
    reqStart += Util::epoch();
  }
  if (reqStart == 0){
    reqStart = Util::epoch() - STAT_CUTOFF;
  }
  if (reqEnd <= 0){
    reqEnd += Util::epoch();
  }
  //at this point, reqStart and reqEnd are the absolute timestamp.
  
  unsigned int fields = 0;
  //next, figure out the fields wanted
  if (req.isMember("fields") && req["fields"].size()){
    jsonForEach(req["fields"], it) {
      if ((*it).asStringRef() == "clients"){fields |= STAT_TOT_CLIENTS;}
      if ((*it).asStringRef() == "inputs"){fields |= STAT_TOT_INPUTS;}
      if ((*it).asStringRef() == "outputs"){fields |= STAT_TOT_OUTPUTS;}
      if ((*it).asStringRef() == "downbps"){fields |= STAT_TOT_BPS_DOWN;}
      if ((*it).asStringRef() == "upbps"){fields |= STAT_TOT_BPS_UP;}
    }
  }
  //select all, if none selected
  if (!fields){fields = STAT_TOT_ALL;}
  //figure out what streams are wanted
  std::set<std::string> streams;
  if (req.isMember("streams") && req["streams"].size()){
    jsonForEach(req["streams"], it) {
      streams.insert((*it).asStringRef());
    }
  }
  //figure out what protocols are wanted
  std::set<std::string> protos;
  if (req.isMember("protocols") && req["protocols"].size()){
    jsonForEach(req["protocols"], it) {
      protos.insert((*it).asStringRef());
    }
  }
  //output the selected fields
  rep["fields"].null();
  if (fields & STAT_TOT_CLIENTS){rep["fields"].append("clients");}
  if (fields & STAT_TOT_INPUTS){rep["fields"].append("inputs");}
  if (fields & STAT_TOT_OUTPUTS){rep["fields"].append("outputs");}
  if (fields & STAT_TOT_BPS_DOWN){rep["fields"].append("downbps");}
  if (fields & STAT_TOT_BPS_UP){rep["fields"].append("upbps");}
  //start data collection
  std::map<long long unsigned int, totalsData> totalsCount;
  //loop over all sessions
  /// \todo Make the interval configurable instead of 1 second
  if (sessions.size()){
    for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
      //data present and wanted? insert it!
      if ((it->second.getEnd() >= (unsigned long long)reqStart || it->second.getStart() <= (unsigned long long)reqEnd) && (!streams.size() || streams.count(it->first.streamName)) && (!protos.size() || protos.count(it->first.connector))){
        for (unsigned long long i = reqStart; i <= reqEnd; ++i){
          if (it->second.hasDataFor(i)){
            totalsCount[i].add(it->second.getBpsDown(i), it->second.getBpsUp(i), it->second.getSessType());
          }
        }
      }
    }
  }
  //output the data itself
  if (!totalsCount.size()){
    //Oh noes! No data. We'll just reply with a bunch of nulls.
    rep["start"].null();
    rep["end"].null();
    rep["data"].null();
    rep["interval"].null();
    return;
  }
  //yay! We have data!
  rep["start"] = (long long)totalsCount.begin()->first;
  rep["end"] = (long long)totalsCount.rbegin()->first;
  rep["data"].null();
  rep["interval"].null();
  long long prevT = 0;
  JSON::Value i;
  for (std::map<long long unsigned int, totalsData>::iterator it = totalsCount.begin(); it != totalsCount.end(); it++){
    JSON::Value d;
    if (fields & STAT_TOT_CLIENTS){d.append(it->second.clients);}
    if (fields & STAT_TOT_INPUTS){d.append(it->second.inputs);}
    if (fields & STAT_TOT_OUTPUTS){d.append(it->second.outputs);}
    if (fields & STAT_TOT_BPS_DOWN){d.append(it->second.downbps);}
    if (fields & STAT_TOT_BPS_UP){d.append(it->second.upbps);}
    rep["data"].append(d);
    if (prevT){
      if (i.size() < 2){
        i.append(1ll);
        i.append((long long)(it->first - prevT));
      }else{
        if (i[1u].asInt() != (long long)(it->first - prevT)){
          rep["interval"].append(i);
          i[0u] = 1ll;
          i[1u] = (long long)(it->first - prevT);
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
  //all done! return is by reference, so no need to return anything here.
}

void Controller::handlePrometheus(HTTP::Parser & H, Socket::Connection & conn, int mode){
  switch (mode){
    case PROMETHEUS_TEXT:
      H.SetHeader("Content-Type", "text/plain; version=0.0.4");
      break;
    case PROMETHEUS_JSON:
      H.SetHeader("Content-Type", "text/json");
      break;
  }
  H.SetHeader("Server", "MistServer/" PACKAGE_VERSION);
  H.StartResponse("200", "OK", H, conn, true);


  //Collect core server stats
  long long int cpu_use = 0;
  long long int mem_total = 0, mem_free = 0, mem_bufcache = 0;
  long long int bw_up_total = 0, bw_down_total = 0;
  {
    std::ifstream cpustat("/proc/stat");
    if (cpustat){
      char line[300];
      while (cpustat.getline(line, 300)){
        static unsigned long long cl_total = 0, cl_idle = 0;
        unsigned long long c_user, c_nice, c_syst, c_idle, c_total;
        if (sscanf(line, "cpu %Lu %Lu %Lu %Lu", &c_user, &c_nice, &c_syst, &c_idle) == 4){
          c_total = c_user + c_nice + c_syst + c_idle;
          if (c_total - cl_total > 0){
            cpu_use = (long long int)(1000 - ((c_idle - cl_idle) * 1000) / (c_total - cl_total));
          }else{
            cpu_use = 0;
          }
          cl_total = c_total;
          cl_idle = c_idle;
          break;
        }
      }
    }
    std::ifstream meminfo("/proc/meminfo");
    if (meminfo){
      char line[300];
      while (meminfo.good()){
        meminfo.getline(line, 300);
        if (meminfo.fail()){
          //empty lines? ignore them, clear flags, continue
          if ( !meminfo.eof()){
            meminfo.ignore();
            meminfo.clear();
          }
          continue;
        }
        long long int i;
        if (sscanf(line, "MemTotal : %lli kB", &i) == 1){
          mem_total = i;
        }
        if (sscanf(line, "MemFree : %lli kB", &i) == 1){
          mem_free = i;
        }
        if (sscanf(line, "Buffers : %lli kB", &i) == 1){
          mem_bufcache += i;
        }
        if (sscanf(line, "Cached : %lli kB", &i) == 1){
          mem_bufcache += i;
        }
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
  long long shm_total = 0, shm_free = 0;
#if !defined(__CYGWIN__) && !defined(_WIN32)
  {
    struct statvfs shmd;
    IPC::sharedPage tmpConf(SHM_CONF, DEFAULT_CONF_PAGE_SIZE, false, false);
    if (tmpConf.mapped && tmpConf.handle){
      fstatvfs(tmpConf.handle, &shmd);
      shm_free = (shmd.f_bfree*shmd.f_frsize)/1024;
      shm_total = (shmd.f_blocks*shmd.f_frsize)/1024;
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

    {//Scope for shortest possible blocking of statsMutex
      tthread::lock_guard<tthread::mutex> guard(statsMutex);
      //collect the data first
      std::map<std::string, uint32_t> outputs;
      unsigned long totViewers = 0, totInputs = 0, totOutputs = 0;
      unsigned int tOut = Util::epoch() - STATS_DELAY;
      unsigned int tIn = Util::epoch() - STATS_INPUT_DELAY;
      //check all sessions
      if (sessions.size()){
        for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
          switch (it->second.getSessType()){
            case SESS_UNSET:
            case SESS_VIEWER:
              if (it->second.hasDataFor(tOut) && it->second.isViewerOn(tOut)){
                outputs[it->first.connector]++;
                totViewers++;
              }
              break;
            case SESS_INPUT:
              if (it->second.hasDataFor(tIn) && it->second.isViewerOn(tIn)){
                totInputs++;
              }
              break;
            case SESS_OUTPUT:
              if (it->second.hasDataFor(tOut) && it->second.isViewerOn(tOut)){
                totOutputs++;
              }
              break;
          }
        }
      }

      response << "# HELP mist_sessions_total Number of sessions active right now, server-wide, by type.\n";
      response << "# TYPE mist_sessions_total gauge\n";
      response << "mist_sessions_total{sessType=\"viewers\"} " << totViewers << "\n";
      response << "mist_sessions_total{sessType=\"incoming\"} " << totInputs << "\n";
      response << "mist_sessions_total{sessType=\"outgoing\"} " << totOutputs << "\n";
      response << "mist_sessions_total{sessType=\"cached\"} " << sessions.size() << "\n\n";

      response << "# HELP mist_outputs Number of viewers active right now, server-wide, by output type.\n";
      response << "# TYPE mist_outputs gauge\n";
      for (std::map<std::string, uint32_t>::iterator it = outputs.begin(); it != outputs.end(); ++it){
        response << "mist_outputs{output=\"" << it->first << "\"} " << it->second << "\n";
      }
      response << "\n";

      response << "# HELP mist_sessions_count Counts of unique sessions by type since server start.\n";
      response << "# TYPE mist_sessions_count counter\n";
      response << "mist_sessions_count{sessType=\"viewers\"} " << servViewers << "\n";
      response << "mist_sessions_count{sessType=\"incoming\"} " << servInputs << "\n";
      response << "mist_sessions_count{sessType=\"outgoing\"} " << servOutputs << "\n\n";

      response << "# HELP mist_bw_total Count of bytes handled since server start, by direction.\n";
      response << "# TYPE mist_bw_total counter\n";
      response << "stat_bw_total{direction=\"up\"} " << bw_up_total << "\n";
      response << "stat_bw_total{direction=\"down\"} " << bw_down_total << "\n\n";
      response << "mist_bw_total{direction=\"up\"} " << servUpBytes << "\n";
      response << "mist_bw_total{direction=\"down\"} " << servDownBytes << "\n\n";
      response << "mist_bw_other{direction=\"up\"} " << servUpOtherBytes << "\n";
      response << "mist_bw_other{direction=\"down\"} " << servDownOtherBytes << "\n\n";
      response << "mist_bw_limit " << bwLimit << "\n\n";

      response << "\n# HELP mist_viewers Number of sessions by type and stream active right now.\n";
      response << "# TYPE mist_viewers gauge\n";
      response << "# HELP mist_viewcount Count of unique viewer sessions since stream start, per stream.\n";
      response << "# TYPE mist_viewcount counter\n";
      response << "# HELP mist_bw Count of bytes handled since stream start, by direction.\n";
      response << "# TYPE mist_bw counter\n";
      for (std::map<std::string, struct streamTotals>::iterator it = streamStats.begin(); it != streamStats.end(); ++it){
        response << "mist_sessions{stream=\"" << it->first << "\",sessType=\"viewers\"} " << it->second.currViews << "\n";
        response << "mist_sessions{stream=\"" << it->first << "\",sessType=\"incoming\"} " << it->second.currIns << "\n";
        response << "mist_sessions{stream=\"" << it->first << "\",sessType=\"outgoing\"} " << it->second.currOuts << "\n";
        response << "mist_viewcount{stream=\"" << it->first << "\"} " << it->second.viewers << "\n";
        response << "mist_bw{stream=\"" << it->first << "\",direction=\"up\"} " << it->second.upBytes << "\n";
        response << "mist_bw{stream=\"" << it->first << "\",direction=\"down\"} " << it->second.downBytes << "\n";
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
    resp["logs"] = (long long)Controller::logCounter;
    {//Scope for shortest possible blocking of statsMutex
      tthread::lock_guard<tthread::mutex> guard(statsMutex);
      //collect the data first
      std::map<std::string, uint32_t> outputs;
      unsigned long totViewers = 0, totInputs = 0, totOutputs = 0;
      unsigned int tOut = Util::epoch() - STATS_DELAY;
      unsigned int tIn = Util::epoch() - STATS_INPUT_DELAY;
      //check all sessions
      if (sessions.size()){
        for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
          switch (it->second.getSessType()){
            case SESS_UNSET:
            case SESS_VIEWER:
              if (it->second.hasDataFor(tOut) && it->second.isViewerOn(tOut)){
                outputs[it->first.connector]++;
                totViewers++;
              }
              break;
            case SESS_INPUT:
              if (it->second.hasDataFor(tIn) && it->second.isViewerOn(tIn)){
                totInputs++;
              }
              break;
            case SESS_OUTPUT:
              if (it->second.hasDataFor(tOut) && it->second.isViewerOn(tOut)){
                totOutputs++;
              }
              break;
          }
        }
      }

      resp["curr"].append((long long)totViewers);
      resp["curr"].append((long long)totInputs);
      resp["curr"].append((long long)totOutputs);
      resp["curr"].append((long long)sessions.size());
      resp["tot"].append((long long)servViewers);
      resp["tot"].append((long long)servInputs);
      resp["tot"].append((long long)servOutputs);
      resp["st"].append((long long)bw_up_total);
      resp["st"].append((long long)bw_down_total);
      resp["bw"].append((long long)servUpBytes);
      resp["bw"].append((long long)servDownBytes);
      resp["bwlimit"] = (long long)bwLimit;
      resp["obw"].append((long long)servUpOtherBytes);
      resp["obw"].append((long long)servDownOtherBytes);


      for (std::map<std::string, struct streamTotals>::iterator it = streamStats.begin(); it != streamStats.end(); ++it){
        resp["streams"][it->first]["tot"].append((long long)it->second.viewers);
        resp["streams"][it->first]["tot"].append((long long)it->second.inputs);
        resp["streams"][it->first]["tot"].append((long long)it->second.outputs);
        resp["streams"][it->first]["bw"].append((long long)it->second.upBytes);
        resp["streams"][it->first]["bw"].append((long long)it->second.downBytes);
        resp["streams"][it->first]["curr"].append((long long)it->second.currViews);
        resp["streams"][it->first]["curr"].append((long long)it->second.currIns);
        resp["streams"][it->first]["curr"].append((long long)it->second.currOuts);
      }
      for (std::map<std::string, uint32_t>::iterator it = outputs.begin(); it != outputs.end(); ++it){
        resp["outputs"][it->first] = (long long)it->second;
      }
    }

    jsonForEach(Storage["streams"], sIt){
      resp["conf_streams"].append(sIt.key());
    }

    {
      tthread::lock_guard<tthread::mutex> guard(Controller::configMutex);
      //Loop over connectors
      const JSON::Value &caps = capabilities["connectors"];
      jsonForEachConst(Storage["config"]["protocols"], prtcl){
        const std::string &cName = (*prtcl)["connector"].asStringRef();
        if ((*prtcl)["online"].asInt() != 1){continue;}
        if (!caps.isMember(cName)){continue;}
        const JSON::Value & capa = caps[cName];
        if (!capa.isMember("optional") || !capa["optional"].isMember("port")){continue;}
        //We now know it's configured, online and has a listening port
        HTTP::URL outURL("HOST");
        //get the default port if none is set
        if (prtcl->isMember("port")){
          outURL.port = (*prtcl)["port"].asString();
        }
        if (!outURL.port.size()){
          outURL.port = capa["optional"]["port"]["default"].asString();
        }
        //set the protocol
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
        //set the public access, if needed
        if (prtcl->isMember("pubaddr") && (*prtcl)["pubaddr"].asString().size()){
          HTTP::URL altURL((*prtcl)["pubaddr"].asString());
          outURL.protocol = altURL.protocol;
          if (altURL.host.size()){outURL.host = altURL.host;}
          outURL.port = altURL.port;
          outURL.path = altURL.path;
        }
        //Add the URL, if present
        if (capa.isMember("url_rel")){
          resp["outputs"][cName] = outURL.link("./"+capa["url_rel"].asStringRef()).getUrl();
        }

        //if this connector can be depended upon by other connectors, loop over the rest
        if (capa.isMember("provides")){
          const std::string &cProv = capa["provides"].asStringRef();
          jsonForEachConst(Storage["config"]["protocols"], chld){
            const std::string &child = (*chld)["connector"].asStringRef();
            if (!caps.isMember(child) || !caps[child].isMember("deps")){continue;}
            if (caps[child].isMember("deps") && caps[child]["deps"].asStringRef() == cProv && caps[child].isMember("url_rel")){
              resp["outputs"][child] = outURL.link("./"+caps[child]["url_rel"].asStringRef()).getUrl();
            }
          }
        }
      }
    }

    H.Chunkify(resp.toString(), conn);
  }

  H.Chunkify("", conn);
  H.Clean();
}

