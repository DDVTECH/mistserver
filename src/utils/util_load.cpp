#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdint.h>
#include <mist/defines.h>
#include <mist/downloader.h>
#include <mist/timing.h>
#include <mist/util.h>
#include <mist/encryption.h>
#include <mist/triggers.h>
#include <set>
#include "util_load.h"
#include <mist/encryption.h>
#include <mist/auth.h>
#include <mist/encode.h>
#include <mist/websocket.h>
#include <mist/tinythread.h>
#include <string>
#include <ctime>
#include <unistd.h>


//transmision json names
std::string const CONFSTREAMSKEY = "conf_streams";
std::string const TAGSKEY = "tags";
std::string const STREAMSKEY = "streams";
std::string const OUTPUTSKEY = "outputs";
std::string const FILLSTATEOUT = "fillStateOut";
std::string const FILLSTREAMSOUT = "fillStreamsOut";
std::string const SCORESOURCE = "scoreSource";
std::string const SCORERATE = "scoreRate";
std::string const CPUKEY = "cpu";
std::string const SERVLATIKEY = "servLati";
std::string const SERVLONGIKEY = "servLongi";
std::string const HOSTNAMEKEY = "hostName";
std::string const RAMKEY = "ram";
std::string const BWKEY = "bw";
std::string const GEOKEY = "geo";
std::string const BONUSKEY = "bonus";
std::string const SAVEKEY = "save";
std::string const LOADKEY = "load";
std::string const IDENTIFIERKEY = "LB";
std::string const TOADD = "toadd";
std::string const CURRBANDWIDTHKEY = "currBandwidth";
std::string const AVAILBANDWIDTHKEY = "availBandwidth";
std::string const CURRRAMKEY = "currram";
std::string const RAMMAXKEY = "ramMax";
std::string const BINHOSTKEY = "binhost";
std::string const BALANCEKEY = "balance";
std::string const ADDUSERKEY = "auser";
std::string const ADDPASSKEY = "apass";
std::string const ADDSALTKEY = "asalt";
std::string const ADDWHITELISTKEY = "awhitelist";
std::string const RUSERKEY = "ruser";
std::string const RPASSKEY = "rpass";
std::string const RSALTKEY = "rsalt";
std::string const RWHITELISTKEY = "rwhitelist";

//balancing transmision json names
std::string const MINSTANDBYKEY = "minstandby";
std::string const MAXSTANDBYKEY = "maxstandby";
std::string const CAPPACITYTRIGGERCPUDECKEY = "triggerdecrementcpu"; //percentage om cpu te verminderen
std::string const CAPPACITYTRIGGERBWDECKEY = "triggerdecrementbandwidth"; //percentage om bandwidth te verminderen
std::string const CAPPACITYTRIGGERRAMDECKEY = "triggerdecrementram"; //percentage om ram te verminderen
std::string const CAPPACITYTRIGGERCPUKEY = "triggercpu"; //max capacity trigger for balancing cpu
std::string const CAPPACITYTRIGGERBWKEY = "triggerbandwidth";  //max capacity trigger for balancing bandwidth
std::string const CAPPACITYTRIGGERRAMKEY = "triggerram"; //max capacity trigger for balancing ram
std::string const HIGHCAPPACITYTRIGGERCPUKEY = "balancingtriggercpu"; //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERCPU
std::string const HIGHCAPPACITYTRIGGERBWKEY = "balancingtriggerbandwidth";  //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERBW
std::string const HIGHCAPPACITYTRIGGERRAMKEY = "balancingtriggerram"; //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERRAM
std::string const LOWCAPPACITYTRIGGERCPUKEY = "balancingminimumtriggercpu"; //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERCPU
std::string const LOWCAPPACITYTRIGGERBWKEY = "balancingminimumtriggerbandwidth";  //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERBW
std::string const LOWCAPPACITYTRIGGERRAMKEY = "balancingminimumtriggerram"; //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERRAM
std::string const BALANCINGINTERVALKEY = "balancinginterval";
std::string const STANDBYKEY = "standby";
std::string const REMOVESTANDBYKEY = "removestandby";
std::string const LOCKKEY = "lock";

//const websocket api names set multiple times
std::string const ADDLOADBALANCER = "addloadbalancer";
std::string const REMOVELOADBALANCER = "removeloadbalancer";
std::string const RESEND = "resend";
std::string const REMOVEHOST = "removehost";
std::string const UPDATEHOST = "updatehost";
std::string const WEIGHTS = "weights";
std::string const ADDVIEWER = "addviewer";
std::string const HOST = "host";
std::string const ADDSERVER = "addserver";
std::string const REMOVESERVER = "removeServer";
std::string const SENDCONFIG = "configExchange";

//config file names
std::string const CONFIGFALLBACK = "fallback";
std::string const CONFIGC = "weight_cpu";
std::string const CONFIGR = "weight_ram";
std::string const CONFIGBW = "weight_bw";
std::string const CONFIGWG = "weight_geo";
std::string const CONFIGWB = "weight_bonus";
std::string const CONFIGPASS = "passHash";
std::string const CONFIGSPASS = "passphrase";
std::string const CONFIGPORT = "port";
std::string const CONFIGINTERFACE = "interface";
std::string const CONFIGWHITELIST = "whitelist";
std::string const CONFIGBEARER = "bearer_tokens";
std::string const CONFIGUSERS = "user_auth";
std::string const CONFIGSERVERS = "server_list";
std::string const CONFIGLOADBALANCER = "loadbalancers";

//balancing config file names
std::string const CONFIGMINSTANDBY = "minstandby";
std::string const CONFIGMAXSTANDBY = "maxstandby";
std::string const CONFIGCAPPACITYTRIGGERCPUDEC = "triggerdecrementcpu"; //percentage om cpu te verminderen
std::string const CONFIGCAPPACITYTRIGGERBWDEC = "triggerdecrementbandwidth"; //percentage om bandwidth te verminderen
std::string const CONFIGCAPPACITYTRIGGERRAMDEC = "triggerdecrementram"; //percentage om ram te verminderen
std::string const CONFIGCAPPACITYTRIGGERCPU = "triggercpu"; //max capacity trigger for balancing cpu
std::string const CONFIGCAPPACITYTRIGGERBW = "triggerbandwidth";  //max capacity trigger for balancing bandwidth
std::string const CONFIGCAPPACITYTRIGGERRAM = "triggerram"; //max capacity trigger for balancing ram
std::string const CONFIGHIGHCAPPACITYTRIGGERCPU = "balancingtriggercpu"; //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERCPU
std::string const CONFIGHIGHCAPPACITYTRIGGERBW = "balancingtriggerbandwidth";  //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERBW
std::string const CONFIGHIGHCAPPACITYTRIGGERRAM = "balancingtriggerram"; //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERRAM
std::string const CONFIGLOWCAPPACITYTRIGGERCPU = "balancingminimumtriggercpu"; //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERCPU
std::string const CONFIGLOWCAPPACITYTRIGGERBW = "balancingminimumtriggerbandwidth";  //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERBW
std::string const CONFIGLOWCAPPACITYTRIGGERRAM = "balancingminimumtriggerram"; //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERRAM
std::string const CONFIGBALANCINGINTERVAL = "balancinginterval";

//streamdetails names
std::string const STREAMDETAILSTOTAL = "total";
std::string const STREAMDETAILSINPUTS = "inputs";
std::string const STREAMDETAILSBANDWIDTH = "bandwidth";
std::string const STREAMDETAILSPREVTOTAL = "prevTotal";
std::string const STREAMDETAILSBYTESUP = "bytesUp";
std::string const STREAMDETAILSBYTESDOWN = "bytesDown";

//outurl names
std::string const OUTURLPRE = "pre";
std::string const OUTURLPOST = "post";


//rebalancing
int MINSTANDBY = 1;
int MAXSTANDBY = 1;
double CAPPACITYTRIGGERCPUDEC = 0.01; //percentage om cpu te verminderen
double CAPPACITYTRIGGERBWDEC = 0.01; //percentage om bandwidth te verminderen
double CAPPACITYTRIGGERRAMDEC = 0.01; //percentage om ram te verminderen
double CAPPACITYTRIGGERCPU = 0.9; //max capacity trigger for balancing cpu
double CAPPACITYTRIGGERBW = 0.9;  //max capacity trigger for balancing bandwidth
double CAPPACITYTRIGGERRAM = 0.9; //max capacity trigger for balancing ram
double HIGHCAPPACITYTRIGGERCPU = 0.8; //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERCPU
double HIGHCAPPACITYTRIGGERBW = 0.8;  //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERBW
double HIGHCAPPACITYTRIGGERRAM = 0.8; //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERRAM
double LOWCAPPACITYTRIGGERCPU = 0.3; //capacity at which considerd almost empty. should be less than CAPPACITYTRIGGERCPU
double LOWCAPPACITYTRIGGERBW = 0.3;  //capacity at which considerd almost empty. should be less than CAPPACITYTRIGGERBW
double LOWCAPPACITYTRIGGERRAM = 0.3; //capacity at which considerd almost empty. should be less than CAPPACITYTRIGGERRAM
int BALANCINGINTERVAL = 1000;


Util::Config *cfg = 0;
std::string passphrase;
std::string fallback;
std::string myName;
tthread::mutex globalMutex;
tthread::mutex fileMutex;
std::map<std::string, int32_t> blankTags;

//server balancing weights
size_t weight_cpu = 500;
size_t weight_ram = 500;
size_t weight_bw = 1000;
size_t weight_geo = 1000;
size_t weight_bonus = 50;


API api;
std::set<hostEntry*> hosts; //array holding all hosts
std::set<LoadBalancer*> loadBalancers; //array holding all load balancers in the mesh
#define SERVERMONITORLIMIT 1

//authentication storage
std::map<std::string,std::pair<std::string, std::string> > userAuth;//username: (passhash, salt)
std::set<std::string> bearerTokens;
std::string passHash;
std::set<std::string> whitelist;
std::map<std::string, std::time_t> activeSalts;
std::string identifier;
std::set<std::string> identifiers;
#define SALTSIZE 10

//file save and loading vars
std::string const fileloc  = "config.txt";
tthread::thread* saveTimer;
std::time_t prevConfigChange;//time of last config change
std::time_t prevSaveTime;//time of last save

//timer vars
#define PROMETHEUSMAXTIMEDIFF 180 //time prometheusnodes stay in system
#define PROMETHEUSTIMEINTERVAL 10 //time prometheusnodes receive data
#define SAVETIMEINTERVAL 5 //time to save after config change in minutes
std::time_t now;

prometheusDataNode lastPromethNode;
std::map<time_t, prometheusDataNode>  prometheusData;

/**
 * creates new prometheus data node every PROMETHEUSTIMEINTERVAL
*/
static void prometheusTimer(void*){
  while(cfg->is_active){
    //create new data node
    lastPromethNode = prometheusDataNode();
    time(&now);
    
    std::map<time_t, prometheusDataNode>::reverse_iterator it = prometheusData.rbegin();
    //remove old data
    while(it != prometheusData.rend()){
      double timeDiff = difftime(now,(*it).first);
      if(timeDiff >= 60*PROMETHEUSMAXTIMEDIFF){
        prometheusData.erase((*it).first);
        it = prometheusData.rbegin();
      }else{
        it++;
      }
    }
    //add new data node to data collection
    prometheusData.insert(std::pair<time_t, prometheusDataNode>(now, lastPromethNode));
    
    sleep(PROMETHEUSTIMEINTERVAL*60);
  }
}
/**
 * return JSON with all prometheus data nodes
*/
JSON::Value handlePrometheus(){
  JSON::Value res;
  for(std::map<time_t, prometheusDataNode>::iterator i = prometheusData.begin(); i != prometheusData.end(); i++){
    for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      JSON::Value serv;
      serv["number of reconnects"] = (*i).second.numReconnectServer[(*it)->name];
      serv["number of successful connects"] = (*i).second.numSuccessConnectServer[(*it)->name];
      serv["number of failed connects"] = (*i).second.numFailedConnectServer[(*it)->name];
      serv["number of new viewers to this server"] = (*i).second.numStreams[(*it)->name];
      res[(*i).first]["servers"][(std::string)(*it)->name] = serv;
    }
    for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
      JSON::Value serv;
      serv["number of reconnects"] = (*i).second.numReconnectLB[(*it)->getName()];
      serv["number of successful connects"] = (*i).second.numSuccessConnectLB[(*it)->getName()];
      serv["number of failed connects"] = (*i).second.numFailedConnectLB[(*it)->getName()];
      res[(*i).first]["load balancers"][(*it)->getName()] = serv;
    }
    res[(*i).first]["successful viewers assignments"] = (*i).second.numSuccessViewer;
    res[(*i).first]["failed viewers assignments"] = (*i).second.numFailedViewer;
    res[(*i).first]["Illegal viewers assignments"] = (*i).second.numIllegalViewer;

    res[(*i).first]["successful source requests"] = (*i).second.numSuccessSource;
    res[(*i).first]["failed source requests"] = (*i).second.numFailedSource;

    res[(*i).first]["successful ingest requests"] = (*i).second.numSuccessIngest;
    res[(*i).first]["failed ingest requests"] = (*i).second.numFailedIngest;

    res[(*i).first]["successful api requests"] = (*i).second.numSuccessRequests;
    res[(*i).first]["failed api requests"] = (*i).second.numFailedRequests;
    res[(*i).first]["Illegal api requests"] = (*i).second.numIllegalRequests;

    res[(*i).first]["successful load balancer requests"] = (*i).second.numLBSuccessRequests;
    res[(*i).first]["failed load balancer requests"] = (*i).second.numLBFailedRequests;
    res[(*i).first]["Illegal load balancer requests"] = (*i).second.numLBIllegalRequests;

    res[(*i).first]["successful login attempts"] = (*i).second.goodAuth;
    res[(*i).first]["failed login attempts"] = (*i).second.badAuth;
  }
  return res;

}


/**
 * timer to send the add viewer data
*/
static void timerAddViewer(void*){
  while(cfg->is_active){
    JSON::Value j;
    for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      std::string name = (*it)->name;
      j[ADDVIEWER][name] = (*it)->details->getAddBandwidth();
    }
    for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it !=loadBalancers.end(); it++){
      (*it)->send(j.asString());
    }
    sleep(100);
  }
}


/**
 * construct an object to represent an other load balancer
*/
LoadBalancer::LoadBalancer(HTTP::Websocket* ws, std::string name, std::string ident) : LoadMutex(0), ws(ws), name(name), ident(ident), state(true), Go_Down(false) {}
LoadBalancer::~LoadBalancer(){
  if(LoadMutex){
    delete LoadMutex;
    LoadMutex = 0;
  }
  //ws->getSocket().close();
  delete ws;
  ws = 0;
}
/**
 * \return the address of this load balancer
*/
std::string LoadBalancer::getName() const {return name;}
std::string LoadBalancer::getIdent() const {return ident;}
/**
 * allows for ordering of load balancers
*/
bool LoadBalancer::operator < (const LoadBalancer &other) const {return this->getName() < other.getName();}
/**
 * allows for ordering of load balancers
*/
bool LoadBalancer::operator > (const LoadBalancer &other) const {return this->getName() > other.getName();}
/**
 * \returns true if \param other is equivelent to this load balancer
*/
bool LoadBalancer::operator == (const LoadBalancer &other) const {return this->getName().compare(other.getName());}
/**
 * \returns true only if \param other is the ip of this load balancer
*/
bool LoadBalancer::operator == (const std::string &other) const {return this->getName().compare(other);}
/**
 * send \param ret to the load balancer represented by this object
*/
void LoadBalancer::send(std::string ret) const {
    if(!Go_Down && state){//prevent sending when shuting down
      ws->sendFrame(ret);
    }
}


/**
 * \returns this object as a string
*/
JSON::Value streamDetails::stringify() const{
  JSON::Value out;
  out[STREAMDETAILSTOTAL] = total;
  out[STREAMDETAILSINPUTS] = inputs;
  out[STREAMDETAILSBANDWIDTH] = bandwidth;
  out[STREAMDETAILSPREVTOTAL] = prevTotal;
  out[STREAMDETAILSBYTESUP] = bytesUp;
  out[STREAMDETAILSBYTESDOWN] = bytesDown;
  return out;
}
/**
 * \returns \param j as a streamDetails object
*/
streamDetails* streamDetails::destringify(JSON::Value j){
    streamDetails* out = new streamDetails();
    out->total = j[STREAMDETAILSTOTAL].asInt();
    out->inputs = j[STREAMDETAILSINPUTS].asInt();
    out->bandwidth = j[STREAMDETAILSBANDWIDTH].asInt();
    out->prevTotal = j[STREAMDETAILSPREVTOTAL].asInt();
    out->bytesUp = j[STREAMDETAILSBYTESUP].asInt();
    out->bytesDown = j[STREAMDETAILSBYTESDOWN].asInt();
    return out;
  }

outUrl::outUrl(){};
outUrl::outUrl(const std::string &u, const std::string &host){
  std::string tmp = u;
  if (u.find("HOST") != std::string::npos){
    tmp = u.substr(0, u.find("HOST")) + host + u.substr(u.find("HOST") + 4);
  }
  size_t dolsign = tmp.find('$');
  pre = tmp.substr(0, dolsign);
  if (dolsign != std::string::npos){post = tmp.substr(dolsign + 1);}
}
/**
 * turn outUrl into string
*/
JSON::Value outUrl::stringify() const{
  JSON::Value j;
  j[OUTURLPRE] = pre;
  j[OUTURLPOST] = post;
  return j;
}
/**
 * turn json \param j into outUrl
*/
outUrl outUrl::destringify(JSON::Value j){
  outUrl r;
  r.pre = j[OUTURLPRE].asString();
  r.post = j[OUTURLPOST].asString();
  return r;
}

/**
 * convert set \param s to json
*/
JSON::Value convertSetToJson(std::set<std::string> s){
  JSON::Value tmp;

  for(std::set<std::string>::iterator it = s.begin(); it != s.end(); ++it){
      tmp.append(*it);
  }
  return tmp;
}
/**
 * convert maps<string, object> \param s to json where the object has a stringify function
*/
template<typename data>
JSON::Value convertMapToJson(std::map<std::string, data> s){
  JSON::Value out;  
  for(typename std::map<std::string, data>::iterator it = s.begin(); it != s.end(); ++it){
      out[(*it).first] = (*it).second.stringify();
  }
  return out;
}
/**
 * convert a map<string, string> \param s to a json
*/
JSON::Value convertMapToJson(std::map<std::string, std::string> s){
  JSON::Value out;  
  for(typename std::map<std::string, std::string>::iterator it = s.begin(); it != s.end(); ++it){
      out[(*it).first] = (*it).second;
  }
  return out;
}
/**
 * convert a map<string, string> \param s to a json
*/
JSON::Value convertMapToJson(std::map<std::string, std::pair<std::string, std::string> > s){
  JSON::Value out;  
  for(typename std::map<std::string,std::pair<std::string, std::string> >::iterator it = s.begin(); it != s.end(); ++it){
      out[(*it).first].append((*it).second.first);
      out[(*it).first].append((*it).second.second);

  }
  return out;
}
/**
 * convert a json \param j to a set<string>
*/
std::set<std::string> convertJsonToSet(JSON::Value j){
  std::set<std::string> s;
  jsonForEach(j, it){
    s.insert(it->asString());
  } 
  return s;
}
/**
 * convert json \param j to map<string, string>
*/
std::map<std::string, std::string> convertJsonToMap(JSON::Value j){
  std::map<std::string, std::string> m;
  jsonForEach(j,i){
    m.insert(std::pair<std::string, std::string>(i.key(),(*i).asString()));
  }
  return m;
}
/**
 * convert json \param j to map<string, string>
*/
std::map<std::string, std::pair<std::string, std::string> > convertJsonToMapPair(JSON::Value j){
  std::map<std::string, std::pair<std::string, std::string> > m;
  jsonForEach(j,i){
    JSON::Iter z(*i);
    std::string first = (*z).asString();
    ++z;
    std::string second = (*z).asString();
    m.insert(std::make_pair (i.key(),std::make_pair(first, second)));
  }
  return m;
}


int32_t applyAdjustment(const std::set<std::string> & tags, const std::string & match, int32_t adj) {
  if (!match.size()){return 0;}
  bool invert = false;
  bool haveOne = false;
  size_t prevPos = 0;
  if (match[0] == '-'){
    invert = true;
    prevPos = 1;
  }
  //Check if any matches inside tags
  size_t currPos = match.find(',', prevPos);
  while (currPos != std::string::npos){
    if (tags.count(match.substr(prevPos, currPos-prevPos))){haveOne = true;}
    prevPos = currPos + 1;
    currPos = match.find(',', prevPos);
  }
  if (tags.count(match.substr(prevPos))){haveOne = true;}
  //If we have any match, apply adj, unless we're doing an inverted search, then return adj on zero matches
  if (haveOne == !invert){return adj;}
  return 0;
}
/**
 * convert degrees to radians
*/
inline double toRad(double degree) {
  return degree / 57.29577951308232087684;
}
/**
 * calcuate distance between 2 coordinates
*/
double geoDist(double lat1, double long1, double lat2, double long2) {
  double dist;
  dist = sin(toRad(lat1)) * sin(toRad(lat2)) + cos(toRad(lat1)) * cos(toRad(lat2)) * cos(toRad(long1 - long2));
  return .31830988618379067153 * acos(dist);
}

/**
 * construct server data object of server monitored in mesh
 * \param LB contains the load balancer that created this object null if this load balancer
 * \param name is the name of the server
*/
hostDetails::hostDetails(char* name) : hostMutex(0), name(name), ramCurr(0), ramMax(0), availBandwidth(128 * 1024 * 1024), addBandwidth(0), balanceCPU(0), balanceRAM(0), balanceBW(0), balanceRedirect("") {}
/**
 * destructor
*/
hostDetails::~hostDetails(){
    if(hostMutex){
      delete hostMutex;
      hostMutex = 0;
    }
  }
/**
 * \returns JSON of server status
*/
JSON::Value hostDetails::getServerData(){
  JSON::Value j;
  j["cpu"] = cpu;
  j["currRam"] = ramCurr;
  j["maxRam"] = ramMax;
  j["currBW"] = currBandwidth;
  j["BWLimit"] = availBandwidth;
  return j;
}
/**
 * \returns the value to add to the bandwidth to prevent overflow
*/
uint64_t hostDetails::getAddBandwidth(){
  uint64_t ret = addBandwidth;
  prevAddBandwidth += addBandwidth;
  addBandwidth = 0; 
  return ret;
}
/**
   *  Fills out a by reference given JSON::Value with current state.
   */
void hostDetails::fillState(JSON::Value &r) const{
    r = fillStateOut;
  }
/** 
   * Fills out a by reference given JSON::Value with current streams viewer count.
   */
void hostDetails::fillStreams(JSON::Value &r) const{
    r = fillStreamsOut;
  }
/**
   * Fills out a by reference given JSON::Value with current stream statistics.
   */ 
void hostDetails::fillStreamStats(const std::string &s, JSON::Value &r) const{
    if(!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
    for (std::map<std::string, streamDetails>::const_iterator jt = streams.begin(); jt != streams.end(); ++jt){
      const std::string &n = jt->first;
      if(s!= "*" && n!= s && n.substr(0,s.size()+1) != s+"+"){continue;}
      if(!r.isMember(n)){
        r[n].append(jt->second.total);
        r[n].append(jt->second.bandwidth);
        r[n].append(jt->second.bytesUp);
        r[n].append(jt->second.bytesDown);
      }else {
        r[n][0u] = r[n][0u].asInt() + jt->second.total;
        r[n][2u] = r[n][2u].asInt() + jt->second.bytesUp;
        r[n][3u] = r[n][3u].asInt() + jt->second.bytesDown;
      }
    }
  }
/**
 * get viewers of stream \param strm
*/
long long hostDetails::getViewers(const std::string &strm) const{
    if(!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
    if(!streams.count(strm)){return 0;}
    return streams.at(strm).total;
  }
/**
 * Scores a potential new connection to this server
 * 0 means not possible, the higher the better.
*/
uint64_t hostDetails::rate(std::string &s, const std::map<std::string, int32_t>  &tagAdjust = blankTags, double lati = 0, double longi = 0) const{
    if(conf_streams.size() && !conf_streams.count(s) && !conf_streams.count(s.substr(0, s.find_first_of("+ ")))){
      MEDIUM_MSG("Stream %s not available from %s", s.c_str(), host.c_str());
      return 0;
    }
    uint64_t score = scoreRate + (streams.count(s) ? weight_bonus : 0);
    uint64_t geo_score = 0;
    if(servLati && servLongi && lati && longi){
      geo_score = weight_geo - weight_geo * geoDist(servLati, servLongi, lati, longi);
      score += geo_score;
    }
    int64_t adjustment = 0;
    if (tagAdjust.size()){
      for (std::map<std::string, int32_t>::const_iterator it = tagAdjust.begin(); it != tagAdjust.end(); ++it){
        adjustment += applyAdjustment(tags, it->first, it->second);
      }
    }
    if (adjustment >= 0 || -adjustment < score){
      score += adjustment;
    }else{
      score = 0;
    }
    return score;
  }
/**
 * Scores this server as a source
 * 0 means not possible, the higher the better.
*/
uint64_t hostDetails::source(const std::string &s, const std::map<std::string, int32_t> &tagAdjust, uint32_t minCpu, double lati = 0, double longi = 0) const{
    if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
    if(s.size() && (!streams.count(s) || !streams.at(s).inputs)){return 0;}

    if (minCpu && cpu + minCpu >= 1000){return 0;}
    uint64_t score = scoreSource;
    int64_t adjustment = 0;
    if (tagAdjust.size()){
      for (std::map<std::string, int32_t>::const_iterator it = tagAdjust.begin(); it != tagAdjust.end(); ++it){
        adjustment += applyAdjustment(tags, it->first, it->second);
      }
    }
    if (adjustment >= 0 || -adjustment < score){
      score += adjustment;
    }else{
      score = 0;
    }
    return score;
  }
/**
 * give url of the server for a protocol, \param proto, and stream, \param s
*/
std::string hostDetails::getUrl(std::string &s, std::string &proto) const{
  if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
  tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
  if(!outputs.count(proto)){return "";}
  const outUrl o = outputs.at(proto);
  return o.pre + s + o.post;
}
/**
   * add the viewer to this host
   * updates all precalculated host vars
   */
void hostDetails::addViewer(std::string &s, bool RESEND){
    if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
    uint64_t toAdd = toAddB;
    if (streams.count(s)){
      toAdd = streams[s].bandwidth;
    }
    // ensure reasonable limits of bandwidth guesses
    if (toAdd < 64 * 1024){toAdd = 64 * 1024;}// minimum of 0.5 mbps
    if (toAdd > 1024 * 1024){toAdd = 1024 * 1024;}// maximum of 8 mbps
    addBandwidth += toAdd;
  }
/**
 * Update precalculated host vars.
*/
void hostDetails::update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource, uint64_t scoreRate, std::map<std::string, outUrl> outputs, std::set<std::string> conf_streams, std::map<std::string, streamDetails> streams, std::set<std::string> tags, uint64_t cpu, double servLati, double servLongi, const char* binHost, std::string host, uint64_t toAdd, uint64_t currBandwidth, uint64_t availBandwidth, uint64_t currRam, uint64_t ramMax){
    if(!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);

    this->fillStateOut = fillStateOut;
    this->fillStreamsOut = fillStreamsOut;
    this->scoreSource = scoreSource;
    this->scoreRate = scoreRate;
    this->outputs = outputs;
    this->conf_streams = conf_streams;
    this->streams = streams;
    this->tags = tags;
    this->cpu = cpu;
    *(this->binHost) = *binHost;
    this->host = host;
    this->toAddB = toAdd;
    

  }
/**
   * Update precalculated host vars without protected vars
   */
void hostDetails::update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource, uint64_t scoreRate, uint64_t toAdd){
    if(!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);

    this->fillStateOut = fillStateOut;
    this->fillStreamsOut = fillStreamsOut;
    this->scoreSource = scoreSource;
    this->scoreRate = scoreRate;
    toAddB = toAdd;
  }
/**
   * allow for json inputs instead of sets and maps for update function
   */
void hostDetails::update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource, uint64_t scoreRate, JSON::Value outputs, JSON::Value conf_streams, JSON::Value streams, JSON::Value tags, uint64_t cpu, double servLati, double servLongi, const char* binHost, std::string host, uint64_t toAdd, uint64_t currBandwidth, uint64_t availBandwidth, uint64_t currRam, uint64_t ramMax){  
    std::map<std::string, outUrl> out;
    std::map<std::string, streamDetails> s;
    streamDetails x;
    if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
    for(int i = 0; i < streams.size(); i++){
      s.insert(std::pair<std::string, streamDetails>(streams[STREAMSKEY][i]["key"], *(x.destringify(streams[STREAMSKEY][i]["streamDetails"]))));
    }
    update(fillStateOut, fillStreamsOut, scoreSource, scoreRate, out, convertJsonToSet(conf_streams), s, convertJsonToSet(tags), cpu, servLati, servLongi, binHost, host, toAdd, currBandwidth, availBandwidth, currRam, ramMax);
  }
  
/**
 * constructor of server monitored by this load balancer
*/
hostDetailsCalc::hostDetailsCalc(char* name) : hostDetails(name), total(0), upPrev(0), downPrev(0), prevTime(0), upSpeed(0), 
  downSpeed(0) {
    cpu = 1000;
    servLati = 0;
    servLongi = 0;
  }
hostDetailsCalc::~hostDetailsCalc(){
    if (hostMutex){
      delete hostMutex;
      hostMutex = 0;
    }
  }
void hostDetailsCalc::badNess(){
    if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
    addBandwidth += 1 * 1024 * 1024;
    addBandwidth *= 1.2;
  }
/// Fills out a by reference given JSON::Value with current state.
void hostDetailsCalc::fillState(JSON::Value &r) const{
    if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
    r["cpu"] = (uint64_t)(cpu / 10);
    if (ramMax){r["ram"] = (uint64_t)((ramCurr * 100) / ramMax);}
    r["up"] = upSpeed;
    r["up_add"] = addBandwidth + prevAddBandwidth;
    r["down"] = downSpeed;
    r["streams"] = streams.size();
    r["viewers"] = total;
    r["bwlimit"] = availBandwidth;
    if (servLati || servLongi){
      r["geo"]["lat"] = servLati;
      r["geo"]["lon"] = servLongi;
      r["geo"]["loc"] = servLoc;
    }
    if (tags.size()){
      for (std::set<std::string>::iterator it = tags.begin(); it != tags.end(); ++it){
        r[TAGSKEY].append(*it);
      }
    }
    if (ramMax && availBandwidth){
      r["score"]["cpu"] = (uint64_t)(weight_cpu - (cpu * weight_cpu) / 1000);
      r["score"]["ram"] = (uint64_t)(weight_ram - ((ramCurr * weight_ram) / ramMax));
      r["score"]["bw"] = (uint64_t)(weight_bw - (((upSpeed + addBandwidth + prevAddBandwidth) * weight_bw) / availBandwidth));
    }
  }
/// Fills out a by reference given JSON::Value with current streams viewer count.
void hostDetailsCalc::fillStreams(JSON::Value &r) const{
    if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);

    for (std::map<std::string, streamDetails>::const_iterator jt = streams.begin(); jt != streams.end(); ++jt){
      r[jt->first] = r[jt->first].asInt() + jt->second.total;
    }
  }
/// Scores a potential new connection to this server
  /// 0 means not possible, the higher the better.
uint64_t hostDetailsCalc::rate() const{
    if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
    if (!ramMax || !availBandwidth){
      WARN_MSG("Host %s invalid: RAM %" PRIu64 ", BW %" PRIu64, host.c_str(), ramMax, availBandwidth);
      return 0;
    }
    if (upSpeed >= availBandwidth || (upSpeed + addBandwidth + prevAddBandwidth) >= availBandwidth){
      INFO_MSG("Host %s over bandwidth: %" PRIu64 "+%" PRIu64 " >= %" PRIu64, host.c_str(), upSpeed,
               addBandwidth + prevAddBandwidth, availBandwidth);
      return 0;
    }
    
    // Calculate score
    uint64_t cpu_score = (weight_cpu - (cpu * weight_cpu) / 1000);
    uint64_t ram_score = (weight_ram - ((ramCurr * weight_ram) / ramMax));
    uint64_t bw_score = (weight_bw - (((upSpeed + addBandwidth +prevAddBandwidth) * weight_bw) / availBandwidth));
  
    uint64_t score = cpu_score + ram_score + bw_score;
    
    return score;
  }
/// Scores this server as a source
  /// 0 means not possible, the higher the better.
uint64_t hostDetailsCalc::source() const{
    if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
    
    if (!ramMax || !availBandwidth){
      WARN_MSG("Host %s invalid: RAM %" PRIu64 ", BW %" PRIu64, host.c_str(), ramMax, availBandwidth);
      return 1;
    }
    if (upSpeed >= availBandwidth || (upSpeed + addBandwidth + addBandwidth) >= availBandwidth){
      INFO_MSG("Host %s over bandwidth: %" PRIu64 "+%" PRIu64 " >= %" PRIu64, host.c_str(), upSpeed,
               addBandwidth + prevAddBandwidth, availBandwidth);
      return 1;
    }
    // Calculate score
    uint64_t cpu_score = (weight_cpu - (cpu * weight_cpu) / 1000);
    uint64_t ram_score = (weight_ram - ((ramCurr * weight_ram) / ramMax));
    uint64_t bw_score = (weight_bw - (((upSpeed + addBandwidth + prevAddBandwidth) * weight_bw) / availBandwidth));

    uint64_t score = cpu_score + ram_score + bw_score + 1;
    return score;
  }
/**
   * calculate and update precalculated variables
  */
void hostDetailsCalc::calc(){
    //calculated vars
    JSON::Value fillStreamsOut;
    fillStreams(fillStreamsOut);
    JSON::Value fillStateOut;
    fillState(fillStateOut);
    uint64_t scoreSource = source();
    uint64_t scoreRate = rate();
    uint64_t toadd;
    if (total){
      toadd = (upSpeed + downSpeed) / total;
    }else{
      toadd = 131072; // assume 1mbps
    }

    //update the local precalculated varsnclude <mist/config.h>

    ((hostDetails*)(this))->update(fillStateOut, fillStreamsOut, scoreSource, scoreRate, toadd);

    //create json to send to other load balancers
    JSON::Value j;
    j[FILLSTATEOUT] = fillStateOut;
    j[FILLSTREAMSOUT] = fillStreamsOut;
    j[SCORESOURCE] = scoreSource;
    j[SCORERATE] = scoreRate;
    j[BINHOSTKEY] = binHost;
    
    
    j[OUTPUTSKEY] = convertMapToJson(outputs);
    j[CONFSTREAMSKEY] = convertSetToJson(conf_streams);
    j[STREAMSKEY] = convertMapToJson(streams);
    j[TAGSKEY] = convertSetToJson(tags);
    
    j[CPUKEY] = cpu;
    j[SERVLATIKEY] = servLati;
    j[SERVLONGIKEY] = servLongi;
    j[HOSTNAMEKEY] = name;
    j[IDENTIFIERKEY] = identifier;

    j[CURRBANDWIDTHKEY] = upSpeed + downSpeed;
    j[AVAILBANDWIDTHKEY] = availBandwidth;
    j[CURRRAMKEY] = ramCurr;
    j[RAMMAXKEY] = ramMax;

    j[TOADD] = toadd;
    

    JSON::Value out;
    out["updateHost"] = j;

    //send to other load balancers
    for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
      (*it)->send(out.asString());
    }
  }
/**
   * update vars from server
   */
void hostDetailsCalc::update(JSON::Value &d){
    if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
    cpu = d["cpu"].asInt();
    if (d.isMember("bwlimit") && d["bwlimit"].asInt()){availBandwidth = d["bwlimit"].asInt();}
    if (d.isMember("loc")){
      if (d["loc"]["lat"].asDouble() != servLati){servLati = d["loc"]["lat"].asDouble();}
      if (d["loc"]["lon"].asDouble() != servLongi){servLongi = d["loc"]["lon"].asDouble();}
      if (d["loc"]["name"].asStringRef() != servLoc){servLoc = d["loc"]["name"].asStringRef();}
    }
    int64_t nRamMax = d["mem_total"].asInt();
    int64_t nRamCur = d["mem_used"].asInt();
    int64_t nShmMax = d["shm_total"].asInt();
    int64_t nShmCur = d["shm_used"].asInt();
    if (d.isMember(TAGSKEY) && d[TAGSKEY].isArray()){
      std::set<std::string> newTags;
      jsonForEach(d[TAGSKEY], tag){
        std::string t = tag->asString();
        if (t.size()){newTags.insert(t);}
      }
      if (newTags != tags){tags = newTags;}
    }
    if (!nRamMax){nRamMax = 1;}
    if (!nShmMax){nShmMax = 1;}
    if (((nRamCur + nShmCur) * 1000) / nRamMax > (nShmCur * 1000) / nShmMax){
      ramMax = nRamMax;
      ramCurr = nRamCur + nShmCur;
    }else{
      ramMax = nShmMax;
      ramCurr = nShmCur;
    }
    total = d["curr"][0u].asInt();
    uint64_t currUp = d["bw"][0u].asInt(), currDown = d["bw"][1u].asInt();
    uint64_t timeDiff = 0;
    if (prevTime){
      timeDiff = time(0) - prevTime;
      if (timeDiff){
        upSpeed = (currUp - upPrev) / timeDiff;
        downSpeed = (currDown - downPrev) / timeDiff;
      }
    }
    prevTime = time(0);
    upPrev = currUp;
    downPrev = currDown;

    if (d.isMember(STREAMSKEY) && d[STREAMSKEY].size()){
      jsonForEach(d[STREAMSKEY], it){
        uint64_t count = (*it)["curr"][0u].asInt() + (*it)["curr"][1u].asInt() + (*it)["curr"][2u].asInt();
        if (!count){
          if (streams.count(it.key())){streams.erase(it.key());}
          continue;
        }

        
        streamDetails &strm = streams[it.key()];

        strm.total = (*it)["curr"][0u].asInt();
        strm.inputs = (*it)["curr"][1u].asInt();
        strm.bytesUp = (*it)["bw"][0u].asInt();
        strm.bytesDown = (*it)["bw"][1u].asInt();
        uint64_t currTotal = strm.bytesUp + strm.bytesDown;
        if (timeDiff && count){
          strm.bandwidth = ((currTotal - strm.prevTotal) / timeDiff) / count;
        }else{
          if (total){
            strm.bandwidth = (upSpeed + downSpeed) / total;
          }else{
            strm.bandwidth = (upSpeed + downSpeed) + 100000;
          }
        }
        strm.prevTotal = currTotal;
      }
      if (streams.size()){
        std::set<std::string> eraseList;
        for (std::map<std::string, streamDetails>::iterator it = streams.begin();
             it != streams.end(); ++it){
          if (!d[STREAMSKEY].isMember(it->first)){eraseList.insert(it->first);}
        }
        for (std::set<std::string>::iterator it = eraseList.begin(); it != eraseList.end(); ++it){
          streams.erase(*it);
        }
      }
    }else{
      streams.clear();
    }
    conf_streams.clear();
    if (d.isMember(CONFSTREAMSKEY) && d[CONFSTREAMSKEY].size()){
      jsonForEach(d[CONFSTREAMSKEY], it){conf_streams.insert(it->asStringRef());}
    }
    outputs.clear();
    if (d.isMember(OUTPUTSKEY) && d[OUTPUTSKEY].size()){
      jsonForEach(d[OUTPUTSKEY], op){outputs[op.key()] = outUrl(op->asStringRef(), host);}
    }
    prevAddBandwidth = 0.75*(prevAddBandwidth);
    calc();//update preclaculated host vars
  }


/**
 * redirects traffic away
*/
bool redirectServer(hostEntry* H, bool empty){
  int reduceCPU;
  int reduceRAM;
  int reduceBW;
  if(!empty){//decrement
    if(H->details->getRamMax() * CAPPACITYTRIGGERRAM > H->details->getRamCurr()) reduceRAM = CAPPACITYTRIGGERRAMDEC * H->details->getRamCurr();
    if(CAPPACITYTRIGGERCPU * 1000 < H->details->getCpu()) reduceCPU = CAPPACITYTRIGGERCPUDEC * H->details->getCpu();
    if(CAPPACITYTRIGGERBW * H->details->getAvailBandwidth() < H->details->getCurrBandwidth()) reduceBW = CAPPACITYTRIGGERBWDEC * H->details->getCurrBandwidth();
  }else {//remove all users
    reduceRAM = H->details->getRamCurr();
    reduceCPU = H->details->getCpu();
    reduceBW = H->details->getCurrBandwidth();
  }
  std::map<int,hostEntry*> lbw;
  //find host with lowest bw usage
  for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
    if((*it)->state != STATE_ACTIVE) continue;
    if(HIGHCAPPACITYTRIGGERBW * (*it)->details->getAvailBandwidth() > ((*it)->details->getCurrBandwidth())){
      lbw.insert(std::pair<uint64_t, hostEntry*>((*it)->details->getCurrBandwidth(), *it));
    }
  }
  if(!lbw.size()) return false;
  std::map<int, hostEntry*>::iterator i = lbw.begin();

  while(i != lbw.end() && (reduceCPU != 0 || reduceBW != 0 || reduceRAM != 0)){//redirect until it finished or can't
    int balancableCpu = HIGHCAPPACITYTRIGGERCPU * 1000 - (*i).second->details->getCpu();
    int balancableRam = HIGHCAPPACITYTRIGGERRAM * (*i).second->details->getRamMax() - (*i).second->details->getRamCurr();
    if(balancableCpu > 0 && 0 < balancableRam){
      int balancableBW = HIGHCAPPACITYTRIGGERBW * (*i).second->details->availBandwidth - (*i).second->details->getCurrBandwidth();
      
      balancableBW = 100;


      H->details->balanceCPU = balancableCpu;
      H->details->balanceBW = balancableBW;
      H->details->balanceRAM = balancableRam;
      H->details->balanceRedirect = (*i).second->name;
      reduceBW -= balancableBW;
      reduceCPU -= balancableCpu;
      reduceRAM -= balancableRam;
      if(reduceCPU == 0 && reduceBW == 0 && reduceRAM == 0){
        return true;
      }
    }
    i++;
    sleep(50);
  }
  return false;
}
/**
 * grabs server from standby and if minstandby reached calls trigger LOAD_OVER
*/
void extraServer(){
  int counter = 0;
  bool found = false;
  for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
    if(!found && (*it)->state == STATE_ONLINE && !(*it)->standByLock) {
      api.removeStandBy(*it);
      found = true;
    }else if((*it)->state == STATE_ONLINE) counter++;
  }
  if(counter < MINSTANDBY) {
    JSON::Value serverData;
    for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      serverData[(const char*)((*it)->name)] = (*it)->details->getServerData();
    }
    if (Triggers::shouldTrigger("LOAD_OVER")){
      Triggers::doTrigger("LOAD_OVER", serverData.asString());
    }
    WARN_MSG("Server capacity running low!");
  }
}
/**
 * puts server in standby mode and if max standby is reached calss trigger LOAD_UNDER
*/
void reduceServer(hostEntry* H){
  api.setStandBy(H, false);
  int counter = 0;
  redirectServer(H, true);
  for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
    if((*it)->state == STATE_ONLINE && !(*it)->standByLock) counter++;
  }
  if(counter > MAXSTANDBY){
    JSON::Value serverData;
    for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      serverData[(const char*)((*it)->name)] = (*it)->details->getServerData();
    }
    if (Triggers::shouldTrigger("LOAD_UNDER")){
      Triggers::doTrigger("LOAD_UNDER", serverData.asString());
    }
    WARN_MSG("A lot of free server ! %d free servers", counter);
  }
}
/**
 * checks if redirect needs to happen
 * prevents servers from going online when still balancing the servers
*/
static void checkNeedRedirect(void*){
  while(cfg->is_active){
    //check if redirect is needed
    bool balancing = false;
    for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if((*it)->state != STATE_ACTIVE) continue;
      if((*it)->details->getRamMax() * CAPPACITYTRIGGERRAM > (*it)->details->getRamCurr() || 
        CAPPACITYTRIGGERCPU * 1000 < (*it)->details->getCpu() ||
        CAPPACITYTRIGGERBW * (*it)->details->getAvailBandwidth() < (*it)->details->getCurrBandwidth()){     
        balancing = redirectServer(*it, false);
      }
    }

    if(!balancing){//dont trigger when still balancing
      //check if reaching capacity
      std::set<hostEntry*> highCapacity;
      std::set<hostEntry*> lowCapacity;
      int counter = 0;
      for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
        if((*it)->state != STATE_ACTIVE) continue;
        counter++;
        if(HIGHCAPPACITYTRIGGERCPU * 1000 < (*it)->details->getCpu()){
          highCapacity.insert(*it);
        }
        if(HIGHCAPPACITYTRIGGERRAM * (*it)->details->getRamMax() < (*it)->details->getRamCurr()){
          highCapacity.insert(*it);
        }
        if(HIGHCAPPACITYTRIGGERBW * (*it)->details->getAvailBandwidth() < (*it)->details->getCurrBandwidth()){
          highCapacity.insert(*it);
        }
        if(LOWCAPPACITYTRIGGERCPU * 1000 > (*it)->details->getCpu()){
          lowCapacity.insert(*it);
        }
        if(LOWCAPPACITYTRIGGERRAM * (*it)->details->getRamMax() > (*it)->details->getRamCurr()){
          lowCapacity.insert(*it);
        }
        if(LOWCAPPACITYTRIGGERBW * (*it)->details->getAvailBandwidth() > (*it)->details->getCurrBandwidth()){
          lowCapacity.insert(*it);
        }
      }
      //check if too much capacity
      if(lowCapacity.size() > 1){
        reduceServer(*lowCapacity.begin());
      }
      //check if too little capacity
      if(lowCapacity.size() == 0 && highCapacity.size() == counter){
        extraServer();
      }
    }
    
    sleep(BALANCINGINTERVAL);
  }
}


/**
 * monitor server 
 * \param hostEntryPointer a hostEntry with hostDetailsCalc on details field
*/
void handleServer(void *hostEntryPointer){
  hostEntry *entry = (hostEntry *)hostEntryPointer;
  JSON::Value bandwidth = 128 * 1024 * 1024u; // assume 1G connection
  HTTP::URL url(entry->name);
  if (!url.protocol.size()){url.protocol = "http";}
  if (!url.port.size()){url.port = "4242";}
  if (url.path.size()){
    bandwidth = url.path;
    bandwidth = bandwidth.asInt() * 1024 * 1024;
    url.path.clear();
  }
  url.path = passphrase + ".json";

  INFO_MSG("Monitoring %s", url.getUrl().c_str());
  entry->details->availBandwidth = bandwidth.asInt();
  ((hostDetailsCalc*)(entry->details))->host = url.host;
  entry->state = STATE_BOOT;
  bool down = true;

  HTTP::Downloader DL;

  while (cfg->is_active && (entry->state != STATE_GODOWN)){
    JSON::Value j;
    j["balancingbw"] = entry->details->balanceBW;
    j["balancingCpu"] = entry->details->balanceCPU;
    j["balancingMem"] = entry->details->balanceRAM;
    j["redirect"] = entry->details->balanceRedirect;
    DL.setHeader("balancing", j.asString().c_str());
    if (DL.get(url) && DL.isOk()){
      JSON::Value servData = JSON::fromString(DL.data());
      if (!servData){
        int tmp = 0;
        if(lastPromethNode.numFailedConnectServer.count(entry->name)){
          tmp = lastPromethNode.numFailedConnectServer.at(entry->name);
        }
        lastPromethNode.numFailedConnectServer.insert(std::pair<std::string, int>(entry->name, tmp+1));
        FAIL_MSG("Can't decode server %s load information", url.host.c_str());
        ((hostDetailsCalc*)(entry->details))->badNess();
        DL.getSocket().close();
        down = true;
        entry->state = STATE_ERROR;
      }else{
        if (down){
          std::string ipStr;
          Socket::hostBytesToStr(DL.getSocket().getBinHost().data(), 16, ipStr);
          WARN_MSG("Connection established with %s (%s)", url.host.c_str(), ipStr.c_str());
          memcpy(((hostDetailsCalc*)(entry->details))->binHost, DL.getSocket().getBinHost().data(), 16);
          entry->state = STATE_ONLINE;
          down = false;
          int tmp = 0;
          if(lastPromethNode.numReconnectServer.count(entry->name)){
            tmp = lastPromethNode.numReconnectServer.at(entry->name);
          }
          lastPromethNode.numReconnectServer.insert(std::pair<std::string, int>(entry->name, tmp+1));
        }
        ((hostDetailsCalc*)(entry->details))->update(servData);
        int tmp = 0;
        if(lastPromethNode.numSuccessConnectServer.count(entry->name)){
          tmp = lastPromethNode.numSuccessConnectServer.at(entry->name);
        }
        lastPromethNode.numSuccessConnectServer.insert(std::pair<std::string, int>(entry->name, tmp+1));
      }
    }else{
      int tmp = 0;
      if(lastPromethNode.numFailedConnectServer.count(entry->name)){
        tmp = lastPromethNode.numFailedConnectServer.at(entry->name);
      }
      lastPromethNode.numFailedConnectServer.insert(std::pair<std::string, int>(entry->name, tmp+1));
      FAIL_MSG("Can't retrieve server %s load information", url.host.c_str());
      ((hostDetailsCalc*)(entry->details))->badNess();
      DL.getSocket().close();
      down = true;
      entry->state = STATE_ERROR;
    }
    Util::wait(5000);
  }
  WARN_MSG("Monitoring thread for %s stopping", url.host.c_str());
  DL.getSocket().close();
  entry->state = STATE_REQCLEAN;
}

/**
 * create new server without starting it
*/
void initNewHost(hostEntry &H, const std::string &N){
  // Cancel if this host has no name set
  if (!N.size()){return;}
  H.state = STATE_OFF;
  memset(H.name, 0, HOSTNAMELEN);
  memcpy(H.name, N.data(), N.size());
  H.thread = 0;
  H.details = 0;
}
/**
 * setup new server for monitoring (with hostDetailsCalc class)
 * \param N gives server name
 * \param H is the host entry being setup
*/
void initHost(hostEntry &H, const std::string &N){
  // Cancel if this host has no name set
  if (!N.size()){return;}
  H.state = STATE_BOOT;
  H.details = (hostDetails*)new hostDetailsCalc(H.name);
  memset(H.name, 0, HOSTNAMELEN);
  memcpy(H.name, N.data(), N.size());
  H.thread = new tthread::thread(handleServer, (void *)&H);
  INFO_MSG("Starting monitoring %s", H.name);
}
/**
 * Setup foreign host (with hostDetails class)
 * \param LB identifies the load balancer creating this foreign host
 * \param N gives server name
 */
void initForeignHost(const std::string &N){
  
  // Cancel if this host has no name or load balancer set
  if (!N.size()){return;}

  hostEntry* H = new hostEntry();
  H->state = STATE_ONLINE;
  H->details = new hostDetails(H->name);
  memset(H->name, 0, HOSTNAMELEN);
  memcpy(H->name, N.data(), N.size());
  H->thread = 0;
  hosts.insert(H);
  INFO_MSG("Created foreign server %s", H->name);
}
/**
 * remove monitored server or foreign server at \param H
*/
void cleanupHost(hostEntry &H){
  // Cancel if this host has no name set
  if (!H.name[0]){return;}
  if(H.state == STATE_BOOT){
    while(H.state != STATE_ONLINE){}
  }
  H.state = STATE_GODOWN;
  INFO_MSG("Stopping monitoring %s", H.name);
  if(H.thread){
    // Clean up thread
    H.thread->join();
    delete H.thread;
    H.thread = 0;
  }
  // Clean up details
  delete H.details;
  H.details = 0;
  memset(H.name, 0, HOSTNAMELEN);
  H.state = STATE_OFF;
}

/// Fills the given map with the given JSON string of tag adjustments
void fillTagAdjust(std::map<std::string, int32_t> & tags, const std::string & adjust){
  JSON::Value adj = JSON::fromString(adjust);
  jsonForEach(adj, t){
    tags[t.key()] = t->asInt();
  }
}

/**
 * generate random string using time and process id
*/
std::string generateSalt(){
  std::string alphbet("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
  std::string out;
  out.reserve(SALTSIZE);
  for(int i = 0; i < SALTSIZE; i++){
    out += alphbet[rand()%alphbet.size()];
  }
  std::time_t t;
  time(&t);
  activeSalts.insert(std::pair<std::string, std::time_t>(out,t));
  return out;
}


/**
 * \return s until first \param delimiter or end of string as a string
*/
std::string delimiterParser::next() {
  //get next delimiter
  int index = s.find(delimiter);
  if(index == -1) index = s.size(); //prevent index from being negative
  std::string ret;
  ret = s.substr(0, index);
  //remove next output from s
  s.erase(0, index + delimiter.size());

  return ret;
}
/**
 * \returns next double seperated by delimiter
*/
double delimiterParser::nextDouble(){
  return atof(this->next().c_str());
}
/**
 * \return s until first \param delimiter or end of string as an Int
*/
int delimiterParser::nextInt() {
  return atoi(this->next().c_str());
}

/**
 * \returns the identifiers of the load balancers that need to monitor the server in \param H
*/
std::set<std::string> hostNeedsMonitoring(hostEntry H){
  int num = 0;//find start position
  std::set<std::string> hostnames;
  for(std::set<hostEntry*>::iterator i = hosts.begin(); i != hosts.end(); i++){
    hostnames.insert((*i)->name);
  }
  for(std::set<std::string>::iterator i = hostnames.begin(); i != hostnames.end(); i++){
    if(H.name == (*i)) break;
    num++;
  }
  //find indexes
  int trigger = hostnames.size()/identifiers.size();
  std::set<int> indexs;
  for(int j = 0; j < SERVERMONITORLIMIT; j++){
    indexs.insert((num/trigger+j) % identifiers.size());
  }
  //find identifiers
  std::set<std::string> ret;
  std::set<int>::iterator i = indexs.begin();
  std::set<std::string>::iterator it = identifiers.begin();
  for(int x = 0; x < SERVERMONITORLIMIT && i != indexs.end(); x++){
    std::advance(it,(*i));
    ret.insert(*it);
    i++;
  }
  return ret;
}
/**
 * changes host to correct monitor state
*/
void checkServerMonitors(){
  INFO_MSG("recalibrating server monitoring")
  //check for monitoring changes
  std::set<hostEntry*>::iterator it = hosts.begin();
  while(it != hosts.end()){
    std::set<std::string> idents = hostNeedsMonitoring(*(*it));
    bool changed = false;
    std::set<std::string>::iterator i = idents.find(identifier);
    if(i != idents.end()){
      if((*it)->thread == 0){//check monitored
        std::string name = ((*it)->name);
        //delete old host
        cleanupHost(**it);
        delete *it;
        //create new host
        hostEntry* e = new hostEntry();
        initHost(*e, name);
        hosts.insert(e);

        //reset itterator
        it = hosts.begin();
        changed = true;
      }
    }else if((*it)->thread != 0 && (*it)->details == 0){//check not monitored
        //delete old host
        std::string name ((*it)->name);
        
        cleanupHost(**it);
        //create new host
        initForeignHost(name);
        
        //reset iterator
        it = hosts.begin();
        changed = true;
        break;
      }
    if(!changed){
      it++;
    }
  }
}



/**
 * \returns the config as a string
*/
std::string configToString(){
  JSON::Value j;
  j[CONFIGFALLBACK] = fallback;
  j[CONFIGC] = weight_cpu;
  j[CONFIGR] = weight_ram;
  j[CONFIGBW] = weight_bw;
  j[CONFIGWG] = weight_geo;
  j[CONFIGWB] = weight_bonus;
  j[CONFIGPASS] = passHash;
  j[CONFIGSPASS] = passphrase;
  j[CONFIGWHITELIST] = convertSetToJson(whitelist);
  j[CONFIGBEARER] = convertSetToJson(bearerTokens);
  j[CONFIGUSERS] = convertMapToJson(userAuth);

  //balancing
  j[CONFIGMINSTANDBY] = MINSTANDBY;
  j[CONFIGMAXSTANDBY] = MAXSTANDBY;
  j[CONFIGCAPPACITYTRIGGERCPUDEC] =  CAPPACITYTRIGGERCPUDEC; 
  j[CONFIGCAPPACITYTRIGGERBWDEC] = CAPPACITYTRIGGERBWDEC; 
  j[CONFIGCAPPACITYTRIGGERRAMDEC] = CAPPACITYTRIGGERRAMDEC; 
  j[CONFIGCAPPACITYTRIGGERCPU] = CAPPACITYTRIGGERCPU; 
  j[CONFIGCAPPACITYTRIGGERBW] = CAPPACITYTRIGGERBW;  
  j[CONFIGCAPPACITYTRIGGERRAM] = CAPPACITYTRIGGERRAM; 
  j[CONFIGHIGHCAPPACITYTRIGGERCPU] = HIGHCAPPACITYTRIGGERCPU; 
  j[CONFIGHIGHCAPPACITYTRIGGERBW] = HIGHCAPPACITYTRIGGERBW;  
  j[CONFIGHIGHCAPPACITYTRIGGERRAM] = HIGHCAPPACITYTRIGGERRAM; 
  j[CONFIGLOWCAPPACITYTRIGGERCPU] = LOWCAPPACITYTRIGGERCPU; 
  j[CONFIGLOWCAPPACITYTRIGGERBW] = LOWCAPPACITYTRIGGERBW;  
  j[CONFIGLOWCAPPACITYTRIGGERRAM] = LOWCAPPACITYTRIGGERRAM; 
  j[CONFIGBALANCINGINTERVAL] = BALANCINGINTERVAL;
  //serverlist 
  std::set<std::string> servers;
  for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
    if((*it)->thread != 0){
      servers.insert((*it)->name);
    }
  }
  j[CONFIGSERVERS] = convertSetToJson(servers);
  //loadbalancer list
  std::set<std::string> lb;
  lb.insert(myName);
  for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
    lb.insert((*it)->getName());
  }
  j[CONFIGLOADBALANCER] = convertSetToJson(lb);
  return j.asString();
}
/**
 * save config vars to config file
 * \param RESEND allows for command to be sent to other load balacners
*/
void saveFile(bool RESEND = false){
  //send command to other load balancers
  if(RESEND){
    JSON::Value j;
    j[SAVEKEY] = true;
    for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
      (*it)->send(j.asString());
    }
  }
  tthread::lock_guard<tthread::mutex> guard(fileMutex);
  std::ofstream file(fileloc.c_str());
  

  if(file.is_open()){

    file << configToString().c_str();
    file.flush();
    file.close();
    time(&prevSaveTime);
    INFO_MSG("config saved");
  }else {
    INFO_MSG("save failed");
  }
}
/**
 * timer to check if enough time passed since last config change to save to the config file
*/
static void saveTimeCheck(void*){
  if(prevConfigChange < prevSaveTime){
    WARN_MSG("manual save1")
    return;
  }
  time(&now);
  double timeDiff = difftime(now,prevConfigChange);
  while(timeDiff < 60*SAVETIMEINTERVAL){
    //check for manual save
    if(prevConfigChange < prevSaveTime){
      return;
    }
    //sleep thread for 600 - timeDiff
    sleep(60*SAVETIMEINTERVAL - timeDiff);
    time(&now);
    timeDiff = difftime(now,prevConfigChange);
  }
  saveFile();
  saveTimer = 0;
}
/**
 * loads the config from a string
*/
void configFromString(std::string s){
  //change config vars
  JSON::Value j = JSON::fromString(s);
  fallback = j[CONFIGFALLBACK].asString();
  weight_cpu = j[CONFIGC].asInt();
  weight_ram = j[CONFIGR].asInt();
  weight_bw = j[CONFIGBW].asInt();
  weight_geo = j[CONFIGWG].asInt();
  weight_bonus = j[CONFIGWB].asInt();
  passHash = j[CONFIGPASS].asString();
  passphrase = j[CONFIGSPASS].asStringRef();
  bearerTokens = convertJsonToSet(j[CONFIGBEARER]);

  //balancing
  MINSTANDBY = j[CONFIGMINSTANDBY].asInt();
  MAXSTANDBY = j[CONFIGMAXSTANDBY].asInt();
  CAPPACITYTRIGGERCPUDEC = j[CONFIGCAPPACITYTRIGGERCPUDEC].asDouble(); //percentage om cpu te verminderen
  CAPPACITYTRIGGERBWDEC = j[CONFIGCAPPACITYTRIGGERBWDEC].asDouble(); //percentage om bandwidth te verminderen
  CAPPACITYTRIGGERRAMDEC = j[CONFIGCAPPACITYTRIGGERRAMDEC].asDouble(); //percentage om ram te verminderen
  CAPPACITYTRIGGERCPU = j[CONFIGCAPPACITYTRIGGERCPU].asDouble(); //max capacity trigger for balancing cpu
  CAPPACITYTRIGGERBW = j[CONFIGCAPPACITYTRIGGERBW].asDouble();  //max capacity trigger for balancing bandwidth
  CAPPACITYTRIGGERRAM = j[CONFIGCAPPACITYTRIGGERRAM].asDouble(); //max capacity trigger for balancing ram
  HIGHCAPPACITYTRIGGERCPU = j[CONFIGHIGHCAPPACITYTRIGGERCPU].asDouble(); //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERCPU
  HIGHCAPPACITYTRIGGERBW = j[CONFIGHIGHCAPPACITYTRIGGERBW].asDouble();  //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERBW
  HIGHCAPPACITYTRIGGERRAM = j[CONFIGHIGHCAPPACITYTRIGGERRAM].asDouble(); //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERRAM
  LOWCAPPACITYTRIGGERCPU = j[CONFIGLOWCAPPACITYTRIGGERCPU].asDouble(); //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERCPU
  LOWCAPPACITYTRIGGERBW = j[CONFIGLOWCAPPACITYTRIGGERBW].asDouble();  //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERBW
  LOWCAPPACITYTRIGGERRAM = j[CONFIGLOWCAPPACITYTRIGGERRAM].asDouble(); //capacity at which considerd almost full. should be less than CAPPACITYTRIGGERRAM
  BALANCINGINTERVAL = j[CONFIGBALANCINGINTERVAL].asInt();

  if(HIGHCAPPACITYTRIGGERCPU > CAPPACITYTRIGGERCPU) HIGHCAPPACITYTRIGGERCPU = CAPPACITYTRIGGERCPU;
  if(HIGHCAPPACITYTRIGGERBW > CAPPACITYTRIGGERBW) HIGHCAPPACITYTRIGGERBW = CAPPACITYTRIGGERBW;
  if(HIGHCAPPACITYTRIGGERRAM > CAPPACITYTRIGGERRAM) HIGHCAPPACITYTRIGGERRAM = CAPPACITYTRIGGERRAM;
  if(LOWCAPPACITYTRIGGERCPU > CAPPACITYTRIGGERCPU) LOWCAPPACITYTRIGGERCPU = HIGHCAPPACITYTRIGGERCPU;
  if(LOWCAPPACITYTRIGGERBW > CAPPACITYTRIGGERBW) LOWCAPPACITYTRIGGERBW = HIGHCAPPACITYTRIGGERBW;
  if(LOWCAPPACITYTRIGGERRAM > CAPPACITYTRIGGERRAM) LOWCAPPACITYTRIGGERRAM = HIGHCAPPACITYTRIGGERRAM;

  //load whitelist
  whitelist = convertJsonToSet(j[CONFIGWHITELIST]);
  
  userAuth = convertJsonToMapPair(j[CONFIGUSERS]);

  //add new servers
  for(int i = 0; i < j[CONFIGSERVERS].size(); i++){
    std::string ret;
    api.addServer(ret,j[CONFIGSERVERS][i], true);
  }
  
  //add new load balancers
  jsonForEach(j[CONFIGLOADBALANCER],i){
    if(!(*i).asString().compare(myName)) continue;
    for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
      if((*it)->getName().compare((*i).asString())){
        new tthread::thread(api.addLB,(void*)&((*i).asStringRef()));
      }
    }
  }
}
/**
 * load config vars from config file 
 * \param RESEND allows for command to be sent sent to other load balancers
*/
void loadFile(bool RESEND = false){
  //send command to other load balancers
  if(RESEND){
    JSON::Value j;
    j[LOADKEY] = true;
    for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
      (*it)->send(j.asString());
    }
  }
  
  tthread::lock_guard<tthread::mutex> guard(fileMutex);
  std::ifstream file(fileloc.c_str());
  std::string data;
  std::string line;
  //read file
  if(file.is_open()){
    while(getline(file,line)){
      data.append(line);
    }

      //remove monitored servers
    for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      api.delServer((*it)->name, true);
    }
    //remove load balancers
    std::set<LoadBalancer*>::iterator it = loadBalancers.begin();
    while(loadBalancers.size()){
      (*it)->send("close");
      (*it)->Go_Down = true;
      loadBalancers.erase(it);
      it = loadBalancers.begin(); 
    }
    configFromString(data);  
  

  file.close();
  INFO_MSG("loaded config");
  checkServerMonitors();
  }else WARN_MSG("cant load")
}

/**
 * allow connection threads to be made to call API::handleRequests
*/
int API::handleRequest(Socket::Connection &conn){
  return handleRequests(conn, 0, 0);
}
/**
 * function to select the api function wanted
 */
int API::handleRequests(Socket::Connection &conn, HTTP::Websocket* webSock = 0, LoadBalancer* LB = 0){
  HTTP::Parser H;
  while (conn){
    // Handle websockets
    if (webSock){
      if (webSock->readFrame()){
        LB = onWebsocketFrame(webSock, conn.getHost(), LB);
        continue;
      }
      else{Util::sleep(100); continue;}
    }else if ((conn.spool() || conn.Received().size()) && H.Read(conn)){
      // Handle upgrade to websocket if the output supports it
      std::string upgradeHeader = H.GetHeader("Upgrade");
      Util::stringToLower(upgradeHeader);
      if (upgradeHeader == "websocket"){
        INFO_MSG("Switching to Websocket mode");
        conn.setBlocking(false);
        webSock = new HTTP::Websocket(conn, H);
        if (!(*webSock)){
          delete webSock;
          webSock = 0;
          continue;
        }
        H.Clean();
        continue;
      }

      //handle non-websocket connections
      delimiterParser path((std::string)HTTP::URL(H.url).path,"/");
      std::string api = path.next();
     
      if(!H.method.compare("PUT") && !api.compare("stream")){
        stream(conn, H, path.next(), path.next(), true);
        lastPromethNode.numSuccessRequests++;
        continue;
      }
      if(!H.method.compare("GET") && !api.compare("salt")){//request your salt
        H.Clean();
        H.SetHeader("Content-Type", "text/plain");
        H.SetBody(userAuth.at(path.next()).second);
        H.setCORSHeaders();
        H.SendResponse("200", "OK", conn);
        H.Clean();
        lastPromethNode.numSuccessRequests++;
        continue;
      }

      if(H.url.substr(0, passphrase.size() + 6) == "/" + passphrase + ".json"){
        H.Clean();
        H.SetHeader("Content-Type", "text/json");
        H.setCORSHeaders();
        H.StartResponse("200", "OK", H, conn, true);
        H.Chunkify(handlePrometheus().toString(), conn);
        continue;
      }

      //Authentication
      std::string creds = H.GetHeader("Authorization");
      //auth with username and password
      if(!creds.substr(0,5).compare("Basic")){
        delimiterParser cred(Encodings::Base64::decode(creds.substr(6,creds.size())),":");
        //check if user exists
        std::map<std::string, std::pair<std::string, std::string> >::iterator user = userAuth.find(cred.next());
        //check password
        if(user == userAuth.end() || ((*user).second.first).compare(Secure::sha256(cred.next()+(*user).second.second))) {
          H.SetBody("invalid credentials");
          H.setCORSHeaders();
          H.SendResponse("403", "Forbidden", conn);
          H.Clean();
          conn.close();
          lastPromethNode.badAuth++;
          continue;
        }
        lastPromethNode.goodAuth++;
      }
      //auth with bearer token
      else if(!creds.substr(0,7).compare("Bearer ")){
        if(!bearerTokens.count(creds.substr(7,creds.size()))){
          H.SetBody("invalid token");
          H.setCORSHeaders();
          H.SendResponse("403", "Forbidden", conn);
          H.Clean();
          conn.close();
          lastPromethNode.badAuth++;
          continue;
        }
        lastPromethNode.goodAuth++;
      }
      //whitelist ipv6 & ipv4
      else if(conn.getHost().size()){
        bool found = false;
        std::set<std::string>::iterator it = whitelist.begin();
        while( it != whitelist.end()){
          if(Socket::isBinAddress(conn.getBinHost(), *it)){
            found = true;
            break;
          }
          it++;
        }
        if(!found){
          H.SetBody("not in whitelist");
          H.setCORSHeaders();
          H.SendResponse("403", "Forbidden", conn);
          H.Clean();
          conn.close();
          lastPromethNode.badAuth++;
          continue;
        }
        lastPromethNode.goodAuth++;
      }
      //block other auth forms including none
      else{
        H.SetBody("no credentials given");
        H.setCORSHeaders();
        H.SendResponse("403", "Forbidden", conn);
        H.Clean();
        conn.close();
        lastPromethNode.badAuth++;
        continue;
      }

     //API METHODS     
      if(!H.method.compare("PUT")){
        //save config
        if(!api.compare("save")){
          saveFile(true);
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody("OK");
          H.setCORSHeaders();
          H.SendResponse("204", "OK", conn);
          H.Clean();
          lastPromethNode.numSuccessRequests++;
        }
        //load config
        else if(!api.compare("load")){
          loadFile(true);
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody("OK");
          H.setCORSHeaders();
          H.SendResponse("204", "OK", conn);
          H.Clean();
          lastPromethNode.numSuccessRequests++;
        }
        //add load balancer to mesh
        else if(!api.compare("loadbalancers")){
          std::string loadbalancer = path.next();
          new tthread::thread(addLB,(void*)&loadbalancer);
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody("OK");
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          lastPromethNode.numSuccessRequests++;
        }
        // Get/set weights
        else if (!api.compare("weights")){
          JSON::Value ret = setWeights(path);
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody(ret.toString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          lastPromethNode.numSuccessRequests++;
        }
        // Add server to list
        else if (!api.compare("servers")){
          std::string ret;
          addServer(ret, path.next(), true);
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody(ret.c_str());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          lastPromethNode.numSuccessRequests++;
        }
        else if(!api.compare("balancing")){
          balance(path);
          lastPromethNode.numSuccessRequests++;
        }
        else if(!api.compare("standby")){
          std::string name = path.next();
          std::set<hostEntry*>::iterator it = hosts.begin();
          while(!name.compare((*it)->name) && it != hosts.end()) it++;
          if(it != hosts.end()) {
            setStandBy(*it, path.nextInt());
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }else{
            lastPromethNode.numFailedRequests++;
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("invalid server name");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
          }
        }
        //auth
        else if(!api.compare("auth")){
          api = path.next();
          // add bearer token
          if (!api.compare("bearer")){
            std::string bearer = path.next();
            bearerTokens.insert(bearer);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("OK");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
            //start save timer
            time(&prevConfigChange);
            if(saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck,NULL);
          }
          // add user acount
          else if (!api.compare("user")){
            std::string userName = path.next();
            std::string salt = generateSalt();
            std::string password = Secure::sha256(path.next()+salt);
            userAuth[userName] = std::pair<std::string, std::string>(password, salt);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("OK");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            JSON::Value j;
            j[ADDUSERKEY] = userName;
            j[ADDPASSKEY] = password;
            j[ADDSALTKEY] = salt;
            for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
              (*it)->send(j.asString());
            }
            lastPromethNode.numSuccessRequests++;
            //start save timer
            time(&prevConfigChange);
            if(saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck,NULL);
          }
          // add whitelist policy
          else if (!api.compare("whitelist")){
            whitelist.insert(H.body);
            JSON::Value j;
            j[ADDWHITELISTKEY] = H.body;
            for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
              (*it)->send(j.asString());
            }
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("OK");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
            //start save timer
            time(&prevConfigChange);
            if(saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck,NULL);
          }
          //handle none api
          else{
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("invalid");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numIllegalRequests++;
          }
        }
        //handle none api
        else{
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody("invalid");
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          lastPromethNode.numIllegalRequests++;
        }
      }else if(!H.method.compare("GET")){
        if(!api.compare("loadbalancers")){
          std::string out = getLoadBalancerList();
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody(out);
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          lastPromethNode.numSuccessRequests++;
        }        
        // Get server list
        else if (!api.compare("servers")){
          JSON::Value ret = serverList();
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody(ret.toPrettyString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          lastPromethNode.numSuccessRequests++;
        }
        // Request viewer count
        else if (!api.compare("viewers")){
          JSON::Value ret = getViewers();
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody(ret.toPrettyString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          lastPromethNode.numSuccessRequests++;
        }
        // Request full stream statistics
        else if (!api.compare("streamstats")){
          JSON::Value ret = getStreamStats(path.next());
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody(ret.toPrettyString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          lastPromethNode.numSuccessRequests++;   
        }
        //get stream viewer count
        else if (!api.compare("stream")){
          uint64_t count = getStream(path.next());
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody(JSON::Value(count).asString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          lastPromethNode.numSuccessRequests++;
        }
        // Find source for given stream
        else if (!api.compare("source")){
          std::string source = path.next();
          getSource(conn, H, source, path.next(), true);
        }
        // Find optimal ingest point
        else if (!api.compare("ingest")){
          std::string ingest = path.next();
          getIngest(conn, H, ingest, path.next(), true);
        }
        // Find host(s) status
        else if (!api.compare("host")){
          std::string host = path.next();
          if(!host.size()){
            JSON::Value ret = getAllHostStates();
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody(ret.toPrettyString());
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }else{
            JSON::Value ret = getHostState(host);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody(ret.toPrettyString());
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }
        // Get weights
        }else if (!api.compare("weights")){
          JSON::Value ret = setWeights(delimiterParser("",""));
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody(ret.toString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          lastPromethNode.numSuccessRequests++;
        }
        else if(!api.compare("balancing")){
          JSON::Value ret;
          ret["balancing Interval"] = BALANCINGINTERVAL;
          ret["minStandby"] = MINSTANDBY;
          ret["maxStandby"] = MAXSTANDBY;
          ret["HIGHCAPPACITYTRIGGERBW"] = HIGHCAPPACITYTRIGGERBW;
          ret["HIGHCAPPACITYTRIGGERCPU"] = HIGHCAPPACITYTRIGGERCPU;
          ret["HIGHCAPPACITYTRIGGERRAM"] = HIGHCAPPACITYTRIGGERRAM;
          ret["LOWCAPPACITYTRIGGERBW"] = LOWCAPPACITYTRIGGERBW;
          ret["LOWCAPPACITYTRIGGERCPU"] = LOWCAPPACITYTRIGGERCPU;
          ret["LOWCAPPACITYTRIGGERRAM"] = LOWCAPPACITYTRIGGERRAM;
          ret["CAPPACITYTRIGGERBW"] = CAPPACITYTRIGGERBW;
          ret["CAPPACITYTRIGGERCPU"] = CAPPACITYTRIGGERCPU;
          ret["CAPPACITYTRIGGERRAM"] = CAPPACITYTRIGGERRAM;
          ret["CAPPACITYTRIGGERCPUDEC"] = CAPPACITYTRIGGERCPUDEC;
          ret["CAPPACITYTRIGGERBWDEC"] = CAPPACITYTRIGGERBWDEC;
          ret["CAPPACITYTRIGGERRAMDEC"] = CAPPACITYTRIGGERRAMDEC;
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody(ret.toString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          lastPromethNode.numSuccessRequests++;
        }
        else if(!api.compare("auth")){
          api = path.next();
          // add bearer token
          if (!api.compare("bearer")){
            JSON::Value j = convertSetToJson(bearerTokens);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody(j.asString());
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }
          // add user acount
          else if (!api.compare("user")){
            JSON::Value j = convertMapToJson(userAuth);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody(j.asString());
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }
          // add whitelist policy
          else if (!api.compare("whitelist")){
            JSON::Value j = convertSetToJson(whitelist);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody(j.asString());
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
          }
          //handle none api
          else{
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("invalid");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            conn.close();
            lastPromethNode.numIllegalRequests++;
          }
        }
        //handle none api
        else{
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody("invalid");
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          conn.close();
          lastPromethNode.numIllegalRequests++;
        }
      }else if(!H.method.compare("DELETE")){
        //remove load balancer from mesh
        if(!api.compare("loadbalancers")){
          std::string loadbalancer = path.next();
          removeLB(loadbalancer, path.next());
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody("OK");
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          lastPromethNode.numSuccessRequests++;
        }
        // Remove server from list
        else if (!api.compare("servers")){
          std::string s = path.next();
          JSON::Value ret = delServer(s, true);
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody(ret.toPrettyString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          lastPromethNode.numSuccessRequests++;
        }
        //auth
        else if(!api.compare("auth")){
          api = path.next();
          // del bearer token
          if (!api.compare("bearer")){
            bearerTokens.erase(path.next());
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("OK");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
            //start save timer
            time(&prevConfigChange);
            if(saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck,NULL);
          }
          // del user acount
          else if (!api.compare("user")){
            std::string userName = path.next();
            userAuth.erase(userName);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("OK");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            JSON::Value j;
            j[RUSERKEY] = userName;
            for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
              (*it)->send(j.asString());
            }
            lastPromethNode.numSuccessRequests++;
            //start save timer
            time(&prevConfigChange);
            if(saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck,NULL);
          }
          // del whitelist policy
          else if (!api.compare("whitelist")){
            std::set<std::string>::iterator it = whitelist.begin();
            while(it != whitelist.end()){
              if(!(*it).compare(H.body)){
                whitelist.erase(it);
                it = whitelist.begin();
              }else it++;
            }
            
            JSON::Value j;
            j[RUSERKEY] =H.body;
            for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
              (*it)->send(j.asString());
            }
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("OK");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numSuccessRequests++;
            //start save timer
            time(&prevConfigChange);
            if(saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck,NULL);
          }
          //handle none api
          else{
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("invalid");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            lastPromethNode.numIllegalRequests++;
          }
        }
        //handle none api
        else{
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody("invalid");
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          lastPromethNode.numIllegalRequests++;
        }
      }
      //handle none api
      else{
        H.Clean();
        H.SetHeader("Content-Type", "text/plain");
        H.SetBody("invalid");
        H.setCORSHeaders();
        H.SendResponse("200", "OK", conn);
        H.Clean();
        lastPromethNode.numIllegalRequests++;
      }
    }
  }
  //check if this is a load balancer connection
  if(LB){
    if(!LB->Go_Down){//check if load balancer crashed
      LB->state = false;
      WARN_MSG("restarting connection of load balancer: %s", LB->getName().c_str());
      int tmp = 0;
      if(lastPromethNode.numReconnectLB.count(LB->getName())){
        tmp = lastPromethNode.numReconnectLB.at(LB->getName());
      }
      lastPromethNode.numReconnectLB.insert(std::pair<std::string, int>(LB->getName(), tmp+1));
      new tthread::thread(reconnectLB, (void*)LB);
    }else{//shutdown load balancer
      LB->Go_Down = true;
      loadBalancers.erase(LB);
      delete LB;
      INFO_MSG("shuting Down connection");
    }
  }
  conn.close();
  return 0;
}
/**
 * handle websockets only used for other load balancers 
 * \return loadbalancer corisponding to this socket
*/
LoadBalancer* API::onWebsocketFrame(HTTP::Websocket* webSock, std::string name, LoadBalancer* LB){
  std::string frame(webSock->data, webSock->data.size());
  if(!frame.substr(0, frame.size()).compare("ident")){
    webSock->sendFrame(identifier);
    lastPromethNode.numLBSuccessRequests++;
  }
  else if(!frame.substr(0, frame.find(":")).compare("auth")){
    //send response to challenge
    std::string auth = frame.substr(frame.find(":")+1);
    std::string pass = Secure::sha256(passHash+auth);
    webSock->sendFrame(pass);

    //send own challenge
    std::string salt = generateSalt();
    webSock->sendFrame(salt);
    lastPromethNode.numLBSuccessRequests++;
  }
  else if(!frame.substr(0, frame.find(":")).compare("salt")){
    //check responce
    std::string salt = frame.substr(frame.find(":")+1, frame.find(";")-frame.find(":")-1);
    std::map<std::string, time_t>::iterator saltIndex = activeSalts.find(salt);

    if(saltIndex == activeSalts.end()){
      webSock->sendFrame("noAuth");
      webSock->getSocket().close();
      WARN_MSG("no salt")
      lastPromethNode.numLBFailedRequests++;
      return LB;
    }

    if(!Secure::sha256(passHash+salt).compare(frame.substr(frame.find(";")+1, frame.find(" ")-frame.find(";")-1))){
      //auth successful
      webSock->sendFrame("OK");
      LB = new LoadBalancer(webSock, frame.substr(frame.find(" ")+1, frame.size()), frame.substr(frame.find(" "), frame.size()));
      loadBalancers.insert(LB);
      INFO_MSG("Load balancer added");
      checkServerMonitors();
      lastPromethNode.numLBSuccessRequests++;
    }else{
      webSock->sendFrame("noAuth");
      INFO_MSG("unautherized load balancer");
      LB = 0;
      lastPromethNode.numLBFailedRequests++;
    }
    
  }
  //close bad auth
  else if(!frame.substr(0, frame.find(":")).compare("noAuth")){
    webSock->getSocket().close();
    lastPromethNode.numLBSuccessRequests++;
  }
  //close authenticated load balancer
  else if(!frame.compare("close")){
    LB->Go_Down = true;
    loadBalancers.erase(LB);
    webSock->getSocket().close();
    lastPromethNode.numLBSuccessRequests++;
  }
  else if(LB && !frame.substr(0, 1).compare("{")){
    JSON::Value newVals = JSON::fromString(frame);
    if(newVals.isMember(ADDLOADBALANCER)) {
      new tthread::thread(api.addLB,(void*)&(newVals[ADDLOADBALANCER].asStringRef()));
      lastPromethNode.numLBSuccessRequests++;
    }else if(newVals.isMember(REMOVELOADBALANCER)) {
      api.removeLB(newVals[REMOVELOADBALANCER], newVals[RESEND]);
      lastPromethNode.numLBSuccessRequests++;
    }else if(newVals.isMember(UPDATEHOST)) {
      api.updateHost(newVals[UPDATEHOST]);
      lastPromethNode.numLBSuccessRequests++;
    }else if(newVals.isMember(WEIGHTS)) {
      api.setWeights(newVals[WEIGHTS]);
      lastPromethNode.numLBSuccessRequests++;
    }else if(newVals.isMember(ADDSERVER)){
      std::string ret;
      api.addServer(ret,newVals[ADDSERVER], false);
      lastPromethNode.numLBSuccessRequests++;
    }else if(newVals.isMember(REMOVESERVER)){
      api.delServer(newVals[REMOVESERVER].asString(), false);
      lastPromethNode.numLBSuccessRequests++;
    }else if(newVals.isMember(SENDCONFIG)){
      configFromString(newVals[SENDCONFIG]);
      lastPromethNode.numLBSuccessRequests++;
    }else if(newVals.isMember(ADDVIEWER)){
      //find host
      for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
        if(newVals[HOST].asString().compare((*it)->details->host)){
          //call add viewer function
          jsonForEach(newVals[ADDVIEWER], i){
            for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
              if(!i.key().compare((*it)->name)) {
                (*it)->details->prevAddBandwidth += i.num();
                continue;
              }
            }
          }
        }
      }
      lastPromethNode.numLBSuccessRequests++;
    }else if(newVals.isMember(SAVEKEY)){
      saveFile();
      lastPromethNode.numLBSuccessRequests++;
    }else if(newVals.isMember(LOADKEY)){
      loadFile();
      lastPromethNode.numLBSuccessRequests++;
    }else if(newVals.isMember(BALANCEKEY)){
      balance(newVals[BALANCEKEY]);
      lastPromethNode.numLBSuccessRequests++;
    }else if(newVals.isMember(STANDBYKEY)){
      std::set<hostEntry*>::iterator it = hosts.begin();
      while(!newVals[STANDBYKEY].asString().compare((*it)->name) && it != hosts.end()) it++;
      if(it != hosts.end()) setStandBy(*it, newVals[LOCKKEY]);
      lastPromethNode.numLBSuccessRequests++;
    }else if(newVals.isMember(REMOVESTANDBYKEY)){
      std::set<hostEntry*>::iterator it = hosts.begin();
      while(!newVals[STANDBYKEY].asString().compare((*it)->name) && it != hosts.end()) it++;
      if(it != hosts.end()) removeStandBy(*it);
      lastPromethNode.numLBSuccessRequests++;
    }else if(newVals.isMember(ADDWHITELISTKEY)){
      whitelist.insert(newVals[ADDWHITELISTKEY].asString());
      lastPromethNode.numLBSuccessRequests++;
    }else if(newVals.isMember(ADDPASSKEY) && newVals.isMember(ADDUSERKEY) && newVals.isMember(ADDSALTKEY)){
      userAuth[newVals[ADDUSERKEY].asString()] = std::pair<std::string, std::string>(newVals[ADDPASSKEY].asString(), newVals[ADDSALTKEY].asString());
      lastPromethNode.numLBSuccessRequests++;
    }else if(newVals.isMember(RWHITELISTKEY)){
      std::set<std::string>::iterator it = whitelist.begin();
      while(it != whitelist.end()){
        if(!(*it).compare(newVals[RWHITELISTKEY].asString())){
          whitelist.erase(it);
          it = whitelist.begin();
        }else it++;
      }
      lastPromethNode.numLBSuccessRequests++;
    }else if(newVals.isMember(RUSERKEY)){
      userAuth.erase(newVals[RUSERKEY].asString());
      lastPromethNode.numLBSuccessRequests++;
    }else {
      lastPromethNode.numLBIllegalRequests++;
    }
  }else{
    lastPromethNode.numLBIllegalRequests++;
  }
  return LB;
}

/**
 * remove standby status from server
 * does not remove locked standby
*/
void API::removeStandBy(hostEntry* H){
  if(H->standByLock) {
    WARN_MSG("can't activate server. it is locked in standby mode");
    return;
  }
  if(H->state == STATE_ONLINE){
    H->state = STATE_ACTIVE;
    INFO_MSG("server %s removed from standby mode", H->name);
    JSON::Value j;
    j[REMOVESTANDBYKEY] = H->name;
    for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
      (*it)->send(j.asString());
    }
    return;
  }
  WARN_MSG("can't activate server. server is not running or already active");
}
/**
 * puts server in standby made with locked status depending on \param lock
*/
void API::setStandBy(hostEntry* H, bool lock){
  if(H->state != STATE_ACTIVE || H->state != STATE_ONLINE){
    WARN_MSG("server %s is not available", H->name);
    return;
  }
  H->state = STATE_ACTIVE;
  H->standByLock = lock; 
  JSON::Value j;
  j[STANDBYKEY] = H->name;
  j[LOCKKEY] = lock;
  for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
    (*it)->send(j.asString());
  }
}
/**
 * set balancing settings received through API
*/
void API::balance(delimiterParser path){
  JSON::Value j;
  std::string api = path.next();
  while(!api.compare("minstandby") || !api.compare("maxstandby") || !api.compare(CAPPACITYTRIGGERCPUDECKEY) || !api.compare(CAPPACITYTRIGGERRAMDECKEY) ||
    !api.compare(CAPPACITYTRIGGERBWDECKEY) || !api.compare(CAPPACITYTRIGGERCPUKEY) || !api.compare(CAPPACITYTRIGGERRAMKEY) ||
    !api.compare(CAPPACITYTRIGGERBWKEY) || !api.compare(HIGHCAPPACITYTRIGGERCPUKEY) || !api.compare(HIGHCAPPACITYTRIGGERRAMKEY) ||
    !api.compare(HIGHCAPPACITYTRIGGERBWKEY) || !api.compare(LOWCAPPACITYTRIGGERCPUKEY) || !api.compare(LOWCAPPACITYTRIGGERRAMKEY) ||
    !api.compare(LOWCAPPACITYTRIGGERBWKEY) || !api.compare(BALANCINGINTERVALKEY)) {
    if(!api.compare("minstandby")){
      int newVal = path.nextInt();
      if(newVal > MAXSTANDBY){
        MINSTANDBY = newVal;
        j[BALANCEKEY][MINSTANDBYKEY] = MINSTANDBY;
      }
    }
    else if(!api.compare("maxstandby")){
      int newVal = path.nextInt();
      if(newVal < MINSTANDBY){
        MAXSTANDBY = newVal;
        j[BALANCEKEY][MAXSTANDBYKEY] = MAXSTANDBY;
      }
    }        
    if(!api.compare(CAPPACITYTRIGGERCPUDECKEY)){
      double newVal = path.nextDouble();
      if(newVal >= 0 && newVal <= 1){
        CAPPACITYTRIGGERCPUDEC = newVal;
        j[BALANCEKEY][CAPPACITYTRIGGERCPUDECKEY] = CAPPACITYTRIGGERCPUDEC;
      }
    }
    else if(!api.compare(CAPPACITYTRIGGERRAMDECKEY)){
      double newVal = path.nextDouble();
      if(newVal >= 0 && newVal <= 1){
        CAPPACITYTRIGGERRAMDEC = newVal;
        j[BALANCEKEY][CAPPACITYTRIGGERRAMDECKEY] = CAPPACITYTRIGGERRAMDEC;
      }
    }
    else if(!api.compare(CAPPACITYTRIGGERBWDECKEY)){
      double newVal = path.nextDouble();
      if(newVal >= 0 && newVal <= 1){
        CAPPACITYTRIGGERBWDEC = newVal;
        j[BALANCEKEY][CAPPACITYTRIGGERBWDECKEY] = CAPPACITYTRIGGERBWDEC;
      }
    }
    else if(!api.compare(CAPPACITYTRIGGERCPUKEY)){
      double newVal = path.nextDouble();
      if(newVal >= 0 && newVal <= 1){
        CAPPACITYTRIGGERCPU = newVal;
        j[BALANCEKEY][CAPPACITYTRIGGERCPUKEY] = CAPPACITYTRIGGERCPU;
      }
    }
    else if(!api.compare(CAPPACITYTRIGGERRAMKEY)){
      double newVal = path.nextDouble();
      if(newVal >= 0 && newVal <= 1){
        CAPPACITYTRIGGERRAM = newVal;
        j[BALANCEKEY][CAPPACITYTRIGGERRAMKEY] = CAPPACITYTRIGGERRAM;
      }
    }
    else if(!api.compare(CAPPACITYTRIGGERBWKEY)){
      double newVal = path.nextDouble();
      if(newVal >= 0 && newVal <= 1){
        CAPPACITYTRIGGERBW = newVal;
        j[BALANCEKEY][CAPPACITYTRIGGERBWKEY] = CAPPACITYTRIGGERBW;
      }
    }
    else if(!api.compare(HIGHCAPPACITYTRIGGERCPUKEY)){
      double newVal = path.nextDouble();
      if(newVal >= 0 && newVal <= CAPPACITYTRIGGERCPU){
        HIGHCAPPACITYTRIGGERCPU = newVal;
        j[BALANCEKEY][HIGHCAPPACITYTRIGGERCPUKEY] = HIGHCAPPACITYTRIGGERCPU;
      }
    }
    else if(!api.compare(HIGHCAPPACITYTRIGGERRAMKEY)){
      double newVal = path.nextDouble();
      if(newVal >= 0 && newVal <= CAPPACITYTRIGGERRAM){
        HIGHCAPPACITYTRIGGERRAM = newVal;
        j[BALANCEKEY][HIGHCAPPACITYTRIGGERRAMKEY] = HIGHCAPPACITYTRIGGERRAM;
      }
    }
    else if(!api.compare(HIGHCAPPACITYTRIGGERBWKEY)){
      double newVal = path.nextDouble();
      if(newVal >= 0 && newVal <= CAPPACITYTRIGGERBW){
        HIGHCAPPACITYTRIGGERBW = newVal;
        j[BALANCEKEY][HIGHCAPPACITYTRIGGERBWKEY] = HIGHCAPPACITYTRIGGERBW;
      }
    }
    if(!api.compare(LOWCAPPACITYTRIGGERCPUKEY)){
      double newVal = path.nextDouble();
      if(newVal >= 0 && newVal <= HIGHCAPPACITYTRIGGERCPU){
        LOWCAPPACITYTRIGGERCPU = newVal;
        j[BALANCEKEY][LOWCAPPACITYTRIGGERCPUKEY] = LOWCAPPACITYTRIGGERCPU;
      }
    }
    else if(!api.compare(LOWCAPPACITYTRIGGERRAMKEY)){
      double newVal = path.nextDouble();
      if(newVal >= 0 && newVal <= HIGHCAPPACITYTRIGGERRAM){
        LOWCAPPACITYTRIGGERRAM = newVal;
        j[BALANCEKEY][LOWCAPPACITYTRIGGERRAMKEY] = LOWCAPPACITYTRIGGERRAM;
      }
    }
    else if(!api.compare(LOWCAPPACITYTRIGGERBWKEY)){
      double newVal = path.nextDouble();
      if(newVal >= 0 && newVal <= HIGHCAPPACITYTRIGGERBW){
        LOWCAPPACITYTRIGGERBW = newVal;
        j[BALANCEKEY][LOWCAPPACITYTRIGGERBWKEY] = LOWCAPPACITYTRIGGERBW;
      }
    }          
    else if(!api.compare(BALANCINGINTERVALKEY)){
      int newVal = path.nextInt();
      if(newVal >= 0){
        BALANCINGINTERVAL = newVal;
        j[BALANCEKEY][BALANCINGINTERVALKEY] = BALANCINGINTERVAL;
      }
    }else {
      path.next();
    }
    api = path.next();
  }
  for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
    (*it)->send(j.asString());
  }
  //start save timer
  time(&prevConfigChange);
  if(saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck,NULL);
}
/**
 * set balancing settings receiverd from load balancers
*/
void API::balance(JSON::Value newVals){
  if(newVals.isMember("minstandby")){
    int newVal = newVals[MINSTANDBYKEY].asInt();
    if(newVal > MAXSTANDBY){
      MINSTANDBY = newVal;
    }
  }
  else if(newVals.isMember("maxstandby")){
    int newVal = newVals[MAXSTANDBYKEY].asInt();
    if(newVal < MINSTANDBY){
      MAXSTANDBY = newVal;
    }
  }        
  if(newVals.isMember(CAPPACITYTRIGGERCPUDECKEY)){
    double newVal = newVals[CAPPACITYTRIGGERCPUDECKEY].asDouble();
    if(newVal >= 0 && newVal <= 1){
      CAPPACITYTRIGGERCPUDEC = newVal;
    }
  }
  else if(newVals.isMember(CAPPACITYTRIGGERRAMDECKEY)){
    double newVal = newVals[CAPPACITYTRIGGERRAMDECKEY].asDouble();
    if(newVal >= 0 && newVal <= 1){
      CAPPACITYTRIGGERRAMDEC = newVal;
    }
  }
  else if(newVals.isMember(CAPPACITYTRIGGERBWDECKEY)){
    double newVal = newVals[CAPPACITYTRIGGERBWDECKEY].asDouble();
    if(newVal >= 0 && newVal <= 1){
      CAPPACITYTRIGGERBWDEC = newVal;
    }
  }
  else if(newVals.isMember(CAPPACITYTRIGGERCPUKEY)){
    double newVal = newVals[CAPPACITYTRIGGERCPUKEY].asDouble();
    if(newVal >= 0 && newVal <= 1){
      CAPPACITYTRIGGERCPU = newVal;
    }
  }
  else if(newVals.isMember(CAPPACITYTRIGGERRAMKEY)){
    double newVal = newVals[CAPPACITYTRIGGERRAMKEY].asDouble();
    if(newVal >= 0 && newVal <= 1){
      CAPPACITYTRIGGERRAM = newVal;
    }
  }
  else if(newVals.isMember(CAPPACITYTRIGGERBWKEY)){
    double newVal = newVals[CAPPACITYTRIGGERBWKEY].asDouble();
    if(newVal >= 0 && newVal <= 1){
      CAPPACITYTRIGGERBW = newVal;
    }
  }
  else if(newVals[HIGHCAPPACITYTRIGGERCPUKEY]){
    double newVal = newVals[HIGHCAPPACITYTRIGGERCPUKEY].asDouble();
    if(newVal >= 0 && newVal <= CAPPACITYTRIGGERCPU){
      HIGHCAPPACITYTRIGGERCPU = newVal;
    }
  }
  else if(newVals.isMember(HIGHCAPPACITYTRIGGERRAMKEY)){
    double newVal = newVals[HIGHCAPPACITYTRIGGERRAMKEY].asDouble();
    if(newVal >= 0 && newVal <= CAPPACITYTRIGGERRAM){
      HIGHCAPPACITYTRIGGERRAM = newVal;
    }
  }
  else if(newVals.isMember(HIGHCAPPACITYTRIGGERBWKEY)){
    double newVal = newVals[HIGHCAPPACITYTRIGGERBWKEY].asDouble();
    if(newVal >= 0 && newVal <= CAPPACITYTRIGGERBW){
      HIGHCAPPACITYTRIGGERBW = newVal;
    }
  }
  if(newVals.isMember(LOWCAPPACITYTRIGGERCPUKEY)){
    double newVal = newVals[LOWCAPPACITYTRIGGERCPUKEY].asDouble();
    if(newVal >= 0 && newVal <= HIGHCAPPACITYTRIGGERCPU){
      LOWCAPPACITYTRIGGERCPU = newVal;
    }
  }
  else if(newVals.isMember(LOWCAPPACITYTRIGGERRAMKEY)){
    double newVal = newVals[LOWCAPPACITYTRIGGERRAMKEY].asDouble();
    if(newVal >= 0 && newVal <= HIGHCAPPACITYTRIGGERRAM){
      LOWCAPPACITYTRIGGERRAM = newVal;
    }
  }
  else if(newVals.isMember(LOWCAPPACITYTRIGGERBWKEY)){
    double newVal = newVals[LOWCAPPACITYTRIGGERBWKEY].asDouble();
    if(newVal >= 0 && newVal <= HIGHCAPPACITYTRIGGERBW){
      LOWCAPPACITYTRIGGERBW = newVal;
    }
  }          
  else if(newVals.isMember(BALANCINGINTERVALKEY)){
    int newVal = newVals[BALANCINGINTERVALKEY].asInt();
    if(newVal >= 0){
      BALANCINGINTERVAL = newVal;
    }
  }
  //start save timer
  time(&prevConfigChange);
  if(saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck,NULL);
}

/**
   * set and get weights
   */
JSON::Value API::setWeights(delimiterParser path){
    JSON::Value ret;
    std::string newVals = path.next();
    while(!newVals.compare("cpu") || !newVals.compare("ram") || !newVals.compare("bw") || !newVals.compare("geo") || !newVals.compare("bonus")){
      int num = path.nextInt();
      if (!newVals.compare("cpu")){
        weight_cpu = num;
      }
      else if (!newVals.compare("ram")){
        weight_ram = num;
      }
      else if (!newVals.compare("bw")){
        weight_bw = num;
      }
      else if (!newVals.compare("geo")){
        weight_geo = num;
      }
      else if (!newVals.compare("bonus")){
        weight_bonus = num;
      }
      newVals = path.next();
    }

    //create json for sending
    ret["cpu"] = weight_cpu;
    ret["ram"] = weight_ram;
    ret["bw"] = weight_bw;
    ret["geo"] = weight_geo;
    ret["bonus"] = weight_bonus;

    JSON::Value j;
    j[WEIGHTS] = ret;
    j[RESEND] = false;
    for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
      (*it)->send(j.asString());
    }
  
  
    //start save timer
    time(&prevConfigChange);
    if(saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck,NULL);
    
    return ret;
  }
/**
   * set weights for websockets
   */
void API::setWeights(const JSON::Value newVals){
    if (!newVals.isMember(CPUKEY)){
      weight_cpu = newVals[CPUKEY].asInt();
    }
    if (!newVals.isMember(RAMKEY)){
      weight_ram = newVals[RAMKEY].asInt();

    }
    if (!newVals.isMember(BWKEY)){
      weight_bw = newVals[BWKEY].asInt();
    }
    if (!newVals.isMember(GEOKEY)){
      weight_geo = newVals[GEOKEY].asInt();
    }
    if (!newVals.isMember(BONUSKEY)){
      weight_bonus = newVals[BONUSKEY].asInt();
    }
    //start save timer
    time(&prevConfigChange);
    if(saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck,NULL);
  }

/**
   * remove server from ?
   */
JSON::Value API::delServer(const std::string delserver, bool resend){
    JSON::Value ret;
    tthread::lock_guard<tthread::mutex> globGuard(globalMutex);
    if(resend){
      JSON::Value j;
      j[REMOVESERVER] = delserver;
      j[RESEND] = false;
      for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
        (*it)->send(j.asString());
      }
    }

    ret = "Server not monitored - could not delete from monitored server list!";
    std::string name = "";
    for (std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((std::string)(*it)->name == delserver){
        name = (*it)->name;
        cleanupHost(**it);
        ret = stateLookup[(*it)->state];
      }
    }

    checkServerMonitors();
    //start save timer
    time(&prevConfigChange);
    if(saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck,NULL);
    return ret;
    
  }
/**
   * add server to be monitored
   */
void API::addServer(std::string& ret, const std::string addserver, bool resend){
    tthread::lock_guard<tthread::mutex> globGuard(globalMutex);
    if (addserver.size() >= HOSTNAMELEN){
      return;
    }
    if(resend){
      JSON::Value j;
      j[ADDSERVER] = addserver;
      j[RESEND] = false;
      for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
        (*it)->send(j.asString());
      }
    }
    bool stop = false;
    hostEntry *newEntry = 0;
    for (std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state == STATE_OFF){continue;}
      if ((std::string)(*it)->name == addserver){
        stop = true;
        break;
      }
    }
    if (stop){
      ret = "Server already monitored - add request ignored";
    }else{
      for (std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
        if ((*it)->state == STATE_OFF){
          initNewHost(**it, addserver);
          newEntry = *it;
          stop = true;
          checkServerMonitors();
          break;
        }
      }
      if (!stop){
        newEntry = new hostEntry();
        initNewHost(*newEntry, addserver);
        hosts.insert(newEntry);
        checkServerMonitors();
      }
      ret = "server starting";
    }
    //start save timer
    time(&prevConfigChange);
    if(saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck,NULL);
    return;
  }
/**
   * return server list
   */
JSON::Value API::serverList(){
    JSON::Value ret;
    for (std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      ret[(std::string)(*it)->name] = stateLookup[(*it)->state];
    }
    return ret;    
  }
 
/**
   * receive server updates and adds new foreign hosts if needed
   */
void API::updateHost(JSON::Value newVals){
  
      std::string hostName = newVals[HOSTNAMEKEY].asString();
      std::set<hostEntry*>::iterator i = hosts.begin();
      while(i != hosts.end()){
        if(hostName == (*i)->name) break;
        i++;
      }
      if(i == hosts.end()){
        INFO_MSG("unknown host update failed")
      }
      (*i)->details->update(newVals[FILLSTATEOUT], newVals[FILLSTREAMSOUT], newVals[SCORESOURCE].asInt(), newVals[SCORERATE].asInt(), newVals[OUTPUTSKEY], newVals[CONFSTREAMSKEY], newVals[STREAMSKEY], newVals[TAGSKEY], newVals[CPUKEY].asInt(), newVals[SERVLATIKEY].asDouble(), newVals[SERVLONGIKEY].asDouble(), newVals[BINHOSTKEY].asString().c_str(), newVals[HOST].asString(), newVals[TOADD].asInt(), newVals[CURRBANDWIDTHKEY].asInt(), newVals[AVAILBANDWIDTHKEY].asInt(), newVals[CURRRAMKEY].asInt(), newVals[RAMMAXKEY].asInt());   
  }

/**
   * remove load balancer from mesh
   */
void API::removeLB(std::string removeLoadBalancer, const std::string RESEND){
  JSON::Value j;
  j[REMOVELOADBALANCER] = removeLoadBalancer;
  j[RESEND] = false;  

  //remove load balancer
  std::set<LoadBalancer*>::iterator it = loadBalancers.begin();
  while( it != loadBalancers.end()){
    if(!(*it)->getName().compare(removeLoadBalancer)){
      INFO_MSG("removeing load balancer: %s", removeLoadBalancer.c_str());
      identifiers.erase((*it)->getIdent());
      (*it)->send("close");
      (*it)->Go_Down = true;
      loadBalancers.erase(it);
      it = loadBalancers.end();
    }else{
      it++;
    }
  }
  //notify the last load balancers
  if(!RESEND.size() || atoi(RESEND.c_str()) ==1){
    for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
      (*it)->send(j.asString());
    }
  }
  checkServerMonitors();
  //start save timer
  time(&prevConfigChange);
  if(saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck,NULL);
}
/**
   * add load balancer to mesh
   */
void API::addLB(void* p){
    std::string* addLoadBalancer = (std::string*)p;
    if(addLoadBalancer->find(":") == -1){
      addLoadBalancer->append(":8042");
    }
    

    Socket::Connection conn(addLoadBalancer->substr(0,addLoadBalancer->find(":")), atoi(addLoadBalancer->substr(addLoadBalancer->find(":")+1).c_str()), false, false);
    
    HTTP::URL url("ws://"+(*addLoadBalancer));
    HTTP::Websocket* ws = new HTTP::Websocket(conn, url);
    

    ws->sendFrame("ident");

    //check responce
    int reset = 0;
    while(!ws->readFrame()){
      reset++;
      if(reset >= 20){
        WARN_MSG("auth failed: connection timeout");
        int tmp = 0;
        if(lastPromethNode.numFailedConnectLB.count(*addLoadBalancer)){
          tmp = lastPromethNode.numFailedConnectLB.at(*addLoadBalancer);
        }
        lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(*addLoadBalancer,tmp+1));
        return;
      }
      sleep(1);
    }

    std::string ident(ws->data, ws->data.size());

    for (std::set<std::string>::iterator i = identifiers.begin(); i != identifiers.end(); i++){
      if(!(*i).compare(ident)){
        ws->sendFrame("noAuth");
        conn.close();
        WARN_MSG("load balancer already connected");
        int tmp = 0;
        if(lastPromethNode.numFailedConnectLB.count(*addLoadBalancer)){
          tmp = lastPromethNode.numFailedConnectLB.at(*addLoadBalancer);
        }
        lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(*addLoadBalancer,tmp+1));
        return;
      }
    }

    //send challenge
    std::string salt = generateSalt();
    ws->sendFrame("auth:" + salt);

    //check responce
    reset = 0;
    while(!ws->readFrame()){
      reset++;
      if(reset >= 20){
        WARN_MSG("auth failed: connection timeout");
        int tmp = 0;
        if(lastPromethNode.numFailedConnectLB.count(*addLoadBalancer)){
          tmp = lastPromethNode.numFailedConnectLB.at(*addLoadBalancer);
        }
        lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(*addLoadBalancer,tmp+1));
        return;
      }
      sleep(1);
    }
    std::string result(ws->data, ws->data.size());
    
    if(Secure::sha256(passHash+salt).compare(result)){
      //unautherized
      WARN_MSG("unautherised");
      int tmp = 0;
      if(lastPromethNode.numFailedConnectLB.count(*addLoadBalancer)){
        tmp = lastPromethNode.numFailedConnectLB.at(*addLoadBalancer);
      }
      lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(*addLoadBalancer,tmp+1));
      ws->sendFrame("noAuth");
      return;
    }
    //send response to challenge
    reset = 0;
    while(!ws->readFrame()){
      reset++;
      if(reset >= 20){
        WARN_MSG("auth failed: connection timeout");
        int tmp = 0;
        if(lastPromethNode.numFailedConnectLB.count(*addLoadBalancer)){
          tmp = lastPromethNode.numFailedConnectLB.at(*addLoadBalancer);
        }
        lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(*addLoadBalancer,tmp+1));
        return;
      }
      sleep(1);
    }
    std::string auth(ws->data, ws->data.size());
    std::string pass = Secure::sha256(passHash+auth);

    ws->sendFrame("salt:"+auth+";"+pass+" "+myName);

    reset = 0;
    while(!ws->readFrame()){
      reset++;
      if(reset >= 20){
        WARN_MSG("auth failed: connection timeout");
        int tmp = 0;
        if(lastPromethNode.numFailedConnectLB.count(*addLoadBalancer)){
          tmp = lastPromethNode.numFailedConnectLB.at(*addLoadBalancer);
        }
        lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(*addLoadBalancer,tmp+1));
        return;
      }
      sleep(1);
    }
    std::string check(ws->data, ws->data.size());
    if(check == "OK"){
      INFO_MSG("Successful authentication of load balancer %s",addLoadBalancer->c_str());
      LoadBalancer* LB = new LoadBalancer(ws, *addLoadBalancer, ident);
      loadBalancers.insert(LB);
      identifiers.insert(ident);
      
      
      JSON::Value z;
      z[SENDCONFIG] = configToString();
      LB->send(z.asString());

      //start save timer
      time(&prevConfigChange);
      if(saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck,NULL);

      int tmp = 0;
      if(lastPromethNode.numSuccessConnectLB.count(*addLoadBalancer)){
        tmp = lastPromethNode.numSuccessConnectLB.at(*addLoadBalancer);
      }
      lastPromethNode.numSuccessConnectLB.insert(std::pair<std::string, int>(*addLoadBalancer,tmp+1));

      //start monitoring
      handleRequests(conn,ws,LB); 
    }else if(check == "noAuth"){
      addLB(addLoadBalancer);
    }
    return;
  }
/**
 * reconnect to load balancer
*/
void API::reconnectLB(void* p) {
  LoadBalancer* LB = (LoadBalancer*)p;
  identifiers.erase(LB->getIdent());
  std::string addLoadBalancer = LB->getName();

  Socket::Connection conn(addLoadBalancer.substr(0,addLoadBalancer.find(":")), atoi(addLoadBalancer.substr(addLoadBalancer.find(":")+1).c_str()), false, false);
  
  HTTP::URL url("ws://"+(addLoadBalancer));
  HTTP::Websocket* ws = new HTTP::Websocket(conn, url);
  

  ws->sendFrame("ident");

  //check responce
  int reset = 0;
  while(!ws->readFrame()){
    reset++;
    if(reset >= 20){
      WARN_MSG("auth failed: connection timeout");
      int tmp = 0;
      if(lastPromethNode.numFailedConnectLB.count(addLoadBalancer)){
        tmp = lastPromethNode.numFailedConnectLB.at(LB->getName());
      }
      lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(LB->getName(),tmp+1));
      reconnectLB(p);
      return;
    }
    sleep(1);
  }

  std::string ident(ws->data, ws->data.size());

  for (std::set<std::string>::iterator i = identifiers.begin(); i != identifiers.end(); i++){
    if(!(*i).compare(ident)){
      ws->sendFrame("noAuth");
      conn.close();
      WARN_MSG("load balancer already connected");
      int tmp = 0;
      if(lastPromethNode.numFailedConnectLB.count(addLoadBalancer)){
        tmp = lastPromethNode.numFailedConnectLB.at(LB->getName());
      }
      lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(LB->getName(),tmp+1));
      return;
    }
  }

  //send challenge
  std::string salt = generateSalt();
  ws->sendFrame("auth:" + salt);

  //check responce
  reset = 0;
  while(!ws->readFrame()){
    reset++;
    if(reset >= 20){
      WARN_MSG("auth failed: connection timeout");
      int tmp = 0;
      if(lastPromethNode.numFailedConnectLB.count(addLoadBalancer)){
        tmp = lastPromethNode.numFailedConnectLB.at(LB->getName());
      }
      lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(LB->getName(),tmp+1));
      reconnectLB(p);
      return;
    }
    sleep(1);
  }
  std::string result(ws->data, ws->data.size());
  
  if(Secure::sha256(passHash+salt).compare(result)){
    //unautherized
    WARN_MSG("unautherised");
    int tmp = 0;
    if(lastPromethNode.numFailedConnectLB.count(addLoadBalancer)){
      tmp = lastPromethNode.numFailedConnectLB.at(LB->getName());
    }
    lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(LB->getName(),tmp+1));
    ws->sendFrame("noAuth");
    return;
  }
  //send response to challenge
  reset = 0;
  while(!ws->readFrame()){
    reset++;
    if(reset >= 20){
      WARN_MSG("auth failed: connection timeout");
      int tmp = 0;
      if(lastPromethNode.numFailedConnectLB.count(addLoadBalancer)){
        tmp = lastPromethNode.numFailedConnectLB.at(LB->getName());
      }
      lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(LB->getName(),tmp+1));
      reconnectLB(p);
      return;
    }
    sleep(1);
  }
  std::string auth(ws->data, ws->data.size());
  std::string pass = Secure::sha256(passHash+auth);

  ws->sendFrame("salt:"+auth+";"+pass+" "+myName);

  reset = 0;
  while(!ws->readFrame()){
    reset++;
    if(reset >= 20){
      WARN_MSG("auth failed: connection timeout");
      int tmp = 0;
      if(lastPromethNode.numFailedConnectLB.count(addLoadBalancer)){
        tmp = lastPromethNode.numFailedConnectLB.at(LB->getName());
      }
      lastPromethNode.numFailedConnectLB.insert(std::pair<std::string, int>(LB->getName(),tmp+1));
      reconnectLB(p);
      return;
    }
    sleep(1);
  }
  std::string check(ws->data, ws->data.size());
  if(check == "OK"){
    INFO_MSG("Successful authentication of load balancer %s",addLoadBalancer.c_str());
    LoadBalancer* LB = new LoadBalancer(ws, addLoadBalancer, ident);
    loadBalancers.insert(LB);
    identifiers.insert(ident);
    LB->state = true;
    

    JSON::Value z;
    z[SENDCONFIG] = configToString();
    LB->send(z.asString());

    int tmp = 0;
    if(lastPromethNode.numSuccessConnectLB.count(LB->getName())){
      tmp = lastPromethNode.numSuccessConnectLB.at(LB->getName());
    }
    lastPromethNode.numSuccessConnectLB.insert(std::pair<std::string, int>(LB->getName(),tmp+1));
    //start monitoring
    handleRequests(conn,ws,LB); 
  }else {
    reconnectLB(p);
  }
  return;
}
/**
  * returns load balancer list
  */
std::string API::getLoadBalancerList(){
    std::string out = "\"loadbalancers\": [";  
    for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
      if(it != loadBalancers.begin()){
        out += ", ";
      }
      out += "\"" + (*it)->getName() + "\"";
    }
    out += "]";
    return out;
  }

/**
   * return server data of a server
   */
JSON::Value API::getHostState(const std::string host){
    JSON::Value ret;
    for (std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state == STATE_OFF){continue;}
      if ((*it)->details->host == host){
        ret = stateLookup[(*it)->state];
        if ((*it)->state != STATE_ACTIVE){continue;}
        (*it)->details->fillState(ret);
        break;
      }
    }
    return ret;
  }
/**
   * return all server data
   */
JSON::Value API::getAllHostStates(){
    JSON::Value ret;
    for (std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state == STATE_OFF){continue;}
      ret[(*it)->details->host] = stateLookup[(*it)->state];
      if ((*it)->state != STATE_ACTIVE){continue;}
      (*it)->details->fillState(ret[(*it)->details->host]);
    }
    return ret;
  }

/**
   * return viewer counts of streams
   */
JSON::Value API::getViewers(){
    JSON::Value ret;
    for (std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state == STATE_OFF){continue;}
        (*it)->details->fillStreams(ret);
    }
    return ret;
  }
/**
   * get view count of a stream
   */
uint64_t API::getStream(const std::string stream){
    uint64_t count = 0;
    for (std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state == STATE_OFF){continue;}
      count += (*it)->details->getViewers(stream);
    }
    return count;   
  }
/**
   * return stream stats
   */
JSON::Value API::getStreamStats(const std::string streamStats){
    JSON::Value ret;
    for (std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state == STATE_OFF){continue;}
      (*it)->details->fillStreamStats(streamStats, ret);
    }
    return ret;
  }
 
/**
   * return the best source of a stream for inter server replication
   */
void API::getSource(Socket::Connection conn, HTTP::Parser H, const std::string source, const std::string fback, bool repeat = true){
    H.Clean();
    H.SetHeader("Content-Type", "text/plain");
    INFO_MSG("Finding source for stream %s", source.c_str());
    std::string bestHost = "";
    std::map<std::string, int32_t> tagAdjust;
    if (H.GetVar("tag_adjust") != ""){fillTagAdjust(tagAdjust, H.GetVar("tag_adjust"));}
    if (H.hasHeader("X-Tag-Adjust")){fillTagAdjust(tagAdjust, H.GetHeader("X-Tag-Adjust"));}
    double lat = 0;
    double lon = 0;
    if (H.GetVar("lat") != ""){
      lat = atof(H.GetVar("lat").c_str());
      H.SetVar("lat", "");
    }
    if (H.GetVar("lon") != ""){
      lon = atof(H.GetVar("lon").c_str());
      H.SetVar("lon", "");
    }
    if (H.hasHeader("X-Latitude")){lat = atof(H.GetHeader("X-Latitude").c_str());}
    if (H.hasHeader("X-Longitude")){lon = atof(H.GetHeader("X-Longitude").c_str());}
    uint64_t bestScore = 0;
    
    for (std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state != STATE_ONLINE){continue;}
      if (Socket::matchIPv6Addr(std::string((*it)->details->binHost, 16), conn.getBinHost(), 0)){
        INFO_MSG("Ignoring same-host entry %s", (*it)->details->host.data());
        continue;
      }
      uint64_t score = (*it)->details->source(source, tagAdjust, 0, lat, lon);
      if (score > bestScore){
        bestHost = "dtsc://" + (*it)->details->host;
        bestScore = score;
      }
    }
    if (bestScore == 0){
      if(repeat){
        extraServer();
        getSource(conn, H, source, fback, false);
      }
      if (fback.size()){
        bestHost = fback;
      }else{
        bestHost = fallback;
      }
      lastPromethNode.numFailedSource++;
      FAIL_MSG("No source for %s found!", source.c_str());
    }else{
      lastPromethNode.numSuccessSource++;
      INFO_MSG("Winner: %s scores %" PRIu64, bestHost.c_str(), bestScore);
    }
    H.SetBody(bestHost);
    H.setCORSHeaders();
    H.SendResponse("200", "OK", conn);
    H.Clean();    
  }
/**
   * return optimal server to start new stream on
   */
void API::getIngest(Socket::Connection conn, HTTP::Parser H, const std::string ingest, const std::string fback, bool repeat = true){
    H.Clean();
    H.SetHeader("Content-Type", "text/plain");
    double cpuUse = atoi(ingest.c_str());
    INFO_MSG("Finding ingest point for CPU usage %.2f", cpuUse);
    std::string bestHost = "";
    std::map<std::string, int32_t> tagAdjust;
    if (H.GetVar("tag_adjust") != ""){fillTagAdjust(tagAdjust, H.GetVar("tag_adjust"));}
    if (H.hasHeader("X-Tag-Adjust")){fillTagAdjust(tagAdjust, H.GetHeader("X-Tag-Adjust"));}
    double lat = 0;
    double lon = 0;
    if(H.GetVar("lat") != ""){
      lat = atof(H.GetVar("lat").c_str());
      H.SetVar("lat", "");
    }
    if(H.GetVar("lon") != ""){
      lon = atof(H.GetVar("lon").c_str());
      H.SetVar("lon", "");
    }
    if(H.hasHeader("X-Latitude")){lat = atof(H.GetHeader("X-Latitude").c_str());}
    if(H.hasHeader("X-Longitude")){lon = atof(H.GetHeader("X-Longitude").c_str());}

    uint64_t bestScore = 0;
    for (std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state != STATE_ONLINE){continue;}
      uint64_t score = (*it)->details->source("", tagAdjust, cpuUse * 10, lat, lon);
      if (score > bestScore){
        bestHost = (*it)->details->host;
        bestScore = score;
      }
    }
    if (bestScore == 0){
      if(repeat){
        extraServer();
        getIngest(conn, H, ingest, fback, false);
        return;
      }
      if (fback.size()){
        bestHost = fback;
      }else{
        bestHost = fallback;
      }
      lastPromethNode.numFailedIngest++;
      FAIL_MSG("No ingest point found!");
    }else{
      lastPromethNode.numSuccessIngest++;
      INFO_MSG("Winner: %s scores %" PRIu64, bestHost.c_str(), bestScore);
    }
    H.SetBody(bestHost);
    H.setCORSHeaders();
    H.SendResponse("200", "OK", conn);
    H.Clean();
  }
  
/**
   * create stream
   */
void API::stream(Socket::Connection conn, HTTP::Parser H, std::string proto, std::string stream, bool repeat = true){
    H.Clean();
    H.SetHeader("Content-Type", "text/plain");
    // Balance given stream
      std::map<std::string, int32_t> tagAdjust;
      if (H.GetVar("tag_adjust") != ""){
        fillTagAdjust(tagAdjust, H.GetVar("tag_adjust"));
        H.SetVar("tag_adjust", "");
      }
      if (H.hasHeader("X-Tag-Adjust")){fillTagAdjust(tagAdjust, H.GetHeader("X-Tag-Adjust"));}
      H.SetVar("proto", "");
      std::string vars = H.allVars();
      if (stream == "favicon.ico"){
        H.Clean();
        H.SendResponse("404", "No favicon", conn);
        H.Clean();
        lastPromethNode.numIllegalViewer++;
        return;
      }
      INFO_MSG("Balancing stream %s", stream.c_str());
      hostEntry *bestHost = 0;
      uint64_t bestScore = 0;
      for (std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
        if ((*it)->state != STATE_ACTIVE){continue;}
        uint64_t score = (*it)->details->rate(stream, tagAdjust);
        if (score > bestScore){
          bestHost = *it;
          bestScore = score;
        }
      }
      if (!bestScore || !bestHost){
        if(repeat){
          extraServer();
          API::stream(conn, H, proto, stream, false);
        }else{
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.setCORSHeaders();
          H.SetBody(fallback);
          lastPromethNode.numFailedViewer++;
          FAIL_MSG("All servers seem to be out of bandwidth!");
        }
      }else{
        INFO_MSG("Winner: %s scores %" PRIu64, bestHost->details->host.c_str(), bestScore);
        bestHost->details->addViewer(stream, true);
        H.Clean();
        H.SetHeader("Content-Type", "text/plain");
        H.setCORSHeaders();
        H.SetBody(bestHost->details->host);
        lastPromethNode.numSuccessViewer++;
        int tmp = 0;
        if(lastPromethNode.numStreams.count(bestHost->name)){
          tmp = lastPromethNode.numStreams.at(bestHost->name);
        }
        lastPromethNode.numStreams.insert(std::pair<std::string, int>(bestHost->name,tmp+1));
      }
      if (proto != "" && bestHost && bestScore){
        H.SetHeader("Content-Type", "text/plain");
        H.Clean();
        H.setCORSHeaders();
        H.SetHeader("Location", bestHost->details->getUrl(stream, proto) + vars);
        H.SetBody(H.GetHeader("Location"));
        H.SendResponse("307", "Redirecting", conn);
        H.Clean();
      }else{
        H.SendResponse("200", "OK", conn);
        H.Clean();
      }
    }// if HTTP request received
/**
   * add viewer to stream on server
   */
void API::addViewer(std::string stream, const std::string addViewer){
    for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if((*it)->name == addViewer){
        (*it)->details->addViewer(stream, true);
        break;
      }
    }
  }


int main(int argc, char **argv){
  Util::redirectLogsIfNeeded();
  Util::Config conf(argv[0]);
  cfg = &conf;
  JSON::Value opt;

  opt["arg"] = "integer";
  opt["short"] = "p";
  opt["long"] = "port";
  opt["help"] = "TCP port to listen on";
  opt["value"].append(8042u);
  conf.addOption("port", opt);

  opt["arg"] = "string";
  opt["short"] = "P";
  opt["long"] = "passphrase";
  opt["help"] = "Passphrase (prometheus option value) to use for data retrieval.";
  opt["value"][0u] = "koekjes";
  conf.addOption("passphrase", opt);

  opt["arg"] = "string";
  opt["short"] = "i";
  opt["long"] = "interface";
  opt["help"] = "Network interface to listen on";
  opt["value"][0u] = "0.0.0.0";
  conf.addOption("interface", opt);

  opt["arg"] = "string";
  opt["short"] = "u";
  opt["long"] = "username";
  opt["help"] = "Username to drop privileges to";
  opt["value"][0u] = "root";
  conf.addOption("username", opt);

  opt["arg"] = "string";
  opt["short"] = "F";
  opt["long"] = "fallback";
  opt["help"] = "Default reply if no servers are available";
  opt["value"][0u] = "FULL";
  conf.addOption("fallback", opt);

  opt["arg"] = "integer";
  opt["short"] = "R";
  opt["long"] = "ram";
  opt["help"] = "Weight for RAM scoring";
  opt["value"].append(weight_ram);
  conf.addOption("ram", opt);

  opt["arg"] = "integer";
  opt["short"] = "C";
  opt["long"] = "cpu";
  opt["help"] = "Weight for CPU scoring";
  opt["value"].append(weight_cpu);
  conf.addOption("cpu", opt);

  opt["arg"] = "integer";
  opt["short"] = "B";
  opt["long"] = "bw";
  opt["help"] = "Weight for BW scoring";
  opt["value"].append(weight_bw);
  conf.addOption("bw", opt);

  opt["arg"] = "integer";
  opt["short"] = "G";
  opt["long"] = "geo";
  opt["help"] = "Weight for geo scoring";
  opt["value"].append(weight_geo);
  conf.addOption("geo", opt);

  opt["arg"] = "string";
  opt["short"] = "A";
  opt["long"] = "auth";
  opt["help"] = "load balancer authentication key";
  conf.addOption("auth", opt);

  opt["arg"] = "integer";
  opt["short"] = "X";
  opt["long"] = "extra";
  opt["help"] = "Weight for extra scoring when stream exists";
  opt["value"].append(weight_bonus);
  conf.addOption("extra", opt);

  opt.null();
  opt["short"] = "L";
  opt["long"] = "localmode";
  opt["help"] = "Control only from local interfaces, request balance from all";
  conf.addOption("localmode", opt);

  opt.null();
  opt["short"] = "c";
  opt["long"] = "config";
  opt["help"] = "load config settings from file";
  conf.addOption("load", opt);
  
  opt["arg"] = "string";
  opt["short"] = "H";
  opt["long"] = "host";
  opt["help"] = "Host name and port where this load balancer can be reached";
  conf.addOption("myName", opt);


  conf.parseArgs(argc, argv);



  std::string password = "default";//set default password for load balancer communication
  passphrase = conf.getOption("passphrase").asStringRef();
  password = conf.getString("auth");
  weight_ram = conf.getInteger("ram");
  weight_cpu = conf.getInteger("cpu");
  weight_bw = conf.getInteger("bw");
  weight_geo = conf.getInteger("geo");
  weight_bonus = conf.getInteger("extra");
  fallback = conf.getString("fallback");
  bool load = conf.getBool("load");
  myName = conf.getString("myName");

  if(myName.find(":") == std::string::npos){
    myName.append(":"+conf.getString("port"));
  }

  
  conf.activate();

  api = API();
  loadBalancers = std::set<LoadBalancer*>();
  //setup saving
  saveTimer = 0;
  time(&prevSaveTime);
  //api login
  srand(time(0)+getpid());//setup random num generator
  std::string salt = generateSalt();
  userAuth.insert(std::pair<std::string, std::pair<std::string, std::string> >("admin",std::pair<std::string, std::string>(Secure::sha256("default"+salt), salt)));
  bearerTokens.insert("test1233");
  //add localhost to whitelist
  if(conf.getBool("localmode")) {
    whitelist.insert("::1/128");
    whitelist.insert("127.0.0.1/24");
  }

  identifier = generateSalt();
  identifiers.insert(identifier);
  

  if(load){
    loadFile();
  }else{
    passHash = Secure::sha256(password);
  }

  std::map<std::string, tthread::thread *> threads;
  
  checkServerMonitors();

  new tthread::thread(timerAddViewer, NULL);
  new tthread::thread(checkNeedRedirect, NULL);
  new tthread::thread(prometheusTimer, NULL);
  conf.serveThreadedSocket(api.handleRequest);
  if (!conf.is_active){
    WARN_MSG("Load balancer shutting down; received shutdown signal");
  }else{
    WARN_MSG("Load balancer shutting down; socket problem");
  }
  conf.is_active = false;
  saveFile();

  // Join all threads
  for (std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
    if (!(*it)->name[0]){continue;}
    (*it)->state = STATE_GODOWN;
  }
  for (std::set<hostEntry*>::iterator i = hosts.begin(); i != hosts.end(); i++){cleanupHost(**i);}
  std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); 
  while(loadBalancers.size()){
    (*it)->send("close");
    (*it)->Go_Down = true;
    loadBalancers.erase(it);
    it = loadBalancers.begin(); 
  }
}
