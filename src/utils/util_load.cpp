#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/downloader.h>
#include <mist/timing.h>
#include <mist/tinythread.h>
#include <mist/util.h>
#include <set>
#include <stdint.h>
#include <string>

Util::Config *cfg = 0;
std::string passphrase;
std::string fallback;
bool localMode = false;
bool useDtscs = false;
tthread::mutex globalMutex;

size_t weight_cpu = 500;
size_t weight_ram = 500;
size_t weight_bw = 1000;
size_t weight_geo = 1000;
size_t weight_bonus = 50;
std::map<std::string, int32_t> blankTags;
unsigned long hostsCounter = 0; // This is a pointer to guarantee atomic accesses.
#define HOSTLOOP                                                                                   \
  unsigned long i = 0;                                                                             \
  i < hostsCounter;                                                                                \
  ++i
#define HOST(no) (hosts[no])
#define HOSTCHECK                                                                                  \
  if (hosts[i].state != STATE_ONLINE){continue;}

#define STATE_OFF 0
#define STATE_BOOT 1
#define STATE_ERROR 2
#define STATE_ONLINE 3
#define STATE_GODOWN 4
#define STATE_REQCLEAN 5
const char *stateLookup[] ={"Offline",           "Starting monitoring",
                             "Monitored (error)", "Monitored (online)",
                             "Requesting stop",   "Requesting clean"};
#define HOSTNAMELEN 1024
#define MAXHOSTS 1000

struct streamDetails{
  uint64_t total;
  uint32_t inputs;
  uint32_t bandwidth;
  uint64_t prevTotal;
  uint64_t bytesUp;
  uint64_t bytesDown;
};

class outUrl{
public:
  std::string pre, post;
  outUrl(){};
  outUrl(const std::string &u, const std::string &host){
    std::string tmp = u;
    if (u.find("HOST") != std::string::npos){
      tmp = u.substr(0, u.find("HOST")) + host + u.substr(u.find("HOST") + 4);
    }
    size_t dolsign = tmp.find('$');
    pre = tmp.substr(0, dolsign);
    if (dolsign != std::string::npos){post = tmp.substr(dolsign + 1);}
  }
};

inline double toRad(double degree){
  return degree / 57.29577951308232087684;
}

double geoDist(double lat1, double long1, double lat2, double long2){
  double dist;
  dist = sin(toRad(lat1)) * sin(toRad(lat2)) + cos(toRad(lat1)) * cos(toRad(lat2)) * cos(toRad(long1 - long2));
  return .31830988618379067153 * acos(dist);
}

int32_t applyAdjustment(const std::set<std::string> & tags, const std::string & match, int32_t adj){
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

class hostDetails{
private:
  tthread::mutex *hostMutex;
  std::map<std::string, struct streamDetails> streams;
  std::set<std::string> conf_streams;
  std::set<std::string> tags;
  std::map<std::string, outUrl> outputs;
  uint64_t cpu;
  uint64_t ramMax;
  uint64_t ramCurr;
  uint64_t upSpeed;
  uint64_t downSpeed;
  uint64_t total;
  uint64_t upPrev;
  uint64_t downPrev;
  uint64_t prevTime;
  uint64_t addBandwidth;

public:
  std::string host;
  char binHost[16];
  uint64_t availBandwidth;
  JSON::Value geoDetails;
  double servLati, servLongi;
  std::string servLoc;
  hostDetails(){
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
  ~hostDetails(){
    if (hostMutex){
      delete hostMutex;
      hostMutex = 0;
    }
  }
  void badNess(){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
    addBandwidth += 1 * 1024 * 1024;
    addBandwidth *= 1.2;
  }
  /// Returns the count of viewers for a given stream s.
  size_t count(std::string &s){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
    if (streams.count(s)){return streams[s].total;}
    return 0;
  }
  /// Fills out a by reference given JSON::Value with current state.
  void fillState(JSON::Value &r){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
    r["cpu"] = (uint64_t)(cpu / 10);
    if (ramMax){r["ram"] = (uint64_t)((ramCurr * 100) / ramMax);}
    r["up"] = upSpeed;
    r["up_add"] = addBandwidth;
    r["down"] = downSpeed;
    r["streams"] = (uint64_t)streams.size();
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
  void fillStreams(JSON::Value &r){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
    for (std::map<std::string, struct streamDetails>::iterator jt = streams.begin();
         jt != streams.end(); ++jt){
      r[jt->first] = r[jt->first].asInt() + jt->second.total;
    }
  }
  /// Fills out a by reference given JSON::Value with current stream statistics.
  void fillStreamStats(const std::string & s, JSON::Value &r){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
    for (std::map<std::string, struct streamDetails>::iterator jt = streams.begin();
         jt != streams.end(); ++jt){
      const std::string & n = jt->first;
      if (s != "*" && n != s && n.substr(0, s.size()+1) != s+"+"){continue;}
      if (!r.isMember(n)){
        r[n].append(jt->second.total);//viewers
        r[n].append(jt->second.bandwidth);//bandwidth usage
        r[n].append(jt->second.bytesUp);//total bytes up
        r[n].append(jt->second.bytesDown);//total bytes down
      }else{
        r[n][0u] = r[n][0u].asInt() + jt->second.total;
        r[n][2u] = r[n][2u].asInt() + jt->second.bytesUp;
        r[n][3u] = r[n][3u].asInt() + jt->second.bytesDown;
      }
    }
  }
  /// Returns viewcount for the given stream
  long long getViewers(const std::string &strm){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
    if (!streams.count(strm)){return 0;}
    return streams[strm].total;
  }
  /// Scores a potential new connection to this server
  /// 0 means not possible, the higher the better.
  uint64_t rate(std::string &s, double lati = 0, double longi = 0, const std::map<std::string, int32_t> &tagAdjust = blankTags){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
    if (!ramMax || !availBandwidth){
      WARN_MSG("Host %s invalid: RAM %" PRIu64 ", BW %" PRIu64, host.c_str(), ramMax, availBandwidth);
      return 0;
    }
    if (upSpeed >= availBandwidth || (upSpeed + addBandwidth) >= availBandwidth){
      INFO_MSG("Host %s over bandwidth: %" PRIu64 "+%" PRIu64 " >= %" PRIu64, host.c_str(), upSpeed,
               addBandwidth, availBandwidth);
      return 0;
    }
    if (conf_streams.size() && !conf_streams.count(s) &&
        !conf_streams.count(s.substr(0, s.find_first_of("+ ")))){
      MEDIUM_MSG("Stream %s not available from %s", s.c_str(), host.c_str());
      return 0;
    }
    // Calculate score
    uint64_t cpu_score = (weight_cpu - (cpu * weight_cpu) / 1000);
    uint64_t ram_score = (weight_ram - ((ramCurr * weight_ram) / ramMax));
    uint64_t bw_score = (weight_bw - (((upSpeed + addBandwidth) * weight_bw) / availBandwidth));
    uint64_t geo_score = 0;
    if (servLati && servLongi && lati && longi){
      geo_score = weight_geo - weight_geo * geoDist(servLati, servLongi, lati, longi);
    }
    uint64_t score = cpu_score + ram_score + bw_score + geo_score + (streams.count(s) ? weight_bonus : 0);
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
    // Print info on host
    MEDIUM_MSG("%s: CPU %" PRIu64 ", RAM %" PRIu64 ", Stream %zu, BW %" PRIu64
               " (max %" PRIu64 " MB/s), Geo %" PRIu64 ", tag adjustment %" PRId64 " -> %" PRIu64,
               host.c_str(), cpu_score, ram_score, streams.count(s) ? weight_bonus : (size_t)0, bw_score,
               availBandwidth / 1024 / 1024, geo_score, adjustment, score);
    return score;
  }
  /// Scores this server as a source
  /// 0 means not possible, the higher the better.
  uint64_t source(const std::string &s, double lati, double longi, const std::map<std::string, int32_t> &tagAdjust, uint32_t minCpu){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
    if (s.size() && (!streams.count(s) || !streams[s].inputs)){return 0;}
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
    if (minCpu && cpu + minCpu >= 1000){return 0;}
    uint64_t cpu_score = (weight_cpu - (cpu * weight_cpu) / 1000);
    uint64_t ram_score = (weight_ram - ((ramCurr * weight_ram) / ramMax));
    uint64_t bw_score = (weight_bw - (((upSpeed + addBandwidth) * weight_bw) / availBandwidth));
    uint64_t geo_score = 0;
    if (servLati && servLongi && lati && longi){
      geo_score = weight_geo - weight_geo * geoDist(servLati, servLongi, lati, longi);
    }
    uint64_t score = cpu_score + ram_score + bw_score + geo_score + 1;
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
    // Print info on host
    MEDIUM_MSG("SOURCE %s: CPU %" PRIu64 ", RAM %" PRIu64 ", Stream %zu, BW %" PRIu64
               " (max %" PRIu64 " MB/s), Geo %" PRIu64 ", tag adjustment %" PRId64 " -> %" PRIu64,
               host.c_str(), cpu_score, ram_score, streams.count(s) ? weight_bonus : (size_t)0, bw_score,
               availBandwidth / 1024 / 1024, geo_score, adjustment, score);
    return score;
  }
  std::string getUrl(std::string &s, std::string &proto){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
    if (!outputs.count(proto)){return "";}
    const outUrl &o = outputs[proto];
    return o.pre + s + o.post;
  }
  void addViewer(std::string &s){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
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
  }
  void update(JSON::Value &d){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
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
        struct streamDetails &strm = streams[it.key()];
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
        for (std::map<std::string, struct streamDetails>::iterator it = streams.begin();
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
  }
};

/// Fixed-size struct for holding a host's name and details pointer
struct hostEntry{
  uint8_t state; // 0 = off, 1 = booting, 2 = running, 3 = requesting shutdown, 4 = requesting clean
  char name[HOSTNAMELEN];          // host+port for server
  hostDetails *details;    /// hostDetails pointer
  tthread::thread *thread; /// thread pointer
};

hostEntry hosts[MAXHOSTS]; /// Fixed-size array holding all hosts

void initHost(hostEntry &H, const std::string &N);
void cleanupHost(hostEntry &H);

///Fills the given map with the given JSON string of tag adjustments
void fillTagAdjust(std::map<std::string, int32_t> & tags, const std::string & adjust){
  JSON::Value adj = JSON::fromString(adjust);
  jsonForEach(adj, t){
    tags[t.key()] = t->asInt();
  }
}

int handleRequest(Socket::Connection &conn){
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
        H.Clean();
        H.SetHeader("Content-Type", "text/plain");
        JSON::Value ret;
        // Get/set weights
        if (weights.size()){
          JSON::Value newVals = JSON::fromString(weights);
          if (newVals.isMember("cpu")){weight_cpu = newVals["cpu"].asInt();}
          if (newVals.isMember("ram")){weight_ram = newVals["ram"].asInt();}
          if (newVals.isMember("bw")){weight_bw = newVals["bw"].asInt();}
          if (newVals.isMember("geo")){weight_geo = newVals["geo"].asInt();}
          if (newVals.isMember("bonus")){weight_bonus = newVals["bonus"].asInt();}
          ret["cpu"] = (uint64_t)weight_cpu;
          ret["ram"] = (uint64_t)weight_ram;
          ret["bw"] = (uint64_t)weight_bw;
          ret["geo"] = (uint64_t)weight_geo;
          ret["bonus"] = (uint64_t)weight_bonus;
          H.SetBody(ret.toString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          continue;
        }
        // Get server list
        if (lstserver.size()){
          for (HOSTLOOP){
            if (hosts[i].state == STATE_OFF){continue;}
            ret[(std::string)hosts[i].name] = stateLookup[hosts[i].state];
          }
          H.SetBody(ret.toPrettyString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          continue;
        }
        // Remove server from list
        if (delserver.size()){
          tthread::lock_guard<tthread::mutex> globGuard(globalMutex);
          ret = "Server not monitored - could not delete from monitored server list!";
          for (HOSTLOOP){
            if (hosts[i].state == STATE_OFF){continue;}
            if ((std::string)hosts[i].name == delserver){
              cleanupHost(hosts[i]);
              ret = stateLookup[hosts[i].state];
            }
          }
          H.SetBody(ret.toPrettyString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          continue;
        }
        // Add server to list
        if (addserver.size()){
          tthread::lock_guard<tthread::mutex> globGuard(globalMutex);
          if (addserver.size() >= HOSTNAMELEN){
            H.SetBody("Host length too long for monitoring");
            H.setCORSHeaders();
            H.SendResponse("200", "OK", conn);
            H.Clean();
            continue;
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
          H.SetBody(ret.toPrettyString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          continue;
        }
        // Request viewer count
        if (viewers.size()){
          for (HOSTLOOP){
            if (hosts[i].state == STATE_OFF){continue;}
            HOST(i).details->fillStreams(ret);
          }
          H.SetBody(ret.toPrettyString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          continue;
        }
        // Request full stream statistics
        if (streamStats.size()){
          for (HOSTLOOP){
            if (hosts[i].state == STATE_OFF){continue;}
            HOST(i).details->fillStreamStats(streamStats, ret);
          }
          H.SetBody(ret.toPrettyString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          continue;
        }
        if (stream.size()){
          uint64_t count = 0;
          for (HOSTLOOP){
            if (hosts[i].state == STATE_OFF){continue;}
            count += HOST(i).details->getViewers(stream);
          }
          H.SetBody(JSON::Value(count).asString());
          H.setCORSHeaders();
          H.SendResponse("200", "OK", conn);
          H.Clean();
          continue;
        }
        // Find source for given stream
        if (source.size()){
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
            if (Socket::matchIPv6Addr(std::string(HOST(i).details->binHost, 16), conn.getBinHost(), 0)){
              INFO_MSG("Ignoring same-host entry %s", HOST(i).details->host.data());
              continue;
            }
            uint64_t score = HOST(i).details->source(source, lat, lon, tagAdjust, 0);
            if (score > bestScore){
              bestHost = (useDtscs ? "dtscs://" : "dtsc://") + HOST(i).details->host;
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
          continue;
        }
        // Find optimal ingest point
        if (ingest.size()){
          double cpuUse = atof(ingest.c_str());
          INFO_MSG("Finding ingest point for CPU usage %.2f", cpuUse);
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
            uint64_t score = HOST(i).details->source("", lat, lon, tagAdjust, cpuUse * 10);
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
          continue;
        }
        // Find host(s) status
        if (!host.size()){
          for (HOSTLOOP){
            if (hosts[i].state == STATE_OFF){continue;}
            ret[HOST(i).details->host] = stateLookup[hosts[i].state];
            HOSTCHECK;
            HOST(i).details->fillState(ret[HOST(i).details->host]);
          }
        }else{
          for (HOSTLOOP){
            if (hosts[i].state == STATE_OFF){continue;}
            if (HOST(i).details->host == host){
              ret = stateLookup[hosts[i].state];
              HOSTCHECK;
              HOST(i).details->fillState(ret);
              break;
            }
          }
        }
        H.SetBody(ret.toPrettyString());
        H.setCORSHeaders();
        H.SendResponse("200", "OK", conn);
        H.Clean();
        continue;
      }
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
      std::string vars = H.allVars();
      if (stream == "favicon.ico"){
        H.Clean();
        H.SendResponse("404", "No favicon", conn);
        H.Clean();
        continue;
      }
      INFO_MSG("Balancing stream %s", stream.c_str());
      H.Clean();
      H.SetHeader("Content-Type", "text/plain");
      H.setCORSHeaders();
      hostEntry *bestHost = 0;
      uint64_t bestScore = 0;
      for (HOSTLOOP){
        HOSTCHECK;
        uint64_t score = HOST(i).details->rate(stream, lat, lon, tagAdjust);
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
  }
  conn.close();
  return 0;
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
  entry->details->availBandwidth = bandwidth.asInt();
  entry->details->host = url.host;
  entry->state = STATE_BOOT;
  bool down = true;

  HTTP::Downloader DL;

  while (cfg->is_active && (entry->state != STATE_GODOWN)){
    if (DL.get(url) && DL.isOk()){
      JSON::Value servData = JSON::fromString(DL.data());
      if (!servData){
        FAIL_MSG("Can't decode server %s load information", url.host.c_str());
        entry->details->badNess();
        DL.getSocket().close();
        down = true;
        entry->state = STATE_ERROR;
      }else{
        if (down){
          std::string ipStr;
          Socket::hostBytesToStr(DL.getSocket().getBinHost().data(), 16, ipStr);
          WARN_MSG("Connection established with %s (%s)", url.host.c_str(), ipStr.c_str());
          memcpy(entry->details->binHost, DL.getSocket().getBinHost().data(), 16);
          entry->state = STATE_ONLINE;
          down = false;
        }
        entry->details->update(servData);
      }
    }else{
      FAIL_MSG("Can't retrieve server %s load information", url.host.c_str());
      entry->details->badNess();
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
  opt["value"].append((uint64_t)weight_ram);
  conf.addOption("ram", opt);

  opt["arg"] = "integer";
  opt["short"] = "C";
  opt["long"] = "cpu";
  opt["help"] = "Weight for CPU scoring";
  opt["value"].append((uint64_t)weight_cpu);
  conf.addOption("cpu", opt);

  opt["arg"] = "integer";
  opt["short"] = "B";
  opt["long"] = "bw";
  opt["help"] = "Weight for BW scoring";
  opt["value"].append((uint64_t)weight_bw);
  conf.addOption("bw", opt);

  opt["arg"] = "integer";
  opt["short"] = "G";
  opt["long"] = "geo";
  opt["help"] = "Weight for geo scoring";
  opt["value"].append((uint64_t)weight_geo);
  conf.addOption("geo", opt);

  opt["arg"] = "integer";
  opt["short"] = "X";
  opt["long"] = "extra";
  opt["help"] = "Weight for extra scoring when stream exists";
  opt["value"].append((uint64_t)weight_bonus);
  conf.addOption("extra", opt);

  opt.null();
  opt["short"] = "L";
  opt["long"] = "localmode";
  opt["help"] = "Control only from local interfaces, request balance from all";
  conf.addOption("localmode", opt);

  opt.null();
  opt["short"] = "S";
  opt["long"] = "secure";
  opt["help"] = "Use DTSC-over-TSL i.e. dtscs:// instead of dtsc:// as pull protocol";
  conf.addOption("secure", opt);

  conf.parseArgs(argc, argv);

  passphrase = conf.getOption("passphrase").asStringRef();
  weight_ram = conf.getInteger("ram");
  weight_cpu = conf.getInteger("cpu");
  weight_bw = conf.getInteger("bw");
  weight_geo = conf.getInteger("geo");
  weight_bonus = conf.getInteger("extra");
  fallback = conf.getString("fallback");
  localMode = conf.getBool("localmode");
  useDtscs = conf.getBool("secure");

  INFO_MSG("Local control only mode is %s", localMode ? "on" : "off");
  INFO_MSG("Pull protocol is %s", useDtscs ? "dtscs://" : "dtsc://");

  JSON::Value &nodes = conf.getOption("server", true);
  conf.activate();

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

  conf.serveThreadedSocket(handleRequest);
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

void initHost(hostEntry &H, const std::string &N){
  // Cancel if this host has no name set
  if (!N.size()){return;}
  H.state = STATE_BOOT;
  H.details = new hostDetails();
  memset(H.name, 0, HOSTNAMELEN);
  memcpy(H.name, N.data(), N.size());
  H.thread = new tthread::thread(handleServer, (void *)&H);
  INFO_MSG("Starting monitoring %s", H.name);
}

void cleanupHost(hostEntry &H){
  // Cancel if this host has no name set
  if (!H.name[0]){return;}
  H.state = STATE_GODOWN;
  INFO_MSG("Stopping monitoring %s", H.name);
  // Clean up thread
  H.thread->join();
  delete H.thread;
  H.thread = 0;
  // Clean up details
  delete H.details;
  H.details = 0;
  memset(H.name, 0, HOSTNAMELEN);
  H.state = STATE_OFF;
}
