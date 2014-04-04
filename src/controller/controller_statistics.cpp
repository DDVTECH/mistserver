#include <cstdio>
#include <mist/config.h>
#include "controller_statistics.h"
#include "controller_limits.h"

/// The STAT_CUTOFF define sets how many seconds of statistics history is kept.
#define STAT_CUTOFF 600

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
#define STAT_CLI_ALL 0xFFFF
// These are used to store "totals" field requests in a bitfield for speedup.
#define STAT_TOT_CLIENTS 1
#define STAT_TOT_BPS_DOWN 2
#define STAT_TOT_BPS_UP 4
#define STAT_TOT_ALL 0xFF


std::multimap<unsigned long long int, Controller::statStorage> Controller::oldConns;///<Old connections, sorted on disconnect timestamp
std::map<unsigned long, Controller::statStorage> Controller::curConns;///<Connection storage, sorted on page location.

/// This function runs as a thread and roughly once per second retrieves
/// statistics from all connected clients, as well as wipes
/// old statistics that have disconnected over 10 minutes ago.
void Controller::SharedMemStats(void * config){
  DEBUG_MSG(DLVL_HIGH, "Starting stats thread");
  IPC::sharedServer statServer("statistics", 88, true);
  while(((Util::Config*)config)->is_active){
    //parse current users
    statServer.parseEach(parseStatistics);
    //wipe old statistics
    while (oldConns.size() && oldConns.begin()->first < (unsigned long long)(Util::epoch() - STAT_CUTOFF)){
      oldConns.erase(oldConns.begin());
    }
    Controller::checkServerLimits(); /*LTS*/
    Util::sleep(1000);
  }
  DEBUG_MSG(DLVL_HIGH, "Stopping stats thread");
}

/// This function is called by parseStatistics.
/// It updates the internally saved statistics data.
void Controller::statStorage::update(IPC::statExchange & data) {
  if (streamName == ""){
    host = data.host();
    streamName = data.streamName();
    connector = data.connector();
  }
  statLog tmp;
  tmp.time = data.time();
  tmp.lastSecond = data.lastSecond();
  tmp.down = data.down();
  tmp.up = data.up();
  log[data.now()] = tmp;
  //wipe data older than approx. STAT_CUTOFF seconds
  if (log.size() > STAT_CUTOFF){
    log.erase(log.begin());
  }
}
  
/// This function is called by the shared memory page that holds statistics.
/// It updates the internally saved statistics data, archiving if neccessary.
void Controller::parseStatistics(char * data, size_t len, unsigned int id){
  IPC::statExchange tmpEx(data);
  curConns[id].update(tmpEx);
  char counter = (*(data - 1));
  if (counter == 126 || counter == 127 || counter == 254 || counter == 255){
    oldConns.insert(std::pair<unsigned long long int, statStorage>(Util::epoch(), curConns[id]));
    curConns.erase(id);
  }
  /*LTS-START*/
  //if (counter < 125 && Controller::isBlacklisted(tmpEx.host(), ID, tmpEx.time())){
  //  (*(data - 1)) = 128;//Send disconnect message;
  //}
  /*LTS-END*/
}

/// Returns true if this stream has at least one connected client.
bool Controller::hasViewers(std::string streamName){
  if (curConns.size()){
    for (std::map<unsigned long, statStorage>::iterator it = curConns.begin(); it != curConns.end(); it++){
      if (it->second.streamName == streamName){
        return true;
      }
    }
  }
  return false;
}

/// This takes a "clients" request, and fills in the response data.
/// 
/// \api
/// `"client"` requests take the form of:
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
void Controller::fillClients(JSON::Value & req, JSON::Value & rep){
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
    for (JSON::ArrIter it = req["fields"].ArrBegin(); it != req["fields"].ArrEnd(); it++){
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
    for (JSON::ArrIter it = req["streams"].ArrBegin(); it != req["streams"].ArrEnd(); it++){
      streams.insert((*it).asStringRef());
    }
  }
  //figure out what protocols are wanted
  std::set<std::string> protos;
  if (req.isMember("protocols") && req["protocols"].size()){
    for (JSON::ArrIter it = req["protocols"].ArrBegin(); it != req["protocols"].ArrEnd(); it++){
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
  //output the data itself
  rep["data"].null();
  //start with current connections
  if (curConns.size()){
    for (std::map<unsigned long, statStorage>::iterator it = curConns.begin(); it != curConns.end(); it++){
      unsigned long long time = reqTime;
      if (now){time = it->second.log.rbegin()->first;}
      //data present and wanted? insert it!
      if ((it->second.log.rbegin()->first >= time && it->second.log.begin()->first <= time) && (!streams.size() || streams.count(it->second.streamName)) && (!protos.size() || protos.count(it->second.connector))){
        JSON::Value d;
        std::map<unsigned long long, statLog>::iterator statRef = it->second.log.lower_bound(time);
        std::map<unsigned long long, statLog>::iterator prevRef = --(it->second.log.lower_bound(time));
        if (fields & STAT_CLI_HOST){d.append(it->second.host);}
        if (fields & STAT_CLI_STREAM){d.append(it->second.streamName);}
        if (fields & STAT_CLI_PROTO){d.append(it->second.connector);}
        if (fields & STAT_CLI_CONNTIME){d.append((long long)statRef->second.time);}
        if (fields & STAT_CLI_POSITION){d.append((long long)statRef->second.lastSecond);}
        if (fields & STAT_CLI_DOWN){d.append(statRef->second.down);}
        if (fields & STAT_CLI_UP){d.append(statRef->second.up);}
        if (fields & STAT_CLI_BPS_DOWN){
          if (statRef != it->second.log.begin()){
            unsigned int diff = statRef->first - prevRef->first;
            d.append((statRef->second.down - prevRef->second.down) / diff);
          }else{
            d.append(statRef->second.down);
          }
        }
        if (fields & STAT_CLI_BPS_UP){
          if (statRef != it->second.log.begin()){
            unsigned int diff = statRef->first - prevRef->first;
            d.append((statRef->second.up - prevRef->second.up) / diff);
          }else{
            d.append(statRef->second.up);
          }
        }
        rep["data"].append(d);
      }
    }
  }
  //if we're only interested in current, don't even bother looking at history
  if (now){
    return;
  }
  //look at history
  if (oldConns.size()){
    for (std::map<unsigned long long int, statStorage>::iterator it = oldConns.begin(); it != oldConns.end(); it++){
      //data present and wanted? insert it!
      if ((it->second.log.rbegin()->first >= (unsigned long long)reqTime && it->second.log.begin()->first <= (unsigned long long)reqTime) && (!streams.size() || streams.count(it->second.streamName)) && (!protos.size() || protos.count(it->second.connector))){
        JSON::Value d;
        std::map<unsigned long long, statLog>::iterator statRef = it->second.log.lower_bound(reqTime);
        std::map<unsigned long long, statLog>::iterator prevRef = --(it->second.log.lower_bound(reqTime));
        if (fields & STAT_CLI_HOST){d.append(it->second.host);}
        if (fields & STAT_CLI_STREAM){d.append(it->second.streamName);}
        if (fields & STAT_CLI_PROTO){d.append(it->second.connector);}
        if (fields & STAT_CLI_CONNTIME){d.append((long long)statRef->second.time);}
        if (fields & STAT_CLI_POSITION){d.append((long long)statRef->second.lastSecond);}
        if (fields & STAT_CLI_DOWN){d.append(statRef->second.down);}
        if (fields & STAT_CLI_UP){d.append(statRef->second.up);}
        if (fields & STAT_CLI_BPS_DOWN){
          if (statRef != it->second.log.begin()){
            unsigned int diff = statRef->first - prevRef->first;
            d.append((statRef->second.down - prevRef->second.down) / diff);
          }else{
            d.append(statRef->second.down);
          }
        }
        if (fields & STAT_CLI_BPS_UP){
          if (statRef != it->second.log.begin()){
            unsigned int diff = statRef->first - prevRef->first;
            d.append((statRef->second.up - prevRef->second.up) / diff);
          }else{
            d.append(statRef->second.up);
          }
        }
        rep["data"].append(d);
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
void Controller::fillTotals(JSON::Value & req, JSON::Value & rep){
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
    for (JSON::ArrIter it = req["fields"].ArrBegin(); it != req["fields"].ArrEnd(); it++){
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
    for (JSON::ArrIter it = req["streams"].ArrBegin(); it != req["streams"].ArrEnd(); it++){
      streams.insert((*it).asStringRef());
    }
  }
  //figure out what protocols are wanted
  std::set<std::string> protos;
  if (req.isMember("protocols") && req["protocols"].size()){
    for (JSON::ArrIter it = req["protocols"].ArrBegin(); it != req["protocols"].ArrEnd(); it++){
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
  //start with current connections
  if (curConns.size()){
    for (std::map<unsigned long, statStorage>::iterator it = curConns.begin(); it != curConns.end(); it++){
      //data present and wanted? insert it!
      if (it->second.log.size() > 1 && (it->second.log.rbegin()->first >= (unsigned long long)reqStart || it->second.log.begin()->first <= (unsigned long long)reqEnd) && (!streams.size() || streams.count(it->second.streamName)) && (!protos.size() || protos.count(it->second.connector))){
        //keep track of the previous and current, starting at position 2 so there's always a delta down/up value.
        std::map<unsigned long long, statLog>::iterator pi = it->second.log.begin();
        for (std::map<unsigned long long, statLog>::iterator li = ++(it->second.log.begin()); li != it->second.log.end(); li++){
          if (li->first < (unsigned long long)reqStart || pi->first > (unsigned long long)reqEnd){
            continue;
          }
          unsigned int diff = li->first - pi->first;
          unsigned int ddown = (li->second.down - pi->second.down) / diff;
          unsigned int dup = (li->second.up - pi->second.up) / diff;
          for (long long unsigned int t = pi->first; t < li->first; t++){
            if (t >= (unsigned long long)reqStart && t <= (unsigned long long)reqEnd){
              totalsCount[t].add(ddown, dup);
            }
          }
          pi = li;//set previous iterator to log iterator
        }
      }
    }
  }
  //look at history
  if (oldConns.size()){
    for (std::map<unsigned long long int, statStorage>::iterator it = oldConns.begin(); it != oldConns.end(); it++){
      //data present and wanted? insert it!
      if (it->second.log.size() > 1 && (it->second.log.rbegin()->first >= (unsigned long long)reqStart || it->second.log.begin()->first <= (unsigned long long)reqEnd) && (!streams.size() || streams.count(it->second.streamName)) && (!protos.size() || protos.count(it->second.connector))){
        //keep track of the previous and current, starting at position 2 so there's always a delta down/up value.
        std::map<unsigned long long, statLog>::iterator pi = it->second.log.begin();
        for (std::map<unsigned long long, statLog>::iterator li = ++(it->second.log.begin()); li != it->second.log.end(); li++){
          if (li->first < (unsigned long long)reqStart || pi->first > (unsigned long long)reqEnd){
            continue;
          }
          unsigned int diff = li->first - pi->first;
          unsigned int ddown = (li->second.down - pi->second.down) / diff;
          unsigned int dup = (li->second.up - pi->second.up) / diff;
          for (long long unsigned int t = pi->first; t < li->first; t++){
            if (t >= (unsigned long long)reqStart && t <= (unsigned long long)reqEnd){
              totalsCount[t].add(ddown, dup);
            }
          }
          pi = li;//set previous iterator to log iterator
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
