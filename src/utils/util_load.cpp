#include "util_load.h"

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


Util::Config *cfg = 0;
std::string passphrase;
std::string fallback;
bool localMode = false;
tthread::mutex globalMutex;
std::map<std::string, int32_t> blankTags;
size_t weight_cpu = 500;
size_t weight_ram = 500;
size_t weight_bw = 1000;
size_t weight_geo = 1000;
size_t weight_bonus = 50;

unsigned long hostsCounter = 0; // This is a pointer to guarantee atomic accesses.
#define HOSTLOOP                                                                             \
  unsigned long i = 0;                                                                             \
  i < hostsCounter;                                                                                \
  ++i
#define HOST(no) (hosts[no])
#define HOSTCHECK                                                                                  \
  if (hosts[i].state != STATE_ONLINE){continue;}


API api;
hostEntry hosts[MAXHOSTS]; /// Fixed-size array holding all hosts
std::set<LoadBalancer> loadBalancers;


std::string Data::stringify() {
  return std::string();
}

std::string streamDetails::stringify(){
  std::ostringstream out;
  out << "\"streamDetails\": [\"total\": " << total <<  ", \"inputs\": " << inputs << ", \"bandwidth\": " << bandwidth << ", \"prevTotal\": " << prevTotal << ", \"bytesUp\": " << bytesUp << ", \"bytesDown\": " << bytesDown << "]";
  return out.str();
}
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

  LoadBalancer::LoadBalancer(std::string ip) {
    this->ip = ip;
    LoadMutex = 0;
    HTTP::URL url(ip);
    std::map<std::string, std::string>* headers = new std::map<std::string, std::string>();
    Socket::Connection conn;
    ws = new HTTP::Websocket(conn, url, headers);
    
  }
  LoadBalancer::LoadBalancer(Socket::Connection conn, HTTP::Parser &h){
    if(!LoadMutex){LoadMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*LoadMutex);
    ws = new HTTP::Websocket(conn, h);
    
  }
  LoadBalancer::~LoadBalancer(){
    if(LoadMutex){
      delete LoadMutex;
      LoadMutex = 0;
    }
    delete ws;
  }
  std::string LoadBalancer::receive(){
    if(!LoadMutex){LoadMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*LoadMutex);
    std::string ret;
    if(ws->readLoop()){
      ret = ws->data;
    }
    //TODO implement: Dencryption::AES();
    return ret;
  }

  std::string LoadBalancer::getName() const {return ip;}
  bool LoadBalancer::operator < (const LoadBalancer &other) const {return this->getName() < other.getName();}
  bool LoadBalancer::operator > (const LoadBalancer &other) const {return this->getName() > other.getName();}
  bool LoadBalancer::operator == (const LoadBalancer &other) const {return this->getName() == other.getName();}
  bool LoadBalancer::operator == (const std::string &other) const {return this->getName().compare(other);}
  
  void LoadBalancer::send(JSON::Value j){
    if(!LoadMutex){LoadMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*LoadMutex);
    //TODO implement: Encryption::AES();
    ws->sendFrame(j.asString());
  }
  
  void LoadBalancer::monitor(){
    JSON::Value newVals = JSON::fromString(receive());
    if(newVals.isMember("addLoadBalancer")) {
      api.addLB(newVals["aadLoadBalancer"], newVals["resend"]);
    }else if(newVals.isMember("removeLoadBalancer")) {
      api.removeLB(newVals["removeLoadBalancer"], newVals["resend"]);
    }else if(newVals.isMember("removeHost")) {
      api.removeHost(newVals["removeHost"]);
    }else if(newVals.isMember("addserver")) {
      JSON::Value ret;
      api.addServer(ret, newVals["addserver"]);
    }else if(newVals.isMember("updateHost")) {
      api.updateHost(newVals["updateHost"]);
    }else if(newVals.isMember("delServer")) {
      api.delServer(newVals["delServer"]);
    }else if(newVals.isMember("weights")) {
      api.setWeights(newVals["weights"], newVals["resend"]);
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

std::string outUrl::stringify(){
  return "\"outUrl\": [\"pre\": " + pre + ", \"post\": " + post + "]";
}

outUrl* outUrl::destringify(JSON::Value j){
  outUrl* r;
  r->pre = j["pre"].asString();
  r->post = j["post"].asString();
  return r;
}


void convertSetToJson(JSON::Value j, std::set<std::string> s, std::string key){
  std::string out = "\"" + key + "\": [";  
  for(std::set<std::string>::iterator it = s.begin(); it != s.end(); ++it){
      if(it != s.begin()){
        out += ", ";
      }
      out += "\"" + *it + "\"";
  }
  out += "]";
  j[key] = out;
}

void convertMapToJson(JSON::Value j, std::map<std::string, Data> s, std::string key){
  std::string out = "\"" + key + "\": [";  
  for(std::map<std::string, Data>::iterator it = s.begin(); it != s.end(); ++it){
      if(it != s.begin()){
        out += ", ";
      }
      out += "\"" + (*it).first + "\": " + (*it).second.stringify();
  }
  out += "]";
  j[key] = out;
}

std::set<std::string> convertJsonToSet(JSON::Value j){
  std::set<std::string> s;
  jsonForEach(j,i){
    s.insert(i->asString());
  }
  return s;
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


  hostDetails::hostDetails(std::set<LoadBalancer> LB){
    this->LB = LB;
    hostMutexf = 0;
  }
  hostDetails::~hostDetails(){
    if(hostMutexf){
      delete hostMutexf;
      hostMutexf = 0;
    }
  }
  /**
   *  Fills out a by reference given JSON::Value with current state.
   */
  void hostDetails::fillState(JSON::Value &r){
    r = fillStateOut;
  }
  /** 
   * Fills out a by reference given JSON::Value with current streams viewer count.
   */
  void hostDetails::fillStreams(JSON::Value &r){
    r = fillStreamsOut;
  }
  /**
   * Fills out a by reference given JSON::Value with current stream statistics.
   */ 
  void hostDetails::fillStreamStats(const std::string &s,JSON::Value &r) {
    if(!hostMutexf){hostMutexf = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutexf);
    for (std::map<std::string, streamDetails>::iterator jt = streams.begin(); jt != streams.end(); ++jt){
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
  long long hostDetails::getViewers(const std::string &strm){
    if(!hostMutexf){hostMutexf = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutexf);
    if(!streams.count(strm)){return 0;}
    return streams[strm].total;
  }
  uint64_t hostDetails::rate(std::string &s, const std::map<std::string, int32_t>  &tagAdjust = blankTags, double lati = 0, double longi = 0){
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
  uint64_t hostDetails::source(const std::string &s, const std::map<std::string, int32_t> &tagAdjust, uint32_t minCpu, double lati = 0, double longi = 0){
    if (!hostMutexf){hostMutexf = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutexf);
    if(s.size() && (!streams.count(s) || !streams[s].inputs)){return 0;}

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
  std::string hostDetails::getUrl(std::string &s, std::string &proto){
    if(!hostMutexf) {hostMutexf = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> gaurd(*hostMutexf);
    if(!outputs.count(proto)){return "";}
    const outUrl o = outputs[proto];
    return o.pre + s + o.post;
  }
  /**
   * Sends update to original load balancer to add a viewer.
   */
  void hostDetails::addViewer(std::string &s){
    //TODO
  }
  /**
   * Update precalculated host vars.
   */
  void hostDetails::update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource, uint64_t scoreRate, std::map<std::string, outUrl> outputs, std::set<std::string> conf_streams, std::map<std::string, streamDetails> streams, std::set<std::string> tags, uint64_t cpu, double servLati, double servLongi, const char* binHost, std::string host){
    if(!hostMutexf){hostMutexf = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutexf);

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
   * allow for json inputs instead of sets and maps for update function
   */
  void hostDetails::update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource, uint64_t scoreRate, JSON::Value outputs, JSON::Value conf_streams, JSON::Value streams, JSON::Value tags, uint64_t cpu, double servLati, double servLongi, const char* binHost, std::string host){  
    //TODO convert maps and sets
    std::map<std::string, outUrl> out;
    std::map<std::string, streamDetails> s;
    streamDetails x;
    for(int i = 0; i < streams.size(); i++){
      s.insert(std::pair<std::string, streamDetails>(streams[STREAMSKEY][i]["key"], *(x.destringify(streams[STREAMSKEY][i]["streamDetails"]))));
    }
    update(fillStateOut, fillStreamsOut, scoreSource, scoreRate, out, convertJsonToSet(conf_streams), s, convertJsonToSet(tags), cpu, servLati, servLongi, binHost, host);
  }
  


  hostDetailsCalc::hostDetailsCalc() : hostDetails(std::set<LoadBalancer>()){
    hostMutex = 0;
    cpu = 1000;
    ramMax = 0;
    ramCurr = 0;
    upSpeed = 0;
    downSpeed = 0;
    upPrev = 0;
    downPrev = 0;
    prevTime = 0;
    total = 0;
    addBandwidth = 0;
    servLati = 0;
    servLongi = 0;
    availBandwidth = 128 * 1024 * 1024; // assume 1G connections
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
  void hostDetailsCalc::fillState(JSON::Value &r){
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
        r["tags"].append(*it);
      }
    }
    if (ramMax && availBandwidth){
      r["score"]["cpu"] = (uint64_t)(weight_cpu - (cpu * weight_cpu) / 1000);
      r["score"]["ram"] = (uint64_t)(weight_ram - ((ramCurr * weight_ram) / ramMax));
      r["score"]["bw"] = (uint64_t)(weight_bw - (((upSpeed + addBandwidth) * weight_bw) / availBandwidth));
    }
  }
  /// Fills out a by reference given JSON::Value with current streams viewer count.
  void hostDetailsCalc::fillStreams(JSON::Value &r){
    if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
    tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);

    for (std::map<std::string, streamDetails>::iterator jt = streams.begin();
         jt != streams.end(); ++jt){
      r[jt->first] = r[jt->first].asInt() + jt->second.total;
    }
  }
  /// Scores a potential new connection to this server
  /// 0 means not possible, the higher the better.
  uint64_t hostDetailsCalc::rate(){
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
  uint64_t hostDetailsCalc::source(){
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
    
    //update the local precalculated vars
    ((hostDetails*)(this))->update(fillStateOut, fillStreamsOut, scoreSource, scoreRate, outputs, conf_streams, streams, tags, cpu, servLati, servLongi, binHost, host);
    //TODO: test send to other load balancers
    //create json to send to other load balancers
    JSON::Value j;
    j["fillStaterOut"] = fillStateOut;
    j["fillStreamsOut"] = fillStreamsOut;
    j["scoreSource"] = scoreSource;
    j["scoreRate"] = scoreRate;
    
    //streamDetails &strm = *(streamDetails*)&(streams[it.key()]);
    convertMapToJson(j, *(std::map<std::string,Data>*)&outputs, OUTPUTSKEY);
    convertSetToJson(j, conf_streams, CONFSTREAMSKEY);
    convertMapToJson(j, *(std::map<std::string,Data>*)&streams, STREAMSKEY);
    convertSetToJson(j, tags, TAGSKEY);
    
    j["cpu"] = cpu;
    j["servLati"] = servLati;
    j["servLongi"] = servLongi;

    //TODO: what if load balancer crashed
    //TODO send to other load balancers
    for(std::set<LoadBalancer>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
      HTTP::Parser H;
      HTTP::URL url((*it).getName()+ "/updateHost");
      HTTP::Downloader DL;
    
      /*DL.post(url,j);
      if(!DL.isOK()){
        loadBalancer.remove(lb);
      }
      */
      DL.getSocket().close();
    }
  }
  
  /**
   * add the viewer to this host
   * updates all precalculated host vars
   */
  void hostDetailsCalc::addViewer(std::string &s){
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
    if (d.isMember("tags") && d["tags"].isArray()){
      std::set<std::string> newTags;
      jsonForEach(d["tags"], tag){
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

    if (d.isMember("streams") && d["streams"].size()){
      jsonForEach(d["streams"], it){
        uint64_t count = (*it)["curr"][0u].asInt() + (*it)["curr"][1u].asInt() + (*it)["curr"][2u].asInt();
        if (!count){
          if (streams.count(it.key())){streams.erase(it.key());}
          continue;
        }
        streamDetails strm = streams[it.key()];

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
          if (!d["streams"].isMember(it->first)){eraseList.insert(it->first);}
        }
        for (std::set<std::string>::iterator it = eraseList.begin(); it != eraseList.end(); ++it){
          streams.erase(*it);
        }
      }
    }else{
      streams.clear();
    }
    conf_streams.clear();
    if (d.isMember("conf_streams") && d["conf_streams"].size()){
      jsonForEach(d["conf_streams"], it){conf_streams.insert(it->asStringRef());}
    }
    outputs.clear();
    if (d.isMember("outputs") && d["outputs"].size()){
      jsonForEach(d["outputs"], op){outputs[op.key()] = outUrl(op->asStringRef(), host);}
    }
    addBandwidth *= 0.75;
    calc();//update preclaculated host vars
  }
  

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
  ((hostDetailsCalc*)&(entry->details))->availBandwidth = bandwidth.asInt();
  ((hostDetailsCalc*)&(entry->details))->host = url.host;
  entry->state = STATE_BOOT;
  bool down = true;

  HTTP::Downloader DL;

  while (cfg->is_active && (entry->state != STATE_GODOWN)){
    if (DL.get(url) && DL.isOk()){
      JSON::Value servData = JSON::fromString(DL.data());
      if (!servData){
        FAIL_MSG("Can't decode server %s load information", url.host.c_str());
        ((hostDetailsCalc*)&(entry->details))->badNess();
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
        ((hostDetailsCalc*)&(entry->details))->update(servData);
      }
    }else{
      FAIL_MSG("Can't retrieve server %s load information", url.host.c_str());
      ((hostDetailsCalc*)&(entry->details))->badNess();
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

void initHost(hostEntry &H, const std::string &N){
  // Cancel if this host has no name set
  if (!N.size()){return;}
  H.state = STATE_BOOT;
  H.details = new hostDetailsCalc();
  //(*(hostDetailsCalc*)&H.details).calc();
  memset(H.name, 0, HOSTNAMELEN);
  memcpy(H.name, N.data(), N.size());
  H.thread = new tthread::thread(handleServer, (void *)&H);
  INFO_MSG("Starting monitoring %s", H.name);
}

/**
 * Setup foreign host
 */
void initForeignHost(hostEntry &H, const std::string &N, const std::set<std::string> &LB){
  // Cancel if this host has no name or load balancer set
  if (!N.size()){return;}
  H.state = STATE_ONLINE;
  std::set<LoadBalancer> LBList;
  //add LB to LBList if in mesh only
  for(std::set<std::string>::iterator it = LB.begin(); it != LB.end(); ++it){
    std::set<LoadBalancer>::iterator i = loadBalancers.begin();
    while(i != loadBalancers.end()){
      if((*i).getName() == (*it)){//check if LB is  mesh
        LBList.insert(LoadBalancer((*it)));
        break;
      }else if((*i).getName() > (*it)){//check if past LB in search
        break;
      }else {//go to next in mesh list
        i++;
      }
    }
    
  }
  H.details = new hostDetails(LBList);
  memset(H.name, 0, HOSTNAMELEN);
  memcpy(H.name, N.data(), N.size());
  H.thread = 0;
  INFO_MSG("Created foreign server %s", H.name);
}

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

/// Fills the given map with the given JSON string of tag adjustments
void fillTagAdjust(std::map<std::string, int32_t> & tags, const std::string & adjust){
  JSON::Value adj = JSON::fromString(adjust);
  jsonForEach(adj, t){
    tags[t.key()] = t->asInt();
  }
}


/**
 * function to select the api function wanted
 */
  int API::handleRequest(Socket::Connection &conn){
  HTTP::Parser H;
  while (conn){
    if ((conn.spool() || conn.Received().size()) && H.Read(conn)){
      // Special commands
      if (H.url.size() == 1){
        if (localMode && !conn.isLocal()){
          H.SetBody("Configuration only accessible from local interfaces");
          H.setCORSHeaders();
          H.SendResponse("403", "Forbidden", conn);
          H.Clean();
          continue;
        }
        std::string host = H.GetVar("host");
        std::string viewers = H.GetVar("viewers");
        std::string streamStats = H.GetVar("streamstats");
        std::string stream = H.GetVar("stream");
        std::string source = H.GetVar("source");
        std::string ingest = H.GetVar("ingest");
        std::string fback = H.GetVar("fallback");
        std::string lstserver = H.GetVar("lstserver");
        std::string addserver = H.GetVar("addserver");
        std::string delserver = H.GetVar("delserver");
        std::string weights = H.GetVar("weights");
        std::string hostUpdate = H.GetVar("updateHosts");
        std::string hostToRemove = H.GetVar("removeHost");
        std::string viewer = H.GetVar("addViewer");
        std::string addLoadBalancer = H.GetVar("addloadbalancer");
        std::string removeLoadBalancer = H.GetVar("removeloadbalancer");
        std::string resend = H.GetVar("resend");
        std::string getLBList = H.GetVar("LBList");
        H.Clean();
        H.SetHeader("Content-Type", "text/plain");
        
        //return load balancer list
        if(getLBList.size()){
          std::string out = getLoadBalancerList();
          H.SetBody(out);
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
        }
        //add load balancer to mesh
        else if(addLoadBalancer.size()){
          addLB(addLoadBalancer, resend);
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          
        }
        //remove load balancer from mesh
        else if(removeLoadBalancer.size()){
          removeLB(removeLoadBalancer, resend);
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          
        }
        //add viewer to host
        else if(viewer.size()){
          addViewer(stream, viewer);
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          
        }
        //remove foreign host
        else if(hostToRemove.size()){
          removeHost(hostToRemove);
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
                  
        }
        //receive host data
        else if(hostUpdate.size()){
          updateHost(JSON::fromString(hostUpdate));
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
        }
        // Get/set weights
        else if (weights.size()){
          JSON::Value ret = setWeights(weights, resend);
          H.SetBody(ret.toString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
        }
        // Get server list
        else if (lstserver.size()){
          JSON::Value ret = serverList();
          H.SetBody(ret.toPrettyString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
        }
        // Remove server from list
        else if (delserver.size()){
          JSON::Value ret = delServer(delserver);
          H.SetBody(ret.toPrettyString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
        }
        // Add server to list
        else if (addserver.size()){
          JSON::Value* ret;
          addServer(ret, addserver);
          if(ret == NULL){
            H.SetBody("Host length too long for monitoring");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
          }else {
            H.SetBody(ret->toPrettyString());
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
          }
        }
        // Request viewer count
        else if (viewers.size()){
          JSON::Value ret = getViewers();
          H.SetBody(ret.toPrettyString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
        }
        // Request full stream statistics
        else if (streamStats.size()){
          JSON::Value ret = (streamStats);
          H.SetBody(ret.toPrettyString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();   
        }
        else if (stream.size()){
          uint64_t count = getStream(stream);
          H.SetBody(JSON::Value(count).asString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
        }
        // Find source for given stream
        else if (source.size()){
          getSource(conn, H, source, fback);
          
        }
        // Find optimal ingest point
        else if (ingest.size()){
          getIngest(conn, H, ingest, fback);
        }
        // Find host(s) status
        else if (!host.size()){
          JSON::Value ret = getAllHostStates();
          H.SetBody(ret.toPrettyString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
        }else{
          JSON::Value ret = getHostState(host);
          H.SetBody(ret.toPrettyString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
        }
      }
      else {
        stream(conn, H);  
      }
    }
  }
  conn.close();
  return 0;
}

  /**
   * set and get weights
   */
  JSON::Value API::setWeights(const std::string weights, const std::string resend){
    JSON::Value ret;
    JSON::Value newVals = JSON::fromString(weights);
    if (newVals.isMember("cpu")){weight_cpu = newVals["cpu"].asInt();}
    if (newVals.isMember("ram")){weight_ram = newVals["ram"].asInt();}
    if (newVals.isMember("bw")){weight_bw = newVals["bw"].asInt();}
    if (newVals.isMember("geo")){weight_geo = newVals["geo"].asInt();}
    if (newVals.isMember("bonus")){weight_bonus = newVals["bonus"].asInt();}
    ret["cpu"] = weight_cpu;
    ret["ram"] = weight_ram;
    ret["bw"] = weight_bw;
    ret["geo"] = weight_geo;
    ret["bonus"] = weight_bonus;

    
    if(!resend.size() || atoi(resend.c_str()) == 1){
      for(std::set<LoadBalancer>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
        //TODO call change config on lb
      }
    }
    return ret;
  }
  
  /**
   * remove server from ?
   */
  JSON::Value API::delServer(const std::string delserver){
    JSON::Value ret;
    tthread::lock_guard<tthread::mutex> globGuard(globalMutex);
    ret = "Server not monitored - could not delete from monitored server list!";
    for (HOSTLOOP){
      if (hosts[i].state == STATE_OFF){continue;}
      if ((std::string)hosts[i].name == delserver){
        cleanupHost(hosts[i]);
        ret = stateLookup[hosts[i].state];
      }
    }
    return ret;
    
  }

  /**
   * receive server updates and adds new foreign hosts if needed
   */
  void API::updateHost(JSON::Value newVals){
    if(newVals.isMember("hostName")){
      std::string hostName = newVals["hostName"].asString();
      int hostIndex = -1;
      for(HOSTLOOP){
        if(hostName == hosts[i].name){hostIndex = i;}
      }
      if(hostIndex == -1){

        initForeignHost(HOST(hostsCounter), hostName, convertJsonToSet(newVals["LB"]));
        hostIndex = hostsCounter;
        ++hostsCounter;
      }
      hosts[hostIndex].details->update(newVals["fillStateOut"], newVals["fillStreamsOut"], newVals["scoreSource"].asInt(), newVals["scoreRate"].asInt(), newVals["outputs"], newVals["conf_streams"], newVals["streams"], newVals["tags"], newVals["cpu"].asInt(), newVals["servLati"].asDouble(), newVals["servLongi"].asDouble(), newVals["binHost"].asString().c_str(), newVals["host"].asString());   
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
    for (HOSTLOOP){
      if (hosts[i].state == STATE_OFF){continue;}
      if ((std::string)hosts[i].name == addserver){
        stop = true;
        break;
      }
    }
    if (stop){
      ret = "Server already monitored - add request ignored";
    }else{
      for (HOSTLOOP){
        if (hosts[i].state == STATE_OFF){
          initHost(hosts[i], addserver);
          newEntry = &(hosts[i]);
          stop = true;
          break;
        }
      }
      if (!stop){
        initHost(HOST(hostsCounter), addserver);
        newEntry = &HOST(hostsCounter);
        ++hostsCounter; // up the hosts counter
      }
      ret[addserver] = stateLookup[newEntry->state];
    }
    return;
  }
   
  /**
   * remove server from load balancer( both monitored and foreign )
   */
  void API::removeHost(const std::string removeHost){
    for(HOSTLOOP){
      if(removeHost == hosts[i].name){
        if(!hosts[i].thread){
          cleanupHost(hosts[i]);
        }
        break;
      }
    }
  }
   
  /**
   * remove load balancer from mesh
   */
  void API::removeLB(std::string removeLoadBalancer, const std::string resend){
    if(false){//TODO remove all LB  if this ip
      
      return;
    }else loadBalancers.erase(removeLoadBalancer);
      
    
    if(!resend.size() || atoi(resend.c_str()) ==1){
      for(int i = 0; i < loadBalancers.size(); i++){
        //TODO this api call on lb
      }
    }
  }
  
  /**
   * add load balancer to mesh
   */
  void API::addLB(std::string addLoadBalancer, const std::string resend){
    //check if load balancer is already in list
    
    if(!loadBalancers.count(addLoadBalancer)){
      if(!resend.size() || atoi(resend.c_str()) == 1){
        //TODO send LBList to ip in addLoadBalancer
      }
      return;
    }
    
    if(!resend.size() || atoi(resend.c_str()) == 1){
      for(std::set<LoadBalancer>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
        //TODO send to other load balancers
      }
      //TODO send LBList to ip in addLoadBalancer
    }
    loadBalancers.insert(addLoadBalancer);          
  }

  /**
  * returns load balancer list
  */
  std::string API::getLoadBalancerList(){
    std::string out = "\"lblist\": [";  
    for(std::set<LoadBalancer>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
      if(it != loadBalancers.begin()){
        out += ", ";
      }
      out += "\"" + (*it).getName() + "\"";
    }
    out += "]";
    return out;
  }
  
  /**
   * return viewer counts of streams
   */
  JSON::Value API::getViewers(){
    JSON::Value ret;
    for (HOSTLOOP){
      if (hosts[i].state == STATE_OFF){continue;}
        HOST(i).details->fillStreams(ret);
    }
    return ret;
  }
  
  /**
   * return the best source of a stream
   */
  void API::getSource(Socket::Connection conn, HTTP::Parser H, const std::string source, const std::string fback){
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
    
    for (HOSTLOOP){
      HOSTCHECK;
      //WARN_MSG("Cpu %" PRIu64 " i: %lu binhost %s",hosts[i].details->cpu, i, hosts[i].details->binHost.c_str());
      if (Socket::matchIPv6Addr(std::string(HOST(i).details->binHost, 16), conn.getBinHost(), 0)){
        INFO_MSG("Ignoring same-host entry %s", HOST(i).details->host.data());
        continue;
      }
      uint64_t score = HOST(i).details->source(source, tagAdjust, 0, lat, lon);
      if (score > bestScore){
        bestHost = "dtsc://" + HOST(i).details->host;
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
    for (HOSTLOOP){
      if (hosts[i].state == STATE_OFF){continue;}
      count += HOST(i).details->getViewers(stream);
    }
    return count;   
  }
  
  /**
   * return server list
   */
  JSON::Value API::serverList(){
    JSON::Value ret;
    for (HOSTLOOP){
      if (hosts[i].state == STATE_OFF){continue;}
      ret[(std::string)hosts[i].name] = stateLookup[hosts[i].state];
    }
    return ret;    
  }
 
  /**
   * return ingest point
   */
  void API::getIngest(Socket::Connection conn, HTTP::Parser H, const std::string ingest, const std::string fback){
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
    for (HOSTLOOP){
      HOSTCHECK;
      uint64_t score = HOST(i).details->source("", tagAdjust, cpuUse * 10, lat, lon);
      if (score > bestScore){
        bestHost = HOST(i).details->host;
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
  void API::stream(Socket::Connection conn, HTTP::Parser H){
    // Balance given stream
      std::string stream = HTTP::URL(H.url).path;
      std::string proto = H.GetVar("proto");
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
      for (HOSTLOOP){
        HOSTCHECK;
        uint64_t score = HOST(i).details->rate(stream, tagAdjust);
        if (score > bestScore){
          bestHost = &HOST(i);
          bestScore = score;
        }
      }
      if (!bestScore || !bestHost){
        H.SetBody(fallback);
        FAIL_MSG("All servers seem to be out of bandwidth!");
      }else{
        INFO_MSG("Winner: %s scores %" PRIu64, bestHost->details->host.c_str(), bestScore);
        bestHost->details->addViewer(stream);
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
    for (HOSTLOOP){
      if (hosts[i].state == STATE_OFF){continue;}
      HOST(i).details->fillStreamStats(streamStats, ret);
    }
    return ret;
  }
 
  /**
   * add viewer to stream on server
   */
  void API::addViewer(std::string stream, const std::string addViewer){
    for(HOSTLOOP){
      if(hosts[i].name == addViewer){
        //next line can cause infinate loop if LB ip is 
        hosts[i].details->addViewer(stream);
        break;
      }
    }
  }

  /**
   * return server data of a server
   */
  JSON::Value API::getHostState(const std::string host){
    JSON::Value ret;
    for (HOSTLOOP){
      if (hosts[i].state == STATE_OFF){continue;}
      if (HOST(i).details->host == host){
        ret = stateLookup[hosts[i].state];
        HOSTCHECK;
        HOST(i).details->fillState(ret);
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
    for (HOSTLOOP){
      if (hosts[i].state == STATE_OFF){continue;}
      ret[HOST(i).details->host] = stateLookup[hosts[i].state];
      HOSTCHECK;
      HOST(i).details->fillState(ret[HOST(i).details->host]);
    }
    return ret;
  }


int main(int argc, char **argv){
  Util::redirectLogsIfNeeded();
  memset(hosts, 0, sizeof(hostEntry)*MAXHOSTS); // zero-fill the hosts list
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

  conf.parseArgs(argc, argv);

  passphrase = conf.getOption("passphrase").asStringRef();
  weight_ram = conf.getInteger("ram");
  weight_cpu = conf.getInteger("cpu");
  weight_bw = conf.getInteger("bw");
  weight_geo = conf.getInteger("geo");
  weight_bonus = conf.getInteger("extra");
  fallback = conf.getString("fallback");
  localMode = conf.getBool("localmode");
  INFO_MSG("Local control only mode is %s", localMode ? "on" : "off");

  JSON::Value &nodes = conf.getOption("server", true);
  conf.activate();

  api = API();
  loadBalancers = std::set<LoadBalancer>();

  std::map<std::string, tthread::thread *> threads;
  jsonForEach(nodes, it){
    if (it->asStringRef().size() > 199){
      FAIL_MSG("Host length too long for monitoring, skipped: %s", it->asStringRef().c_str());
      continue;
    }
    initHost(HOST(hostsCounter), it->asStringRef());
    ++hostsCounter; // up the hosts counter
  }
  WARN_MSG("Load balancer activating. Balancing between %lu nodes.", hostsCounter);

  conf.serveThreadedSocket(api.handleRequest);
  if (!conf.is_active){
    WARN_MSG("Load balancer shutting down; received shutdown signal");
  }else{
    WARN_MSG("Load balancer shutting down; socket problem");
  }
  conf.is_active = false;

  // Join all threads
  for (HOSTLOOP){
    if (!HOST(i).name[0]){continue;}
    HOST(i).state = STATE_GODOWN;
  }
  for (HOSTLOOP){cleanupHost(HOST(i));}
}