#include <cstdio>
#include <list>
#include <mist/config.h>
#include "controller_statistics.h"
#include "controller_storage.h"

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
tthread::mutex Controller::statsMutex;

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

/// \todo Make this prettier.
IPC::sharedServer * statPointer = 0;


/// This function runs as a thread and roughly once per second retrieves
/// statistics from all connected clients, as well as wipes
/// old statistics that have disconnected over 10 minutes ago.
void Controller::SharedMemStats(void * config){
  DEBUG_MSG(DLVL_HIGH, "Starting stats thread");
  IPC::sharedServer statServer(SHM_STATISTICS, STAT_EX_SIZE, true);
  statPointer = &statServer;
  std::set<std::string> inactiveStreams;
  while(((Util::Config*)config)->is_active){
    {
      tthread::lock_guard<tthread::mutex> guard(Controller::configMutex);
      tthread::lock_guard<tthread::mutex> guard2(statsMutex);
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
    }
    Util::wait(1000);
  }
  statPointer = 0;
  DEBUG_MSG(DLVL_HIGH, "Stopping stats thread");
}

/// Updates the given active connection with new stats data.
void Controller::statSession::update(unsigned long index, IPC::statExchange & data){
  curConns[index].update(data);
  //store timestamp of last received data, if newer
  if (data.now() > lastSec){
    lastSec = data.now();
  }
  //store timestamp of first received data, if older
  if (firstSec > data.now()){
    firstSec = data.now();
  }
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
  long long retVal = 0;
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
  long long retVal = 0;
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

Controller::statStorage::statStorage(){
  removeDown = 0;
  removeUp = 0;
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
  tmp.down = data.down() - removeDown;
  tmp.up = data.up() - removeUp;
  if (!log.size() && tmp.down + tmp.up > COUNTABLE_BYTES){
    //substract the start values if they are too high - this is a resumed connection of some sort
    removeDown = tmp.down;
    removeUp = tmp.up;
  }
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
