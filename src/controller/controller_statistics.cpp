#include <cstdio>
#include <list>
#include <fstream>
#include <mist/config.h>
#include <mist/shared_memory.h>
#include <mist/dtsc.h>
#include "controller_statistics.h"
#include "controller_limits.h"
#include "controller_push.h"
#include "controller_storage.h"

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
#define STAT_TOT_ALL 0xFF

#define COUNTABLE_BYTES 128*1024


std::map<Controller::sessIndex, Controller::statSession> Controller::sessions; ///< list of sessions that have statistics data available
std::map<unsigned long, Controller::sessIndex> Controller::connToSession; ///< Map of socket IDs to session info.
bool Controller::killOnExit = KILL_ON_EXIT;
tthread::mutex Controller::statsMutex;
std::map<std::string, unsigned int> Controller::activeStreams;
unsigned int Controller::maxConnsPerIP = 0;

//For server-wide totals. Local to this file only.
struct streamTotals {
  unsigned long long upBytes;
  unsigned long long downBytes;
  unsigned long long inputs;
  unsigned long long outputs;
  unsigned long long viewers;
  unsigned int timeout;
};
static std::map<std::string, struct streamTotals> streamStats;
static unsigned long long servUpBytes = 0;
static unsigned long long servDownBytes = 0;
static unsigned long long servInputs = 0;
static unsigned long long servOutputs = 0;
static unsigned long long servViewers = 0;

Controller::sessIndex::sessIndex(std::string dhost, unsigned int dcrc, std::string dstreamName, std::string dconnector){
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
  s << host << " " << crc << " " << streamName << " " << connector;
  return s.str();
}

/// Initializes a sessIndex from a statExchange object, converting binary format IP addresses into strings.
/// This extracts the host, stream name, connector and crc field, ignoring everything else.
Controller::sessIndex::sessIndex(IPC::statExchange & data){
  std::string tHost = data.host();
  if (tHost.substr(0, 12) == std::string("\000\000\000\000\000\000\000\000\000\000\377\377", 12)){
    char tmpstr[16];
    snprintf(tmpstr, 16, "%hhu.%hhu.%hhu.%hhu", tHost[12], tHost[13], tHost[14], tHost[15]);
    host = tmpstr;
  }else{
    char tmpstr[40];
    snprintf(tmpstr, 40, "%0.2x%0.2x:%0.2x%0.2x:%0.2x%0.2x:%0.2x%0.2x:%0.2x%0.2x:%0.2x%0.2x:%0.2x%0.2x:%0.2x%0.2x", tHost[0], tHost[1], tHost[2], tHost[3], tHost[4], tHost[5], tHost[6], tHost[7], tHost[8], tHost[9], tHost[10], tHost[11], tHost[12], tHost[13], tHost[14], tHost[15]);
    host = tmpstr;
  }
  streamName = data.streamName();
  connector = data.connector();
  crc = data.crc();
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

/// Forces a disconnect to all users.
void Controller::killStatistics(char * data, size_t len, unsigned int id){
  (*(data - 1)) = 128;//Send disconnect message;
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


/// This function runs as a thread and roughly once per second retrieves
/// statistics from all connected clients, as well as wipes
/// old statistics that have disconnected over 10 minutes ago.
void Controller::SharedMemStats(void * config){
  DEBUG_MSG(DLVL_HIGH, "Starting stats thread");
  IPC::sharedServer statServer(SHM_STATISTICS, STAT_EX_SIZE, true);
  std::set<std::string> inactiveStreams;
  while(((Util::Config*)config)->is_active){
    {
      tthread::lock_guard<tthread::mutex> guard(statsMutex);
      //parse current users
      statServer.parseEach(parseStatistics);
      //wipe old statistics
      if (sessions.size()){
        std::list<sessIndex> mustWipe;
        unsigned long long cutOffPoint = Util::epoch() - STAT_CUTOFF;
        for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
          it->second.wipeOld(cutOffPoint);
          if (!it->second.hasData()){
            mustWipe.push_back(it->first);
          }
        }
        while (mustWipe.size()){
          sessions.erase(mustWipe.front());
          mustWipe.pop_front();
        }
      }
      if (activeStreams.size()){
        for (std::map<std::string, unsigned int>::iterator it = activeStreams.begin(); it != activeStreams.end(); ++it){
          if (++it->second > STATS_DELAY){
            streamStopped(it->first);
            inactiveStreams.insert(it->first);
          }
        }
        while (inactiveStreams.size()){
          activeStreams.erase(*inactiveStreams.begin());
          streamStats.erase(*inactiveStreams.begin());
          inactiveStreams.erase(inactiveStreams.begin());
        }
      }
      Controller::checkServerLimits(); /*LTS*/
    }
    Util::wait(1000);
  }
  DEBUG_MSG(DLVL_HIGH, "Stopping stats thread");
  if (Controller::killOnExit){
    DEBUG_MSG(DLVL_WARN, "Killing all connected clients to force full shutdown");
    unsigned int c = 0;//to prevent eternal loops
    do{
      statServer.parseEach(killStatistics);
      Util::wait(250);
    }while(statServer.amount && c++ < 10);
  }
}

/// Updates the given active connection with new stats data.
void Controller::statSession::update(unsigned long index, IPC::statExchange & data){
  //update the sync byte: 0 = requesting fill, 1 = needs checking, > 1 = state known (100=denied, 10=accepted)
  if (!data.getSync()){
    std::string myHost;
    {
      sessIndex tmpidx(data);
      myHost = tmpidx.host;
    }
    MEDIUM_MSG("Setting sync to %u for %s, %s, %s, %lu", sync, data.streamName().c_str(), data.connector().c_str(), myHost.c_str(), data.crc() & 0xFFFFFFFFu);
    //if we have a maximum connection count per IP, enforce it
    if (maxConnsPerIP){
      unsigned int currConns = 1;
      long long shortly = Util::epoch();
      for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){

        if (&it->second != this && it->first.host == myHost && (it->second.hasDataFor(shortly-STATS_DELAY) || it->second.hasDataFor(shortly) || it->second.hasDataFor(shortly-1) || it->second.hasDataFor(shortly-2) || it->second.hasDataFor(shortly-3) || it->second.hasDataFor(shortly-4) || it->second.hasDataFor(shortly-5)) && ++currConns > maxConnsPerIP){break;}
      }
      if (currConns > maxConnsPerIP){
        WARN_MSG("Disconnecting session from %s: exceeds max connection count of %u", myHost.c_str(), maxConnsPerIP);
        data.setSync(100);
      }else{
        data.setSync(sync);
      }
    }else{
      //no maximum, just set the sync byte to its current value
      data.setSync(sync);
    }
  }else{
    if (sync < 2){
      sync = data.getSync();
    }
  }
  long long prevDown = getDown();
  long long prevUp = getUp();
  curConns[index].update(data);
  //store timestamp of last received data, if newer
  if (data.now() > lastSec){
    lastSec = data.now();
  }
  //store timestamp of first received data, if older
  if (firstSec > data.now()){
    firstSec = data.now();
  }
  long long currDown = getDown();
  long long currUp = getUp();
  servUpBytes += currUp - prevUp;
  servDownBytes += currDown - prevDown;
  if (currDown + currUp > COUNTABLE_BYTES){
    std::string streamName = data.streamName();
    if (prevUp + prevDown < COUNTABLE_BYTES){
      if (data.connector() == "INPUT"){
        ++servInputs;
        streamStats[streamName].inputs++;
        sessionType = SESS_INPUT;
      }else if (data.connector() == "OUTPUT"){
        ++servOutputs;
        streamStats[streamName].outputs++;
        sessionType = SESS_OUTPUT;
      }else{
        ++servViewers;
        streamStats[streamName].viewers++;
        sessionType = SESS_VIEWER;
      }
      streamStats[streamName].upBytes += currUp;
      streamStats[streamName].downBytes += currDown;
    }else{
      streamStats[streamName].upBytes += currUp - prevUp;
      streamStats[streamName].downBytes += currDown - prevDown;
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

/// Archives the given connection.
void Controller::statSession::finish(unsigned long index){
  oldConns.push_back(curConns[index]);
  curConns.erase(index);
}

/// Constructs an empty session
Controller::statSession::statSession(){
  firstSec = 0xFFFFFFFFFFFFFFFFull;
  lastSec = 0;
  sync = 1;
  wipedUp = 0;
  wipedDown = 0;
  sessionType = SESS_UNSET;
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
    INSANE_MSG("SWITCHING %s OVER TO %s", connToSession[id].toStr().c_str(), idx.toStr().c_str());
    sessions[connToSession[id]].switchOverTo(sessions[idx], id);
    if (!sessions[connToSession[id]].hasData()){
      sessions.erase(connToSession[id]);
    }
  }
  //store the index for later comparison
  connToSession[id] = idx;
  //update the session with the latest data
  sessions[idx].update(id, tmpEx);
  //check validity of stats data
  char counter = (*(data - 1));
  if (counter == 126 || counter == 127){
    //the data is no longer valid - connection has gone away, store for later
    sessions[idx].finish(id);
    connToSession.erase(id);
  }else{
    if (sessions[idx].getSessType() != SESS_OUTPUT){
      std::string strmName = tmpEx.streamName();
      if (strmName.size()){
        if (!activeStreams.count(strmName)){
          streamStarted(strmName);
        }
        activeStreams[strmName] = 0;
      }
    }
  }
  /*LTS-START*/
  //if (counter < 125 && Controller::isBlacklisted(tmpEx.host(), ID, tmpEx.time())){
  //  (*(data - 1)) = 128;//Send disconnect message;
  //}
  /*LTS-END*/
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
  unsigned int t = Util::epoch() - STATS_DELAY;
  //check all sessions
  if (sessions.size()){
    for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
      if (!onlyNow || (it->second.hasDataFor(t) && it->second.isViewerOn(t))){
        streams.insert(it->first.streamName);
        if (it->second.getSessType() == SESS_VIEWER){
          clients[it->first.streamName]++;
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
          streamIndex.init(pageId, DEFAULT_META_PAGE_SIZE, false, false);
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
      downbps = 0;
      upbps = 0;
    }
    void add(unsigned int down, unsigned int up){
      clients++;
      downbps += down;
      upbps += up;
    }
    long long clients;
    long long downbps;
    long long upbps;
};

/// This takes a "totals" request, and fills in the response data.
/// 
/// \api
/// `"totals"` requests take the form of:
/// ~~~~~~~~~~~~~~~{.js}
/// {
///   //array of streamnames to accumulate. Empty means all.
///   "streams": ["streama", "streamb", "streamc"],
///   //array of protocols to accumulate. Empty means all.
///   "protocols": ["HLS", "HSS"],
///   //list of requested data fields. Empty means all.
///   "fields": ["clients", "downbps", "upbps"],
///   //unix timestamp of data start. Negative means X seconds ago. Empty means earliest available.
///   "start": 1234567
///   //unix timestamp of data end. Negative means X seconds ago. Empty means latest available (usually 'now').
///   "end": 1234567
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
///   //unix timestamp of start of data. Always present, always absolute.
///   "start": 1234567,
///   //unix timestamp of end of data. Always present, always absolute.
///   "end": 1234567,
///   //array of actually represented data fields.
///   "fields": [...]
///   // Time between datapoints. Here: 10 points with each 5 seconds afterwards, followed by 10 points with each 1 second afterwards.
///   "interval": [[10, 5], [10, 1]],
///   //the data for the times as mentioned in the "interval" field, in the order they appear in the "fields" field.
///   "data": [[x, y, z], [x, y, z], [x, y, z]]
/// }
/// ~~~~~~~~~~~~~~~
/// In case of the second method, the response is an array in the same order as the requests.
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
            totalsCount[i].add(it->second.getBpsDown(i), it->second.getBpsUp(i));
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
  H.StartResponse("200", "OK", H, conn);


  //Collect core server stats
  long long int cpu_use = 0;
  long long int mem_total = 0, mem_free = 0, mem_bufcache = 0;
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
  }


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

    {//Scope for shortest possible blocking of statsMutex
      tthread::lock_guard<tthread::mutex> guard(statsMutex);
      //collect the data first
      std::map<std::string, struct streamTotals> streams;
      unsigned long totViewers = 0, totInputs = 0, totOutputs = 0;
      unsigned int t = Util::epoch() - STATS_DELAY;
      //check all sessions
      if (sessions.size()){
        for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
          if (it->second.hasDataFor(t) && it->second.isViewerOn(t)){
            switch (it->second.getSessType()){
              case SESS_UNSET:
              case SESS_VIEWER:
                streams[it->first.streamName].viewers++;
                totViewers++;
                break;
              case SESS_INPUT:
                streams[it->first.streamName].inputs++;
                totInputs++;
                break;
              case SESS_OUTPUT:
                streams[it->first.streamName].outputs++;
                totOutputs++;
                break;
            }

          }
        }
      }

      response << "# HELP mist_sessions_total Number of sessions active right now, server-wide, by type.\n";
      response << "# TYPE mist_sessions_total gauge\n";
      response << "mist_sessions_total{sessType=\"viewers\"} " << totViewers << "\n";
      response << "mist_sessions_total{sessType=\"incoming\"} " << totInputs << "\n";
      response << "mist_sessions_total{sessType=\"outgoing\"} " << totOutputs << "\n";
      response << "mist_sessions_total{sessType=\"cached\"} " << sessions.size() << "\n\n";

      response << "# HELP mist_sessions_count Counts of unique sessions by type since server start.\n";
      response << "# TYPE mist_sessions_count counter\n";
      response << "mist_sessions_count{sessType=\"viewers\"} " << servViewers << "\n";
      response << "mist_sessions_count{sessType=\"incoming\"} " << servInputs << "\n";
      response << "mist_sessions_count{sessType=\"outgoing\"} " << servOutputs << "\n\n";

      response << "# HELP mist_bw_total Count of bytes handled since server start, by direction.\n";
      response << "# TYPE mist_bw_total counter\n";
      response << "mist_bw_total{direction=\"up\"} " << servUpBytes << "\n";
      response << "mist_bw_total{direction=\"down\"} " << servDownBytes << "\n\n";

      response << "# HELP mist_viewers Number of sessions by type and stream active right now.\n";
      response << "# TYPE mist_viewers gauge\n";
      for (std::map<std::string, struct streamTotals>::iterator it = streams.begin(); it != streams.end(); ++it){
        response << "mist_sessions{stream=\"" << it->first << "\",sessType=\"viewers\"} " << it->second.viewers << "\n";
        response << "mist_sessions{stream=\"" << it->first << "\",sessType=\"incoming\"} " << it->second.inputs << "\n";
        response << "mist_sessions{stream=\"" << it->first << "\",sessType=\"outgoing\"} " << it->second.outputs << "\n";
      }

      response << "\n# HELP mist_viewcount Count of unique viewer sessions since stream start, per stream.\n";
      response << "# TYPE mist_viewcount counter\n";
      response << "# HELP mist_bw Count of bytes handled since stream start, by direction.\n";
      response << "# TYPE mist_bw counter\n";
      for (std::map<std::string, struct streamTotals>::iterator it = streamStats.begin(); it != streamStats.end(); ++it){
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
    resp["logs"] = (long long)Controller::logCounter;
    {//Scope for shortest possible blocking of statsMutex
      tthread::lock_guard<tthread::mutex> guard(statsMutex);
      //collect the data first
      std::map<std::string, struct streamTotals> streams;
      unsigned long totViewers = 0, totInputs = 0, totOutputs = 0;
      unsigned int t = Util::epoch() - STATS_DELAY;
      //check all sessions
      if (sessions.size()){
        for (std::map<sessIndex, statSession>::iterator it = sessions.begin(); it != sessions.end(); it++){
          if (it->second.hasDataFor(t) && it->second.isViewerOn(t)){
            switch (it->second.getSessType()){
              case SESS_UNSET:
              case SESS_VIEWER:
                streams[it->first.streamName].viewers++;
                totViewers++;
                break;
              case SESS_INPUT:
                streams[it->first.streamName].inputs++;
                totInputs++;
                break;
              case SESS_OUTPUT:
                streams[it->first.streamName].outputs++;
                totOutputs++;
                break;
            }

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
      resp["bw"].append((long long)servUpBytes);
      resp["bw"].append((long long)servDownBytes);

      for (std::map<std::string, struct streamTotals>::iterator it = streams.begin(); it != streams.end(); ++it){
        resp["streams"][it->first]["curr"].append((long long)it->second.viewers);
        resp["streams"][it->first]["curr"].append((long long)it->second.inputs);
        resp["streams"][it->first]["curr"].append((long long)it->second.outputs);
      }

      for (std::map<std::string, struct streamTotals>::iterator it = streamStats.begin(); it != streamStats.end(); ++it){
        resp["streams"][it->first]["tot"].append((long long)it->second.viewers);
        resp["streams"][it->first]["tot"].append((long long)it->second.inputs);
        resp["streams"][it->first]["tot"].append((long long)it->second.outputs);
        resp["streams"][it->first]["bw"].append((long long)it->second.upBytes);
        resp["streams"][it->first]["bw"].append((long long)it->second.downBytes);
      }
    }
    H.Chunkify(resp.toString(), conn);
  }

  H.Chunkify("", conn);
  H.Clean();
}

