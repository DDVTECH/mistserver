#include "server.h"
#include <cmath>

const char *stateLookup[] ={
    "Offline",         "Starting monitoring", "Monitored (error)",     "Monitored (not in service)",
    "Requesting stop", "Requesting clean",    "Monitored (in service)"};

tthread::mutex globalMutex;

// server balancing weights
size_t weight_cpu = 500;
size_t weight_ram = 500;
size_t weight_bw = 1000;
size_t weight_geo = 1000;
size_t weight_bonus = 50;

Util::Config *cfg = 0;

std::map<std::string, int32_t> blankTags;

std::set<LoadBalancer *> loadBalancers; // array holding all load balancers in the mesh
std::string identifier;
std::set<std::string> identifiers;

std::set<hostEntry *> hosts; // array holding all hosts

/**
 * \returns this object as a string
 */
JSON::Value streamDetails::stringify() const{
  JSON::Value out;
  out[streamDetailsTotal] = total;
  out[streamDetailsInputs] = inputs;
  out[streamDetailsBandwidth] = bandwidth;
  out[streamDetailsPrevTotal] = prevTotal;
  out[streamDetailsBytesUp] = bytesUp;
  out[streamDetailsBytesDown] = bytesDown;
  return out;
}
/**
 * \returns \param j as a streamDetails object
 */
streamDetails *streamDetails::destringify(JSON::Value j){
  streamDetails *out = new streamDetails();
  out->total = j[streamDetailsTotal].asInt();
  out->inputs = j[streamDetailsInputs].asInt();
  out->bandwidth = j[streamDetailsBandwidth].asInt();
  out->prevTotal = j[streamDetailsPrevTotal].asInt();
  out->bytesUp = j[streamDetailsBytesUp].asInt();
  out->bytesDown = j[streamDetailsBytesDown].asInt();
  return out;
}

outUrl::outUrl(){};
outUrl::outUrl(const std::string &u, const std::string &host){
  std::string tmp = u;
  if (u.find("host") != std::string::npos){
    tmp = u.substr(0, u.find("host")) + host + u.substr(u.find("host") + 4);
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
  j[outUrlPre] = pre;
  j[outUrlPost] = post;
  return j;
}
/**
 * turn json \param j into outUrl
 */
outUrl outUrl::destringify(JSON::Value j){
  outUrl r;
  r.pre = j[outUrlPre].asString();
  r.post = j[outUrlPost].asString();
  return r;
}

/**
 * construct an object to represent an other load balancer
 */
LoadBalancer::LoadBalancer(HTTP::Websocket *ws, std::string name, std::string ident)
    : LoadMutex(0), ws(ws), name(name), ident(ident), state(true), Go_Down(false){}
LoadBalancer::~LoadBalancer(){
  if (LoadMutex){
    delete LoadMutex;
    LoadMutex = 0;
  }
  // ws->getSocket().close();
  delete ws;
  ws = 0;
}
/**
 * \return the address of this load balancer
 */
std::string LoadBalancer::getName() const{
  return name;
}
std::string LoadBalancer::getIdent() const{
  return ident;
}
/**
 * allows for ordering of load balancers
 */
bool LoadBalancer::operator<(const LoadBalancer &other) const{
  return this->getName() < other.getName();
}
/**
 * allows for ordering of load balancers
 */
bool LoadBalancer::operator>(const LoadBalancer &other) const{
  return this->getName() > other.getName();
}
/**
 * \returns true if \param other is equivelent to this load balancer
 */
bool LoadBalancer::operator==(const LoadBalancer &other) const{
  return this->getName().compare(other.getName());
}
/**
 * \returns true only if \param other is the ip of this load balancer
 */
bool LoadBalancer::operator==(const std::string &other) const{
  return this->getName().compare(other);
}
/**
 * send \param ret to the load balancer represented by this object
 */
void LoadBalancer::send(std::string ret) const{
  if (!Go_Down && state){// prevent sending when shuting down
    ws->sendFrame(ret);
  }
}

int32_t applyAdjustment(const std::set<std::string> &tags, const std::string &match, int32_t adj){
  if (!match.size()){return 0;}
  bool invert = false;
  bool haveOne = false;
  size_t prevPos = 0;
  if (match[0] == '-'){
    invert = true;
    prevPos = 1;
  }
  // Check if any matches inside tags
  size_t currPos = match.find(',', prevPos);
  while (currPos != std::string::npos){
    if (tags.count(match.substr(prevPos, currPos - prevPos))){haveOne = true;}
    prevPos = currPos + 1;
    currPos = match.find(',', prevPos);
  }
  if (tags.count(match.substr(prevPos))){haveOne = true;}
  // If we have any match, apply adj, unless we're doing an inverted search, then return adj on zero matches
  if (haveOne == !invert){return adj;}
  return 0;
}

/**
 * construct server data object of server monitored in mesh
 * \param LB contains the load balancer that created this object null if this load balancer
 * \param name is the name of the server
 */
hostDetails::hostDetails(char *name)
    : hostMutex(0), name(name), ramCurr(0), ramMax(0), availBandwidth(128 * 1024 * 1024),
      addBandwidth(0), balanceCPU(0), balanceRAM(0), balanceBW(0), balanceRedirect(""){}
/**
 * destructor
 */
hostDetails::~hostDetails(){
  if (hostMutex){
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
  if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
  tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
  for (std::map<std::string, streamDetails>::const_iterator jt = streams.begin(); jt != streams.end(); ++jt){
    const std::string &n = jt->first;
    if (s != "*" && n != s && n.substr(0, s.size() + 1) != s + "+"){continue;}
    if (!r.isMember(n)){
      r[n].append(jt->second.total);
      r[n].append(jt->second.bandwidth);
      r[n].append(jt->second.bytesUp);
      r[n].append(jt->second.bytesDown);
    }else{
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
  if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
  tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
  if (!streams.count(strm)){return 0;}
  return streams.at(strm).total;
}
/**
 * Scores a potential new connection to this server
 * 0 means not possible, the higher the better.
 */
uint64_t hostDetails::rate(std::string &s, const std::map<std::string, int32_t> &tagAdjust = blankTags,
                           double lati, double longi) const{
  if (conf_streams.size() && !conf_streams.count(s) &&
      !conf_streams.count(s.substr(0, s.find_first_of("+ ")))){
    MEDIUM_MSG("Stream %s not available from %s", s.c_str(), host.c_str());
    return 0;
  }
  uint64_t score = scoreRate + (streams.count(s) ? weight_bonus : 0);
  uint64_t geo_score = 0;
  if (servLati && servLongi && lati && longi){
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
uint64_t hostDetails::source(const std::string &s, const std::map<std::string, int32_t> &tagAdjust,
                             uint32_t minCpu, double lati = 0, double longi = 0) const{
  if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
  tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
  if (s.size() && (!streams.count(s) || !streams.at(s).inputs)){return 0;}

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
  if (!outputs.count(proto)){return "";}
  const outUrl o = outputs.at(proto);
  return o.pre + s + o.post;
}
/**
 * add the viewer to this host
 * updates all precalculated host vars
 */
void hostDetails::addViewer(std::string &s, bool resend){
  if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
  tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
  uint64_t toAdd = toAddB;
  if (streams.count(s)){toAdd = streams[s].bandwidth;}
  // ensure reasonable limits of bandwidth guesses
  if (toAdd < 64 * 1024){toAdd = 64 * 1024;}// minimum of 0.5 mbps
  if (toAdd > 1024 * 1024){toAdd = 1024 * 1024;}// maximum of 8 mbps
  addBandwidth += toAdd;
}
/**
 * Update precalculated host vars.
 */
void hostDetails::update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource,
                         uint64_t scoreRate, std::map<std::string, outUrl> outputs,
                         std::set<std::string> conf_streams, std::map<std::string, streamDetails> streams,
                         std::set<std::string> tags, uint64_t cpu, double servLati, double servLongi,
                         const char *binHost, std::string host, uint64_t toAdd, uint64_t currBandwidth,
                         uint64_t availBandwidth, uint64_t currRam, uint64_t ramMax){
  if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
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
void hostDetails::update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource,
                         uint64_t scoreRate, uint64_t toAdd){
  if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
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
void hostDetails::update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource,
                         uint64_t scoreRate, JSON::Value outputs, JSON::Value conf_streams,
                         JSON::Value streams, JSON::Value tags, uint64_t cpu, double servLati,
                         double servLongi, const char *binHost, std::string host, uint64_t toAdd,
                         uint64_t currBandwidth, uint64_t availBandwidth, uint64_t currRam, uint64_t ramMax){
  std::map<std::string, outUrl> out;
  std::map<std::string, streamDetails> s;
  streamDetails x;
  if (!hostMutex){hostMutex = new tthread::recursive_mutex();}
  tthread::lock_guard<tthread::recursive_mutex> guard(*hostMutex);
  for (int i = 0; i < streams.size(); i++){
    s.insert(std::pair<std::string, streamDetails>(
        streams[streamsKey][i]["key"], *(x.destringify(streams[streamsKey][i]["streamDetails"]))));
  }
  update(fillStateOut, fillStreamsOut, scoreSource, scoreRate, out, conf_streams.asStringSet(), s,
         tags.asStringSet(), cpu, servLati, servLongi, binHost, host, toAdd, currBandwidth,
         availBandwidth, currRam, ramMax);
}

/**
 * constructor of server monitored by this load balancer
 */
hostDetailsCalc::hostDetailsCalc(char *name)
    : hostDetails(name), total(0), upPrev(0), downPrev(0), prevTime(0), upSpeed(0), downSpeed(0){
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
      r[tagKey].append(*it);
    }
  }
  if (ramMax && availBandwidth){
    r["score"]["cpu"] = (uint64_t)(weight_cpu - (cpu * weight_cpu) / 1000);
    r["score"]["ram"] = (uint64_t)(weight_ram - ((ramCurr * weight_ram) / ramMax));
    r["score"]["bw"] =
        (uint64_t)(weight_bw - (((upSpeed + addBandwidth + prevAddBandwidth) * weight_bw) / availBandwidth));
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
  uint64_t bw_score =
      (weight_bw - (((upSpeed + addBandwidth + prevAddBandwidth) * weight_bw) / availBandwidth));

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
  uint64_t bw_score =
      (weight_bw - (((upSpeed + addBandwidth + prevAddBandwidth) * weight_bw) / availBandwidth));

  uint64_t score = cpu_score + ram_score + bw_score + 1;
  return score;
}
/**
 * calculate and update precalculated variables
 */
void hostDetailsCalc::calc(){
  // calculated vars
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

  // update the local precalculated varsnclude <mist/config.h>

  ((hostDetails *)(this))->update(fillStateOut, fillStreamsOut, scoreSource, scoreRate, toadd);

  // create json to send to other load balancers
  JSON::Value j;
  j[fillstateout] = fillStateOut;
  j[fillStreamOut] = fillStreamsOut;
  j[scoreSourceKey] = scoreSource;
  j[scoreRateKey] = scoreRate;
  j[binhostKey] = binHost;

  j[outputsKey] = convertMapToJson(outputs);
  j[confStreamKey] = conf_streams;
  j[streamsKey] = convertMapToJson(streams);
  j[tagKey] = tags;

  j[cpuKey] = cpu;
  j[servLatiKey] = servLati;
  j[servLongiKey] = servLongi;
  j[hostnameKey] = name;
  j[identifierKey] = identifier;

  j[currBandwidthKey] = upSpeed + downSpeed;
  j[availBandwidthKey] = availBandwidth;
  j[currRAMKey] = ramCurr;
  j[ramMaxKey] = ramMax;

  j[toAddKey] = toadd;

  JSON::Value out;
  out["updateHost"] = j;

  // send to other load balancers
  for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); ++it){
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
  if (d.isMember(tagKey) && d[tagKey].isArray()){
    std::set<std::string> newTags;
    jsonForEach(d[tagKey], tag){
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

  if (d.isMember(streamsKey) && d[streamsKey].size()){
    jsonForEach(d[streamsKey], it){
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
      for (std::map<std::string, streamDetails>::iterator it = streams.begin(); it != streams.end(); ++it){
        if (!d[streamsKey].isMember(it->first)){eraseList.insert(it->first);}
      }
      for (std::set<std::string>::iterator it = eraseList.begin(); it != eraseList.end(); ++it){
        streams.erase(*it);
      }
    }
  }else{
    streams.clear();
  }
  conf_streams.clear();
  if (d.isMember(confStreamKey) && d[confStreamKey].size()){
    jsonForEach(d[confStreamKey], it){
      conf_streams.insert(it->asStringRef());
    }
  }
  outputs.clear();
  if (d.isMember(outputsKey) && d[outputsKey].size()){
    jsonForEach(d[outputsKey], op){
      outputs[op.key()] = outUrl(op->asStringRef(), host);
    }
  }
  prevAddBandwidth = 0.75 * (prevAddBandwidth);
  calc(); // update preclaculated host vars
}

/**
 * convert degrees to radians
 */
inline double toRad(double degree){
  return degree / 57.29577951308232087684;
}
/**
 * calcuate distance between 2 coordinates
 */
double geoDist(double lat1, double long1, double lat2, double long2){
  double dist;
  dist = sin(toRad(lat1)) * sin(toRad(lat2)) + cos(toRad(lat1)) * cos(toRad(lat2)) * cos(toRad(long1 - long2));
  return .31830988618379067153 * acos(dist);
}

/**
 * convert maps<string, object> \param s to json where the object has a stringify function
 */
template <typename data> JSON::Value convertMapToJson(std::map<std::string, data> s){
  JSON::Value out;
  for (typename std::map<std::string, data>::iterator it = s.begin(); it != s.end(); ++it){
    out[(*it).first] = (*it).second.stringify();
  }
  return out;
}

/**
 * puts server in standby made with locked status depending on \param lock
 */
void setStandBy(hostEntry *H, bool lock){
  if (H->state != STATE_ACTIVE || H->state != STATE_ONLINE){
    WARN_MSG("server %s is not available", H->name);
    return;
  }
  H->state = STATE_ACTIVE;
  H->standByLock = lock;
  JSON::Value j;
  j[standbyKey] = H->name;
  j[lockKey] = lock;
  for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
    (*it)->send(j.asString());
  }
}

/**
 * remove standby status from server
 * does not remove locked standby
 */
void removeStandBy(hostEntry *H){
  if (H->standByLock){
    WARN_MSG("can't activate server. it is locked in standby mode");
    return;
  }
  if (H->state == STATE_ONLINE){
    H->state = STATE_ACTIVE;
    INFO_MSG("server %s removed from standby mode", H->name);
    JSON::Value j;
    j[removeStandbyKey] = H->name;
    for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
      (*it)->send(j.asString());
    }
    return;
  }
  WARN_MSG("can't activate server. server is not running or already active");
}
