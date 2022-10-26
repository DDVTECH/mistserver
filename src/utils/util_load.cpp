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

//const api names set multiple times
std::string const ADDLOADBALANCER = "addloadbalancer";
std::string const REMOVELOADBALANCER = "removeloadbalancer";
std::string const RESEND = "resend";
std::string const REMOVEHOST = "removehost";
std::string const UPDATEHOST = "updatehost";
std::string const WEIGHTS = "weights";
std::string const ADDVIEWER = "addviewer";

//config file name
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


Util::Config *cfg = 0;
std::string passphrase;
std::string fallback;
tthread::mutex globalMutex;
tthread::mutex fileMutex;
std::map<std::string, int32_t> blankTags;
size_t weight_cpu = 500;
size_t weight_ram = 500;
size_t weight_bw = 1000;
size_t weight_geo = 1000;
size_t weight_bonus = 50;


API api;
std::set<hostEntry*> hosts; ///array holding all hosts
std::set<LoadBalancer*> loadBalancers; //array holding all load balancers in the mesh

//authentication storage
std::map<std::string,std::string> userAuth;
std::set<std::string> bearerTokens;
std::string passHash;
std::set<IpPolicy*> whitelist;
std::map<std::string, std::time_t> activeSalts;
#define SALTSIZE 10

//file save and loading vars
std::string const fileloc  = "config.txt";
#define SAVETIMEINTERVAL 5 //time to save after config change in minutes
std::time_t prevSaveTime;
std::time_t now;
std::time_t prevConfigChange;
tthread::thread* saveTimer;


/**
 * constructor of ip policy (ipv4 & ipv6)
*/
IpPolicy::IpPolicy(std::string policy){
  Util::stringToLower(policy);
  //remove <space>
  while(policy.find(" ") != -1){
    policy.erase(policy.find(" "));
  }
  while(policy.find(".") != -1){//convert ipv4 to ipv6 format for handleing
    policy.replace(policy.find("."),1,":");
  }
  //create policy
  delimiterParser o(policy, "+");
  std::string p = o.next();
  bool invalid = false;
  int mask = atoi(p.substr(p.find('/')+1,p.size()).c_str());
  if(mask > 0 && mask <= 128) {//check if poilcy invalid if so skip policy
    andp = p;
  }else {
    invalid = true;
  }
  p = o.next();
  while(p.size()){
    whitelist.insert(new IpPolicy(p));
  }
  if(invalid){delete this;}
}

/**
 * helper function to get next ip num set
*/
std::string IpPolicy::getNextFrame(delimiterParser pol) const{
  std::string ret = pol.next();
  while(ret.size()<4){
    ret.insert(0,"0");
  }
  return ret;
}

/**
 * \returns true if \param ip contains a ip that is allowed by this ip policy
*/
bool IpPolicy::match(std::string ip) const{
  Util::stringToLower(ip);
  //get mask of policy as int
  int mask = atoi(andp.substr(andp.find('/')+1,andp.size()).c_str());

  delimiterParser pol(andp, ":");
  delimiterParser test(ip, ":");

  //check if set of 4 numbers match
  for(int i = 0; i == mask/16; i++){    
    if(getNextFrame(pol).compare(getNextFrame(test))) return false;
  }

  //check if matched policy
  int bits = mask % 16;
  if(bits > 0){
    std::string polLine = getNextFrame(pol);
    std::string testLine = getNextFrame(test);
    //check single numbers
    int divbits = bits/4;
    for(int i = 0; i < divbits; i++){
      if(polLine.at(i) != testLine.at(i)) return false;
    }
    //check bits of single number
    if(bits%4 > 0){
      int polVal = polLine.at(divbits);
      if (polVal >60) polVal -= 51;
      if (polVal >= 30 && polVal < 40) polVal -= 30;
      int testVal = testLine.at(divbits);
      if (testVal >60) testVal -= 51;
      if (testVal >= 30 && testVal < 40) testVal -= 30;
      //calc masked values of address
      //first bit
      int numpol = polVal/8;
      int numtest = testVal/8;

      if(bits >= 2){//second bit
        numpol += polVal/4;
        numtest += polVal/4;
      }
      if(bits >= 3){//third bit
        numpol += polVal/2;
        numtest += testVal/2;
      }
      if(numpol != numtest) return false;
    }
  }
  
  return true;
}

/**
 * \returns true if ip Policy \param ip is equivilent to this object
*/
bool IpPolicy::equals(std::string ip) const{
  Util::stringToLower(ip);
  //remove <space>
  while(ip.find(" ") != -1){
    ip.erase(ip.find(" "));
  }
  while(ip.find(".") != -1){//convert ipv4 to ipv6 format for handleing
    ip.replace(ip.find("."),1,":");
  }
  

  delimiterParser o(ip, "+");
  std::string p = o.next();

  int maskpol = atoi(andp.substr(andp.find('/')+1,andp.size()).c_str());
  while(p.compare("")){
    //get mask of policy as int
    
    int maskline = atoi(p.substr(p.find('/')+1,p.size()).c_str());
    if(maskpol != maskline){continue;}

    delimiterParser pol(andp, ":");
    delimiterParser test(p, ":");

    bool match = true;
    //check if set of 4 numbers match
    for(int i = 0; i == maskpol/16; i++){    
      if(getNextFrame(pol).compare(getNextFrame(test))) {
        match = false;
        break;
      }
    }
    if (!match) continue; 

    //check if matched policy
    int bits = maskpol % 16;
    if(bits > 0){
      std::string polLine = getNextFrame(pol);
      std::string testLine = getNextFrame(test);
      //check single numbers
      int divbits = bits/4;
      for(int i = 0; i < divbits; i++){
        if(polLine.at(i) != testLine.at(i)) {
          match = false;
          break;
        }
      }
      if (!match) continue; 
      //check bits of single number
      if(bits%4 > 0){
        int polVal = polLine.at(divbits);
        if (polVal >60) polVal -= 51;
        if (polVal >= 30 && polVal < 40) polVal -= 30;
        int testVal = testLine.at(divbits);
        if (testVal >60) testVal -= 51;
        if (testVal >= 30 && testVal < 40) testVal -= 30;
        //calc masked values of address1
        //first bit
        int numpol = polVal/8;
        int numtest = testVal/8;

        if(bits >= 2){//second bit
          numpol += polVal/4;
          numtest += polVal/4;
        }
        if(bits >= 3){//third bit
          numpol += polVal/2;
          numtest += testVal/2;
        }
        if(numpol != numtest) continue;
        else return true;
      }
    }
    p = o.next();
  }
  return false;
}

/**
 * \returns this object as a string
*/
JSON::Value streamDetails::stringify() const{
  JSON::Value out;
  out["total"] = total;
  out["inputs"] = inputs;
  out["bandwidth"] = bandwidth;
  out["prevTotal"] = prevTotal;
  out["bytesUp"] = bytesUp;
  out["bytesDown"] = bytesDown;
  return out;
}

/**
 * \returns \param j as a streamDetails object
*/
streamDetails* streamDetails::destringify(JSON::Value j){
    streamDetails* out = new streamDetails();
    out->total = j["total"].asInt();
    out->inputs = j["inputs"].asInt();
    out->bandwidth = j["bandwidth"].asInt();
    out->prevTotal = j["prevTotal"].asInt();
    out->bytesUp = j["bytesUp"].asInt();
    out->bytesDown = j["bytesDown"].asInt();
    return out;
  }


/**
 * construct an object to represent an other load balancer
*/
LoadBalancer::LoadBalancer(HTTP::Websocket* ws, std::string name) : LoadMutex(0), ws(ws), name(name), Go_Down(false) {}

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
    if(!Go_Down){
      ws->sendFrame(ret);
    }
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
  j["pre"] = pre;
  j["post"] = post;
  return j;
}

/**
 * turn json \param j into outUrl
*/
outUrl outUrl::destringify(JSON::Value j){
  outUrl r;
  r.pre = j["pre"].asString();
  r.post = j["post"].asString();
  return r;
}

/**
 * convert ipPolicy set \param s to json
*/
std::string convertSetToJson(std::set<IpPolicy*> s){
  std::string out = "[";  
  for(std::set<IpPolicy*>::iterator it = s.begin(); it != s.end(); ++it){
      if(it != s.begin()){
        out += ", ";
      }
      out += (*it)->andp;
  }
  out += "]";
  return out;
}

/**
 * convert set \param s to json
*/
std::string convertSetToJson(std::set<std::string> s){
  std::string out = "[";  
  for(std::set<std::string>::iterator it = s.begin(); it != s.end(); ++it){
      if(it != s.begin()){
        out += ", ";
      }
      out += *it;
  }
  out += "]";
  return out;
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
 * convert a json \param j to a set<string>
*/
std::set<std::string> convertJsonToSet(JSON::Value j){
  std::set<std::string> s;
  jsonForEach(j,i){
    s.insert(i->asString());
  }
  return s;
}

/**
 * convert json \param j to IpPolicy set
*/
std::set<IpPolicy*>* convertJsonToIpPolicylist(JSON::Value j){
  std::set<IpPolicy*>* s = new std::set<IpPolicy*>();
  jsonForEach(j,i){
    s->insert(new IpPolicy(j));
  }
  return s;
}

/**
 * convert json \param j to map<string, string>
*/
std::map<std::string, std::string> convertJsonToMap(JSON::Value j){
  std::map<std::string, std::string> m;
  for(int i = 0; i < j.size(); i++){
    m.insert(std::pair<std::string, std::string>(j[i], j[i].asString()));
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

inline double toRad(double degree) {
  return degree / 57.29577951308232087684;
}

double geoDist(double lat1, double long1, double lat2, double long2) {
  double dist;
  dist = sin(toRad(lat1)) * sin(toRad(lat2)) + cos(toRad(lat1)) * cos(toRad(lat2)) * cos(toRad(long1 - long2));
  return .31830988618379067153 * acos(dist);
}


hostDetails::hostDetails(std::set<LoadBalancer*> LB, char* name) : hostMutex(0), name(name), LB(LB) {
    
  }
hostDetails::~hostDetails(){
    if(hostMutex){
      delete hostMutex;
      hostMutex = 0;
    }
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
    try{
      if(s.size() && (!streams.count(s) || !streams.at(s).inputs)){return 0;}
    }catch (std::out_of_range const&){
      return 0;
    }

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
    outUrl o;
    try{
      const outUrl o = outputs.at(proto);
      return o.pre + s + o.post;
    }catch (std::out_of_range const&){
      return NULL;
    }
  }
/**
 * Sends update to original load balancer to add a viewer.
*/
void hostDetails::addViewer(std::string &s, bool RESEND){
  if(RESEND){
    JSON::Value j;
    j["addViewer"] = s;
    j["host"] = host;
    for(std::set<LoadBalancer*>::iterator it = LB.begin(); it != LB.end(); it++){
      (*it)->send(j.asString());
    }
  }
}
/**
 * Update precalculated host vars.
*/
void hostDetails::update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource, uint64_t scoreRate, std::map<std::string, outUrl> outputs, std::set<std::string> conf_streams, std::map<std::string, streamDetails> streams, std::set<std::string> tags, uint64_t cpu, double servLati, double servLongi, const char* binHost, std::string host){
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
    

  }
/**
   * Update precalculated host vars without protected vars
   */
void hostDetails::update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource, uint64_t scoreRate){
    if(!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);

    this->fillStateOut = fillStateOut;
    this->fillStreamsOut = fillStreamsOut;
    this->scoreSource = scoreSource;
    this->scoreRate = scoreRate;
  }

/**
   * allow for json inputs instead of sets and maps for update function
   */
void hostDetails::update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource, uint64_t scoreRate, JSON::Value outputs, JSON::Value conf_streams, JSON::Value streams, JSON::Value tags, uint64_t cpu, double servLati, double servLongi, const char* binHost, std::string host){  
    std::map<std::string, outUrl> out;
    std::map<std::string, streamDetails> s;
    streamDetails x;
    if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
    for(int i = 0; i < streams.size(); i++){
      s.insert(std::pair<std::string, streamDetails>(streams[STREAMSKEY][i]["key"], *(x.destringify(streams[STREAMSKEY][i]["streamDetails"]))));
    }
    update(fillStateOut, fillStreamsOut, scoreSource, scoreRate, out, convertJsonToSet(conf_streams), s, convertJsonToSet(tags), cpu, servLati, servLongi, binHost, host);
  }
  
/**
 * constructor of monitored server
*/
hostDetailsCalc::hostDetailsCalc(char* name) : hostDetails(std::set<LoadBalancer*>(), name), ramMax(0),ramCurr(0), upSpeed(0), 
  downSpeed(0), total(0), upPrev(0), downPrev(0), prevTime(0), addBandwidth(0), availBandwidth(128 * 1024 * 1024) {
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
    r["up_add"] = addBandwidth;
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
      r["score"]["bw"] = (uint64_t)(weight_bw - (((upSpeed + addBandwidth) * weight_bw) / availBandwidth));
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
    if (upSpeed >= availBandwidth || (upSpeed + addBandwidth) >= availBandwidth){
      INFO_MSG("Host %s over bandwidth: %" PRIu64 "+%" PRIu64 " >= %" PRIu64, host.c_str(), upSpeed,
               addBandwidth, availBandwidth);
      return 0;
    }
    
    // Calculate score
    uint64_t cpu_score = (weight_cpu - (cpu * weight_cpu) / 1000);
    uint64_t ram_score = (weight_ram - ((ramCurr * weight_ram) / ramMax));
    uint64_t bw_score = (weight_bw - (((upSpeed + addBandwidth) * weight_bw) / availBandwidth));
  
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
    if (upSpeed >= availBandwidth || (upSpeed + addBandwidth) >= availBandwidth){
      INFO_MSG("Host %s over bandwidth: %" PRIu64 "+%" PRIu64 " >= %" PRIu64, host.c_str(), upSpeed,
               addBandwidth, availBandwidth);
      return 1;
    }
    // Calculate score
    uint64_t cpu_score = (weight_cpu - (cpu * weight_cpu) / 1000);
    uint64_t ram_score = (weight_ram - ((ramCurr * weight_ram) / ramMax));
    uint64_t bw_score = (weight_bw - (((upSpeed + addBandwidth) * weight_bw) / availBandwidth));

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

    //update the local precalculated varsnclude <mist/config.h>

    ((hostDetails*)(this))->update(fillStateOut, fillStreamsOut, scoreSource, scoreRate);

    //create json to send to other load balancers
    JSON::Value j;
    j[FILLSTATEOUT] = fillStateOut;
    j[FILLSTREAMSOUT] = fillStreamsOut;
    j[SCORESOURCE] = scoreSource;
    j[SCORERATE] = scoreRate;
    
    
    j[OUTPUTSKEY] = convertMapToJson(outputs);
    j[CONFSTREAMSKEY] = convertSetToJson(conf_streams);
    j[STREAMSKEY] = convertMapToJson(streams);
    j[TAGSKEY] = convertSetToJson(tags);
    
    j[CPUKEY] = cpu;
    j[SERVLATIKEY] = servLati;
    j[SERVLONGIKEY] = servLongi;
    j[HOSTNAMEKEY] = name;

    JSON::Value out;
    out["updateHost"] = j;

    //send to other load balancers
    for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
      (*it)->send(out.asString());
    }
  }
  
/**
   * add the viewer to this host
   * updates all precalculated host vars
   */
void hostDetailsCalc::addViewer(std::string &s, bool RESEND){
    if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
    uint64_t toAdd = 0;
    if (streams.count(s)){
      toAdd = streams[s].bandwidth;
    }else{
      if (total){
        toAdd = (upSpeed + downSpeed) / total;
      }else{
        toAdd = 131072; // assume 1mbps
      }
    }
    // ensure reasonable limits of bandwidth guesses
    if (toAdd < 64 * 1024){toAdd = 64 * 1024;}// minimum of 0.5 mbps
    if (toAdd > 1024 * 1024){toAdd = 1024 * 1024;}// maximum of 8 mbps
    addBandwidth += toAdd;
    calc();
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
    addBandwidth *= 0.75;
    calc();//update preclaculated host vars
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
  ((hostDetailsCalc*)(entry->details))->availBandwidth = bandwidth.asInt();
  ((hostDetailsCalc*)(entry->details))->host = url.host;
  entry->state = STATE_BOOT;
  bool down = true;

  HTTP::Downloader DL;

  while (cfg->is_active && (entry->state != STATE_GODOWN)){
    if (DL.get(url) && DL.isOk()){
      JSON::Value servData = JSON::fromString(DL.data());
      if (!servData){
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
        }
        ((hostDetailsCalc*)(entry->details))->update(servData);
      }
    }else{
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
  if(entry->state != STATE_ONLINE){//notify other load balancers server is unreachable
    JSON::Value j;
    j["removeHost"] = true;
    for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
      (*it)->send(j);
    }
  }
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
void initForeignHost(const std::string &N, const std::string LB){
  
  // Cancel if this host has no name or load balancer set
  if (!N.size()){return;}
  std::set<LoadBalancer*> LBList;
  //add if load balancer in mesh
  
    std::set<LoadBalancer*>::iterator i = loadBalancers.begin();
    while(i != loadBalancers.end()){
      if((*i)->getName() == LB){//check if LB is  mesh
        LBList.insert(*i);
        break;
      }else if((*i)->getName() > LB){//check if past LB in search
        break;
      }else {//go to next in mesh list
        i++;
      }
    }
  //check LB 
  if(LBList.empty()){return;}
  hostEntry* H = new hostEntry();
  hosts.insert(H);
  H->state = STATE_ONLINE;
  H->details = new hostDetails(LBList, H->name);
  memset(H->name, 0, HOSTNAMELEN);
  memcpy(H->name, N.data(), N.size());
  H->thread = 0;
  INFO_MSG("Created foreign server %s", H->name);
}

/**
 * remove monitored server or foreign server at \param H
*/
void cleanupHost(hostEntry &H){
  // Cancel if this host has no name set
  if (!H.name[0]){return;}
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
    JSON::Value j;
    j[CONFIGFALLBACK] = fallback;
    j[CONFIGC] = weight_cpu;
    j[CONFIGR] = weight_ram;
    j[CONFIGBW] = weight_bw;
    j[CONFIGWG] = weight_geo;
    j[CONFIGWB] = weight_bonus;
    j[CONFIGPASS] = passHash;
    j[CONFIGSPASS] = passphrase;
    j[CONFIGPORT] = cfg->getString("port");
    j[CONFIGINTERFACE] = cfg->getString("interface");
    j[CONFIGWHITELIST] = convertSetToJson(whitelist);
    j[CONFIGBEARER] = convertSetToJson(bearerTokens);
    j[CONFIGUSERS] = convertMapToJson(userAuth);
    //serverlist 
    std::set<std::string> servers;
    for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if((*it)->thread != 0){
        servers.insert((*it)->name);
      }
    }
    j[CONFIGSERVERS] = convertSetToJson(servers);

    file << j.asString().c_str();
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
    file.close();
  }
  
  //change config vars
  JSON::Value j = JSON::fromString(data);
  fallback = j[CONFIGFALLBACK].asString();
  weight_cpu = j[CONFIGC].asInt();
  weight_ram = j[CONFIGR].asInt();
  weight_bw = j[CONFIGBW].asInt();
  weight_geo = j[CONFIGWG].asInt();
  weight_bonus = j[CONFIGWB].asInt();
  passHash = j[CONFIGPASS].asString();
  passphrase = j[CONFIGSPASS].asStringRef();
  cfg->addOption("port", j[CONFIGPORT].asString());
  cfg->addOption("interface", j[CONFIGINTERFACE].asString());
  bearerTokens = convertJsonToSet(j[CONFIGBEARER]);
  std::set<IpPolicy*>* tmp = convertJsonToIpPolicylist(j[CONFIGWHITELIST]);
  whitelist = *tmp;
  userAuth = convertJsonToMap(j[CONFIGUSERS]);

  //serverlist 
  //remove monitored servers
  for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
    if((*it)->thread != 0){//check if monitoring
      cleanupHost(*(*it));
    }
  }
  //add new servers
  for(int i = 0; i < j[CONFIGSERVERS].size(); i++){
    hostEntry* e = new hostEntry();
    initHost(*e,j[CONFIGSERVERS][i]);
  }
  //add command line servers
  JSON::Value &nodes = cfg->getOption("server", true);
  jsonForEach(nodes, it){
    if (it->asStringRef().size() > 199){
      FAIL_MSG("Host length too long for monitoring, skipped: %s", it->asStringRef().c_str());
      continue;
    }
    hostEntry* newHost = new hostEntry();
    initHost(*newHost, it->asStringRef());
    hosts.insert(newHost);
  }
  INFO_MSG("loaded config");
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
 * \return s until first \param delimiter or end of string as an Int
*/
int delimiterParser::nextInt() {
  return atoi(this->next().c_str());
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

      if(H.method.compare("PUT") && !api.compare("stream")){
        stream(conn, H, path.next(), path.next());
      }
  

      //Authentication
      std::string creds = H.GetHeader("Authorization");
      //auth with username and password
      if(!creds.substr(0,5).compare("Basic")){
        delimiterParser cred(Encodings::Base64::decode(creds.substr(6,creds.size())),":");
        //check if user exists
        std::map<std::string, std::string>::iterator user = userAuth.find(cred.next());
        //check password
        if(user == userAuth.end() || ((*user).second).compare(cred.next())) {
          H.SetBody("invalid credentials");
          H.setCORSHeaders();
          H.SendResponse("403", "Forbidden", conn);
          H.Clean();
          conn.close();
          continue;
        }
      }
      //auth with bearer token
      else if(!creds.substr(0,7).compare("Bearer ")){
        if(!bearerTokens.count(creds.substr(7,creds.size()))){
          H.SetBody("invalid token");
          H.setCORSHeaders();
          H.SendResponse("403", "Forbidden", conn);
          H.Clean();
          conn.close();
          continue;
        }
      }
      //whitelist ipv6 & ipv4
      else if(conn.getHost().size()){
        bool found = false;
        std::set<IpPolicy*>::iterator it = whitelist.begin();
        while( it != whitelist.end()){
          if((*it)->match(conn.getHost())){
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
          continue;
        }
      }
      //block other auth forms including none
      else{
        H.SetBody("no credentials given");
        H.setCORSHeaders();
        H.SendResponse("403", "Forbidden", conn);
        H.Clean();
        conn.close();
        continue;
      }

     //API METHODS     
      if(!H.method.compare("PUT")){
        if(!api.compare("save")){
          saveFile(true);
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody("OK");
          H.setCORSHeaders();
          H.SendResponse("204", "OK", conn);
          H.Clean();
        }
        //load
        else if(!api.compare("load")){
          loadFile(true);
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody("OK");
          H.setCORSHeaders();
          H.SendResponse("204", "OK", conn);
          H.Clean();
        }
        //return load balancer list
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
        }
        // Add server to list
        else if (!api.compare("server")){
          JSON::Value ret;
          addServer(ret, path.next());
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          if(ret.isNull()){
            H.SetBody("Host length too long for monitoring");
            H.setCORSHeaders();
            H.SendResponse("201", "OK", conn);
            H.Clean();
          }else {
            H.SetBody(ret.toPrettyString());
            H.setCORSHeaders();
            H.SendResponse("201", "OK", conn);
            H.Clean();
          }
        }
        //auth
        else if(!api.compare("auth")){
          api = path.next();
          // add bearer token
          if (!api.compare("bearer")){
            bearerTokens.insert(path.next());
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("OK");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
          }
          // add user acount
          else if (!api.compare("user")){
            std::string userName = path.next();
            userAuth.insert(std::pair<std::string, std::string>(userName,path.next()));
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("OK");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
          }
          // add whitelist policy
          else if (!api.compare("whitelist")){
            IpPolicy* tmp = new IpPolicy(H.body);
            if(tmp != NULL) whitelist.insert(tmp);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("OK");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
          }
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
        }
        // Find source for given stream
        else if (!api.compare("source")){
          std::string source = path.next();
          getSource(conn, H, source, path.next());
        }
        // Find optimal ingest point
        else if (!api.compare("ingest")){
          std::string ingest = path.next();
          getIngest(conn, H, ingest, path.next());
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
          }else{
            JSON::Value ret = getHostState(host);
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody(ret.toPrettyString());
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
          }
        }else if(!api.compare("auth")){
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
          }
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
        }
        //remove foreign host
        else if(!api.compare("host")){
          removeHost(path.next(), conn.getHost());
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody("OK");
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
                  
        }
        // Remove server from list
        else if (!api.compare("server")){
          std::string s = path.next();
          JSON::Value &nodes = cfg->getOption("server", true);
          jsonForEach(nodes, it){
            if(!s.compare((*it).asStringRef())){
              JSON::Value ret = delServer(s);
              H.Clean();
              H.SetHeader("Content-Type", "text/plain");
              H.SetBody(ret.toPrettyString());
              H.setCORSHeaders();
              H.SendResponse("200", "OK", conn);
              H.Clean();
            }
          }
          H.Clean();
          H.SetHeader("Content-Type", "text/plain");
          H.SetBody("not able to remove this server");
          H.setCORSHeaders();
          H.SendResponse("200", "not possible to remove that server", conn);
          H.Clean();
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
          }
          // del user acount
          else if (!api.compare("user")){
            userAuth.erase(path.next());
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("OK");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
          }
          // del whitelist policy
          else if (!api.compare("whitelist")){
            std::set<IpPolicy*>::iterator it = whitelist.begin();
            while(it != whitelist.end()){
              if((*it)->equals(H.body)){
                whitelist.erase(it);
                it = whitelist.begin();
              }else it++;
            }
            H.Clean();
            H.SetHeader("Content-Type", "text/plain");
            H.SetBody("OK");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
          }
        }
      }
    }
  }
  //check if this is a load balancer connection
  if(LB){
    if(!LB->Go_Down){//check if load balancer crashed
      WARN_MSG("restarting connection of load balancer: %s", LB->getName().c_str());
      std::string lbName = LB->getName();
      loadBalancers.erase(LB);
      WARN_MSG("closing LB:%p", LB);
      delete LB;
      new tthread::thread(addLB,(void*)&lbName);
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
  if(!frame.substr(0, frame.find(":")).compare("auth")){
    //send response to challenge
    std::string auth = frame.substr(frame.find(":")+1);
    std::string pass = Secure::sha256(passHash+auth);
    webSock->sendFrame(pass);

    //send own challenge
    std::string salt = generateSalt();
    webSock->sendFrame(salt);
  }
  if(!frame.substr(0, frame.find(":")).compare("salt")){
    //check responce
    std::string salt = frame.substr(frame.find(":")+1, frame.find(";")-frame.find(":")-1);
    std::map<std::string, time_t>::iterator saltIndex = activeSalts.find(salt);

    if(saltIndex == activeSalts.end()){
      webSock->sendFrame("noAuth");
      webSock->getSocket().close();
      WARN_MSG("no salt")
      return LB;
    }

    if(!Secure::sha256(passHash+salt).compare(frame.substr(frame.find(";")+1, frame.size()))){
      //auth successful
      webSock->sendFrame("OK");
      LB = new LoadBalancer(webSock, name);
      loadBalancers.insert(LB);
      INFO_MSG("Load balancer added");
    }else{
      webSock->sendFrame("noAuth");
      INFO_MSG("unautherized load balancer");
      LB = 0;
    }
  }
  //close bad auth
  if(!frame.substr(0, frame.find(":")).compare("noAuth")){
    webSock->getSocket().close();
  }
  //close authenticated load balancer
  if(!frame.compare("close")){
    LB->Go_Down = true;
    loadBalancers.erase(LB);
    webSock->getSocket().close();
  }
  if(LB && !frame.substr(0, 1).compare("{")){
    JSON::Value newVals = JSON::fromString(frame);
    if(newVals.isMember(ADDLOADBALANCER)) {
      new tthread::thread(api.addLB,(void*)&(newVals[ADDLOADBALANCER]));
    }else if(newVals.isMember(REMOVELOADBALANCER)) {
      api.removeLB(newVals[REMOVELOADBALANCER], newVals[RESEND]);
    }else if(newVals.isMember(REMOVEHOST)) {
      api.removeHost(newVals[REMOVEHOST], webSock->getSocket().getHost());
    }else if(newVals.isMember(UPDATEHOST)) {
      api.updateHost(newVals[UPDATEHOST]);
    }else if(newVals.isMember(WEIGHTS)) {
      api.setWeights(newVals[WEIGHTS]);
    }else if(newVals.isMember(ADDVIEWER)){
      //find host
      for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
        if(newVals["host"].asString().compare((*it)->details->host)){
          //call add viewer function
          std::string stream = newVals[ADDVIEWER].asString();
          (*it)->details->addViewer(stream,false);
        }
      }
    }else if(newVals.isMember(SAVEKEY)){
      saveFile();
    }else if(newVals.isMember(LOADKEY)){
      loadFile();
    }
  }
  return LB;
}

/**
   * set and get weights
   */
JSON::Value API::setWeights(delimiterParser path){
    JSON::Value ret;
    std::string newVals = path.next();
    bool changed = false;
    if (!newVals.compare("cpu")){
      weight_cpu = path.nextInt();
      newVals = path.next();
      changed = true;
    }
    if (!newVals.compare("ram")){
      weight_ram = path.nextInt();
      newVals = path.next();
      changed = true;
    }
    if (!newVals.compare("bw")){
      weight_bw = path.nextInt();
      newVals = path.next();
      changed = true;
    }
    if (!newVals.compare("geo")){
      weight_geo = path.nextInt();
      newVals = path.next();
      changed = true;
    }
    if (!newVals.compare("bonus")){
      weight_bonus = path.nextInt();
      newVals = path.next();
      changed = true;
    }

    //create json for sending
    ret["cpu"] = weight_cpu;
    ret["ram"] = weight_ram;
    ret["bw"] = weight_bw;
    ret["geo"] = weight_geo;
    ret["bonus"] = weight_bonus;

    if(changed && (!newVals.size() || atoi(newVals.c_str()) == 1)){
      JSON::Value j;
      j[WEIGHTS] = ret;
      j[RESEND] = false;
      for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
        (*it)->send(j.asString());
      }
    }
    if(changed){
      //start save timer
      time(&prevConfigChange);
      if(saveTimer == 0) saveTimer = new tthread::thread(saveTimeCheck,NULL);
    }
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
JSON::Value API::delServer(const std::string delserver){
    JSON::Value ret;
    tthread::lock_guard<tthread::mutex> globGuard(globalMutex);
    ret = "Server not monitored - could not delete from monitored server list!";
    for (std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state == STATE_OFF){continue;}
      if ((std::string)(*it)->name == delserver){
        cleanupHost(**it);
        ret = stateLookup[(*it)->state];
      }
    }
    return ret;
    
  }

/**
   * receive server updates and adds new foreign hosts if needed
   */
void API::updateHost(JSON::Value newVals){
  
    if(newVals.isMember(HOSTNAMEKEY)){
      std::string hostName = newVals[HOSTNAMEKEY].asString();
      std::set<hostEntry*>::iterator i = hosts.begin();
      while(i != hosts.end()){
        if(hostName == (*i)->name) break;
        i++;
      }
      if(i == hosts.end()){
        INFO_MSG("create new foreign host")
        initForeignHost(hostName, newVals["LB"].asString());
      }
      (*i)->details->update(newVals[FILLSTATEOUT], newVals[FILLSTREAMSOUT], newVals[SCORESOURCE].asInt(), newVals[SCORERATE].asInt(), newVals[OUTPUTSKEY], newVals[CONFSTREAMSKEY], newVals[STREAMSKEY], newVals[TAGSKEY], newVals[CPUKEY].asInt(), newVals[SERVLATIKEY].asDouble(), newVals[SERVLONGIKEY].asDouble(), newVals["binHost"].asString().c_str(), newVals["host"].asString());   
    }
  }
  
/**
   * add server to be monitored
   */
void API::addServer(JSON::Value ret, const std::string addserver){
    tthread::lock_guard<tthread::mutex> globGuard(globalMutex);
    if (addserver.size() >= HOSTNAMELEN){
      return;
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
          initHost(**it, addserver);
          newEntry = *it;
          stop = true;
          break;
        }
      }
      if (!stop){
        newEntry = new hostEntry();
        initHost(*newEntry, addserver);
        hosts.insert(newEntry);
      }
      ret[addserver] = stateLookup[newEntry->state];
    }
    return;
  }
   
/**
   * remove server from load balancer( both monitored and foreign )
   */
void API::removeHost(const std::string removeHost, std::string host){
    for(std::set<hostEntry*>::iterator i = hosts.begin(); i != hosts.end(); i++){
      if(removeHost == (*i)->name){
        for(std::set<LoadBalancer*>::iterator it = (*i)->details->LB.begin(); it != (*i)->details->LB.end(); it++){
          if(!(*it)->getName().compare(host)){
            (*i)->details->LB.erase(it);
          }
        }
        //clean up host if foreign and not being supplied
        if((*i)->details->LB.empty() && !(*i)->thread){
            cleanupHost(**i);
        }
        break;
      }
    }
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
}

/**
   * add load balancer to mesh
   */
void API::addLB(void* p){
    std::string* addLoadBalancer = (std::string*)p;
    //send to other load balancers
      JSON::Value j;
      j["addloadbalancer"] = addLoadBalancer;
      for(std::set<LoadBalancer*>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
        (*it)->send(j.asString());
      }
    

    Socket::Connection conn(addLoadBalancer->substr(0,addLoadBalancer->find(":")), atoi(addLoadBalancer->substr(addLoadBalancer->find(":")+1).c_str()), false, false);
    HTTP::URL url("ws://"+(*addLoadBalancer));
    HTTP::Websocket* ws = new HTTP::Websocket(conn, url);
    
    //send challenge
    std::string salt = generateSalt();
    ws->sendFrame("auth:" + salt);

    //check responce
    int reset = 0;
    while(!ws->readFrame()){
      reset++;
      if(reset >= 20){
        WARN_MSG("auth failed: connection timeout");
        return;
      }
      sleep(1);
    }
    std::string result(ws->data, ws->data.size());
  
    if(Secure::sha256(passHash+salt).compare(result)){
      //unautherized
      WARN_MSG("unautherised");
      ws->sendFrame("noAuth");
      return;
    }
    //send response to challenge
    reset = 0;
    while(!ws->readFrame()){
      reset++;
      if(reset >= 20){
        WARN_MSG("auth failed: connection timeout");
        return;
      }
      sleep(1);
    }
    std::string auth(ws->data, ws->data.size());
    std::string pass = Secure::sha256(passHash+auth);
    ws->sendFrame("salt:"+auth+";"+pass);

    reset = 0;
    while(!ws->readFrame()){
      reset++;
      if(reset >= 20){
        WARN_MSG("auth failed: connection timeout");
        return;
      }
      sleep(1);
    }
    std::string check(ws->data, ws->data.size());
    if(check == "OK"){
      INFO_MSG("Successful authentication of load balancer %s",addLoadBalancer->c_str());
      LoadBalancer* LB = new LoadBalancer(ws, *addLoadBalancer);
      loadBalancers.insert(LB);
      //start monitoring
      handleRequests(conn,ws,LB);
    }else if(check == "noAuth"){
      addLB(addLoadBalancer);
    }
    return;
  }

/**
  * returns load balancer list
  */
std::string API::getLoadBalancerList(){
    std::string out = "\"lblist\": [";  
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
   * return the best source of a stream
   */
void API::getSource(Socket::Connection conn, HTTP::Parser H, const std::string source, const std::string fback){
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
      if (fback.size()){
        bestHost = fback;
      }else{
        bestHost = fallback;
      }
      FAIL_MSG("No source for %s found!", source.c_str());
    }else{
      INFO_MSG("Winner: %s scores %" PRIu64, bestHost.c_str(), bestScore);
    }
    H.SetBody(bestHost);
    H.setCORSHeaders();
    H.SendResponse("200", "OK", conn);
    H.Clean();    
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
   * return server list
   */
JSON::Value API::serverList(){
    JSON::Value ret;
    for (std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state == STATE_OFF){continue;}
      ret[(std::string)(*it)->name] = stateLookup[(*it)->state];
    }
    return ret;    
  }
 
/**
   * return ingest point
   */
void API::getIngest(Socket::Connection conn, HTTP::Parser H, const std::string ingest, const std::string fback){
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
      if (fback.size()){
        bestHost = fback;
      }else{
        bestHost = fallback;
      }
      FAIL_MSG("No ingest point found!");
    }else{
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
void API::stream(Socket::Connection conn, HTTP::Parser H, std::string proto, std::string stream){
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
        return;
      }
      INFO_MSG("Balancing stream %s", stream.c_str());
      H.Clean();
      H.SetHeader("Content-Type", "text/plain");
      H.setCORSHeaders();
      hostEntry *bestHost = 0;
      uint64_t bestScore = 0;
      for (std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
        if ((*it)->state != STATE_ONLINE){continue;}
        uint64_t score = (*it)->details->rate(stream, tagAdjust);
        if (score > bestScore){
          bestHost = *it;
          bestScore = score;
        }
      }
      if (!bestScore || !bestHost){
        H.SetBody(fallback);
        FAIL_MSG("All servers seem to be out of bandwidth!");
      }else{
        INFO_MSG("Winner: %s scores %" PRIu64, bestHost->details->host.c_str(), bestScore);
        bestHost->details->addViewer(stream, true);
        H.SetBody(bestHost->details->host);
      }
      if (proto != "" && bestHost && bestScore){
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
   * add viewer to stream on server
   */
void API::addViewer(std::string stream, const std::string addViewer){
    for(std::set<hostEntry*>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if((*it)->name == addViewer){
        //next line can cause infinate loop if LB ip is 
        (*it)->details->addViewer(stream, true);
        break;
      }
    }
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
        if ((*it)->state != STATE_ONLINE){continue;}
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
      if ((*it)->state != STATE_ONLINE){continue;}
      (*it)->details->fillState(ret[(*it)->details->host]);
    }
    return ret;
  }


int main(int argc, char **argv){
  Util::redirectLogsIfNeeded();
  Util::Config conf(argv[0]);
  cfg = &conf;
  JSON::Value opt;
  opt["arg"] = "string";
  opt["short"] = "s";
  opt["long"] = "server";
  opt["help"] = "Address of a server to balance. Hostname or IP, optionally followed by API port.";
  conf.addOption("server", opt);

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
  opt["value"][0u] = "default";
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

  if(load){
    loadFile();
  }else{
    passHash = Secure::sha256(password);
  }

  JSON::Value &nodes = conf.getOption("server", true);
  conf.activate();

  api = API();
  loadBalancers = std::set<LoadBalancer*>();
  //setup saving
  saveTimer = 0;
  time(&prevSaveTime);
  //api login
  
  srand(time(0)+getpid());//setup random num generator
  userAuth.insert(std::pair<std::string, std::string>("admin","default"));
  bearerTokens.insert("test1233");
  //add localhost to whitelist
  if(conf.getBool("localmode")) {
    whitelist.insert(new IpPolicy("::1/128"));
    whitelist.insert(new IpPolicy("127.0.0.1"));
  }
  

  std::map<std::string, tthread::thread *> threads;
  jsonForEach(nodes, it){
    if (it->asStringRef().size() > 199){
      FAIL_MSG("Host length too long for monitoring, skipped: %s", it->asStringRef().c_str());
      continue;
    }
    hostEntry* newHost = new hostEntry();
    initHost(*newHost, it->asStringRef());
    hosts.insert(newHost);
  }
  WARN_MSG("Load balancer activating. Balancing between %d nodes.", nodes.size());
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