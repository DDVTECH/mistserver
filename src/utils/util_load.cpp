#include <stdint.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/timing.h>
#include <mist/tinythread.h>
#include <set>
#include <string>

Util::Config *cfg = 0;
std::string passphrase;
std::string fallback;

unsigned int weight_cpu = 500;
unsigned int weight_ram = 500;
unsigned int weight_bw = 1000;
unsigned int weight_bonus = 50;
unsigned long hostsCounter = 0; // This is a pointer to guarantee atomic accesses.
#define HOSTLOOP                                                                                   \
  unsigned long i = 0;                                                                             \
  i < hostsCounter;                                              \
  ++i
#define HOST(no) (hosts[no])
#define HOSTCHECK                                                                                  \
  if (hosts[i].state != STATE_ONLINE){continue;}

#define STATE_OFF 0
#define STATE_BOOT 1
#define STATE_ONLINE 2
#define STATE_GODOWN 3
#define STATE_REQCLEAN 4
const char *stateLookup[] ={"Offline", "Starting monitoring", "Monitored", "Requesting stop",
                             "Requesting clean"};

struct streamDetails{
  uint32_t total;
  uint32_t inputs;
  uint32_t bandwidth;
  uint32_t prevTotal;
};

class hostDetails{
private:
  tthread::mutex *hostMutex;
  std::map<std::string, struct streamDetails> streams;
  std::set<std::string> conf_streams;
  unsigned int cpu;
  unsigned long long ramMax;
  unsigned long long ramCurr;
  unsigned int upSpeed;
  unsigned int downSpeed;
  unsigned int total;
  unsigned long long upPrev;
  unsigned long long downPrev;
  unsigned long long prevTime;
  unsigned long long addBandwidth;

public:
  std::string host;
  unsigned long long availBandwidth;
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
  unsigned long long count(std::string &s){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
    if (streams.count(s)){
      return streams[s].total;
    }else{
      return 0;
    }
  }
  /// Fills out a by reference given JSON::Value with current state.
  void fillState(JSON::Value &r){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
    r["cpu"] = (long long)(cpu / 10);
    if (ramMax){r["ram"] = (long long)((ramCurr * 100) / ramMax);}
    r["up"] = (long long)upSpeed;
    r["up_add"] = (long long)addBandwidth;
    r["down"] = (long long)downSpeed;
    r["streams"] = (long long)streams.size();
    r["viewers"] = (long long)total;
    r["bwlimit"] = (long long)availBandwidth;
    if (ramMax && availBandwidth){
      r["score"]["cpu"] = (long long)(weight_cpu - (cpu * weight_cpu) / 1000);
      r["score"]["ram"] = (long long)(weight_ram - ((ramCurr * weight_ram) / ramMax));
      r["score"]["bw"] =
          (long long)(weight_bw - (((upSpeed + addBandwidth) * weight_bw) / availBandwidth));
    }
  }
  /// Fills out a by reference given JSON::Value with current streams.
  void fillStreams(JSON::Value &r){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
    for (std::map<std::string, struct streamDetails>::iterator jt = streams.begin();
         jt != streams.end(); ++jt){
      r[jt->first] = r[jt->first].asInt() + jt->second.total;
    }
  }
  /// Scores a potential new connection to this server
  /// 0 means not possible, the higher the better.
  unsigned int rate(std::string &s){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
    if (!ramMax || !availBandwidth){
      WARN_MSG("Host %s invalid: RAM %llu, BW %llu", host.c_str(), ramMax, availBandwidth);
      return 0;
    }
    if (upSpeed >= availBandwidth || (upSpeed + addBandwidth) >= availBandwidth){
      INFO_MSG("Host %s over bandwidth: %llu+%llu >= %llu", host.c_str(), upSpeed, addBandwidth,
               availBandwidth);
      return 0;
    }
    if (conf_streams.size() && !conf_streams.count(s) && !conf_streams.count(s.substr(0, s.find_first_of("+ ")))){
      MEDIUM_MSG("Stream %s not available from %s", s.c_str(), host.c_str());
      return 0;
    }
    // Calculate score
    unsigned int cpu_score = (weight_cpu - (cpu * weight_cpu) / 1000);
    unsigned int ram_score = (weight_ram - ((ramCurr * weight_ram) / ramMax));
    unsigned int bw_score = (weight_bw - (((upSpeed + addBandwidth) * weight_bw) / availBandwidth));
    unsigned int score = cpu_score + ram_score + bw_score + (streams.count(s) ? weight_bonus : 0);
    // Print info on host
    MEDIUM_MSG("%s: CPU %u, RAM %u, Stream %u, BW %u (max %llu MB/s) -> %u", host.c_str(),
               cpu_score, ram_score, streams.count(s) ? weight_bonus : 0, bw_score,
               availBandwidth / 1024 / 1024, score);
    return score;
  }
  /// Scores this server as a source
  /// 0 means not possible, the higher the better.
  unsigned int source(std::string &s){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
    if (!streams.count(s) || !streams[s].inputs){return 0;}
    if (!ramMax || !availBandwidth){
      WARN_MSG("Host %s invalid: RAM %llu, BW %llu", host.c_str(), ramMax, availBandwidth);
      return 1;
    }
    if (upSpeed >= availBandwidth || (upSpeed + addBandwidth) >= availBandwidth){
      INFO_MSG("Host %s over bandwidth: %llu+%llu >= %llu", host.c_str(), upSpeed, addBandwidth,
               availBandwidth);
      return 1;
    }
    // Calculate score
    unsigned int cpu_score = (weight_cpu - (cpu * weight_cpu) / 1000);
    unsigned int ram_score = (weight_ram - ((ramCurr * weight_ram) / ramMax));
    unsigned int bw_score = (weight_bw - (((upSpeed + addBandwidth) * weight_bw) / availBandwidth));
    unsigned int score = cpu_score + ram_score + bw_score + 1;
    // Print info on host
    MEDIUM_MSG("SOURCE %s: CPU %u, RAM %u, Stream %u, BW %u (max %llu MB/s) -> %u", host.c_str(),
               cpu_score, ram_score, streams.count(s) ? weight_bonus : 0, bw_score,
               availBandwidth / 1024 / 1024, score);
    return score;
  }
  void addViewer(std::string &s){
    if (!hostMutex){hostMutex = new tthread::mutex();}
    tthread::lock_guard<tthread::mutex> guard(*hostMutex);
    unsigned long long toAdd = 0;
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
    if (d.isMember("bwlimit") && d["bwlimit"].asInt()){
      availBandwidth = d["bwlimit"].asInt();
    }
    long long nRamMax = d["mem_total"].asInt();
    long long nRamCur = d["mem_used"].asInt();
    long long nShmMax = d["shm_total"].asInt();
    long long nShmCur = d["shm_used"].asInt();
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
    unsigned long long currUp = d["bw"][0u].asInt(), currDown = d["bw"][1u].asInt();
    unsigned int timeDiff = 0;
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
        unsigned int count =
            (*it)["curr"][0u].asInt() + (*it)["curr"][1u].asInt() + (*it)["curr"][2u].asInt();
        if (!count){
          if (streams.count(it.key())){streams.erase(it.key());}
          continue;
        }
        struct streamDetails &strm = streams[it.key()];
        strm.total = (*it)["curr"][0u].asInt();
        strm.inputs = (*it)["curr"][1u].asInt();
        unsigned long long currTotal = (*it)["bw"][0u].asInt() + (*it)["bw"][1u].asInt();
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
      jsonForEach(d["conf_streams"], it){
        conf_streams.insert(it->asStringRef());
      }
    }
    addBandwidth *= 0.75;
  }
};

/// Fixed-size struct for holding a host's name and details pointer
struct hostEntry{
  uint8_t state; // 0 = off, 1 = booting, 2 = running, 3 = requesting shutdown, 4 = requesting clean
  char name[200];          // 200 chars should be enough
  hostDetails *details;    /// hostDetails pointer
  tthread::thread *thread; /// thread pointer
};

hostEntry hosts[1000]; /// Fixed-size array holding all hosts

void initHost(hostEntry &H, const std::string &N);
void cleanupHost(hostEntry &H);

int handleRequest(Socket::Connection &conn){
  HTTP::Parser H;
  while (conn){
    if ((conn.spool() || conn.Received().size()) && H.Read(conn)){
      // Special commands
      if (H.url.size() == 1){
        std::string host = H.GetVar("host");
        std::string viewers = H.GetVar("viewers");
        std::string source = H.GetVar("source");
        std::string fback = H.GetVar("fallback");
        std::string lstserver = H.GetVar("lstserver");
        std::string addserver = H.GetVar("addserver");
        std::string delserver = H.GetVar("delserver");
        std::string weights = H.GetVar("weights");
        H.Clean();
        H.SetHeader("Content-Type", "text/plain");
        JSON::Value ret;
        //Get/set weights
        if (weights.size()){
          JSON::Value newVals = JSON::fromString(weights);
          if (newVals.isMember("cpu")){weight_cpu = newVals["cpu"].asInt();}
          if (newVals.isMember("ram")){weight_ram = newVals["ram"].asInt();}
          if (newVals.isMember("bw")){weight_bw = newVals["bw"].asInt();}
          if (newVals.isMember("bonus")){weight_bonus = newVals["bonus"].asInt();}
          ret["cpu"] = weight_cpu;
          ret["ram"] = weight_ram;
          ret["bw"] = weight_bw;
          ret["bonus"] = weight_bonus;
          H.SetBody(ret.toString());
          H.SendResponse("200", "OK", conn);
          H.Clean();
          continue;
        }
        // Get server list
        if (lstserver.size()){
          for (HOSTLOOP){
            HOSTCHECK;
            ret[(std::string)hosts[i].name] = stateLookup[hosts[i].state];
          }
          H.SetBody(ret.toPrettyString());
          H.SendResponse("200", "OK", conn);
          H.Clean();
          continue;
        }
        // Remove server from list
        if (delserver.size()){
          ret = "Server not monitored - could not delete from monitored server list!";
          for (HOSTLOOP){
            HOSTCHECK;
            if ((std::string)hosts[i].name == delserver){
              cleanupHost(hosts[i]);
              ret = stateLookup[hosts[i].state];
            }
          }
          H.SetBody(ret.toPrettyString());
          H.SendResponse("200", "OK", conn);
          H.Clean();
          continue;
        }
        // Add server to list
        if (addserver.size()){
          if (addserver.size() > 199){
            H.SetBody("Host length too long for monitoring");
            H.SendResponse("200", "OK", conn);
            H.Clean();
            continue;
          }
          bool stop = false;
          hostEntry *newEntry = 0;
          for (HOSTLOOP){
            HOSTCHECK;
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
          H.SendResponse("200", "OK", conn);
          H.Clean();
          continue;
        }
        // Request viewer count
        if (viewers.size()){
          for (HOSTLOOP){
            HOSTCHECK;
            HOST(i).details->fillStreams(ret);
          }
          H.SetBody(ret.toPrettyString());
          H.SendResponse("200", "OK", conn);
          H.Clean();
          continue;
        }
        // Find source for given stream
        if (source.size()){
          INFO_MSG("Finding source for stream %s", source.c_str());
          std::string bestHost = "";
          unsigned int bestScore = 0;
          for (HOSTLOOP){
            HOSTCHECK;
            unsigned int score = HOST(i).details->source(source);
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
            INFO_MSG("Winner: %s scores %u", bestHost.c_str(), bestScore);
          }
          H.SetBody(bestHost);
          H.SendResponse("200", "OK", conn);
          H.Clean();
          continue;
        }
        // Find host(s) status
        if (!host.size()){
          for (HOSTLOOP){
            HOSTCHECK;
            HOST(i).details->fillState(ret[HOST(i).details->host]);
          }
        }else{
          for (HOSTLOOP){
            HOSTCHECK;
            if (HOST(i).details->host == host){
              HOST(i).details->fillState(ret);
              break;
            }
          }
        }
        H.SetBody(ret.toPrettyString());
        H.SendResponse("200", "OK", conn);
        H.Clean();
        continue;
      }
      // Balance given stream
      std::string stream = H.url.substr(1);
      if (stream == "favicon.ico"){
        H.Clean();
        H.SendResponse("404", "No favicon", conn);
        H.Clean();
        continue;
      }
      INFO_MSG("Balancing stream %s", stream.c_str());
      H.Clean();
      H.SetHeader("Content-Type", "text/plain");
      hostEntry *bestHost = 0;
      unsigned int bestScore = 0;
      for (HOSTLOOP){
        HOSTCHECK;
        unsigned int score = HOST(i).details->rate(stream);
        if (score > bestScore){
          bestHost = &HOST(i);
          bestScore = score;
        }
      }
      if (!bestScore || !bestHost){
        H.SetBody(fallback);
        FAIL_MSG("All servers seem to be out of bandwidth!");
      }else{
        INFO_MSG("Winner: %s scores %u", bestHost->details->host.c_str(), bestScore);
        bestHost->details->addViewer(stream);
        H.SetBody(bestHost->details->host);
      }
      H.SendResponse("200", "OK", conn);
      H.Clean();
    }// if HTTP request received
  }
  conn.close();
  return 0;
}

void handleServer(void *hostEntryPointer){
  hostEntry *entry = (hostEntry *)hostEntryPointer;
  std::string name = entry->name;

  HTTP::Parser H;
  std::string host;
  int port = 4242;
  JSON::Value bandwidth = 128 * 1024 * 1024ll; // assume 1G connection

  size_t slash = name.find('/');
  if (slash != std::string::npos){
    bandwidth = name.substr(slash + 1, std::string::npos);
    bandwidth = bandwidth.asInt() * 1024 * 1024;
    name = name.substr(0, slash);
  }

  size_t colon = name.find(':');
  if (colon != std::string::npos && colon != 0 && colon != name.size()){
    host = name.substr(0, colon);
    port = atoi(name.substr(colon + 1, std::string::npos).c_str());
  }else{
    host = name;
  }

  INFO_MSG("Monitoring %s on port %d using passphrase %s", host.c_str(), port, passphrase.c_str());
  entry->details->availBandwidth = bandwidth.asInt();
  entry->details->host = host;
  entry->state = STATE_ONLINE;
  bool down = true;

  Socket::Connection servConn(host, port, false);
  while (cfg->is_active && (entry->state == STATE_ONLINE)){
    if (!servConn){
      HIGH_MSG("Reconnecting to %s", host.c_str());
      servConn = Socket::Connection(host, port, false);
    }
    if (!servConn){
      MEDIUM_MSG("Cannot reach server %s", host.c_str());
      entry->details->badNess();
      Util::wait(5000);
      down = true;
      continue;
    }

    // retrieve update information
    H.url = "/" + passphrase + ".json";
    H.method = "GET";
    H.SendRequest(servConn);
    H.Clean();
    unsigned int startTime = Util::epoch();
    while (cfg->is_active && servConn &&
           !((servConn.spool() || servConn.Received().size()) && H.Read(servConn))){
      if (Util::epoch() - startTime > 10){
        FAIL_MSG("Server %s timed out", host.c_str());
        servConn.close();
        H.Clean();
      }
      Util::sleep(250);
    }
    JSON::Value servData = JSON::fromString(H.body);
    if (!servData){
      FAIL_MSG("Can't retrieve server %s load information", host.c_str());
      std::cerr << H.body << std::endl;
      down = true;
      entry->details->badNess();
      servConn.close();
    }else{
      if (down){
        WARN_MSG("Connection established with %s", host.c_str());
        down = false;
      }
      entry->details->update(servData);
    }
    H.Clean();
    Util::wait(5000);
  }
  servConn.close();
  entry->state = STATE_REQCLEAN;
}

int main(int argc, char **argv){
  memset(hosts, 0, sizeof(hosts)); // zero-fill the hosts list
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
  opt["value"].append((long long)8042);
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
  opt["value"].append((long long)weight_ram);
  conf.addOption("ram", opt);

  opt["arg"] = "integer";
  opt["short"] = "C";
  opt["long"] = "cpu";
  opt["help"] = "Weight for CPU scoring";
  opt["value"].append((long long)weight_cpu);
  conf.addOption("cpu", opt);

  opt["arg"] = "integer";
  opt["short"] = "B";
  opt["long"] = "bw";
  opt["help"] = "Weight for BW scoring";
  opt["value"].append((long long)weight_bw);
  conf.addOption("bw", opt);

  opt["arg"] = "integer";
  opt["short"] = "X";
  opt["long"] = "extra";
  opt["help"] = "Weight for extra scoring when stream exists";
  opt["value"].append((long long)weight_bonus);
  conf.addOption("extra", opt);

  conf.parseArgs(argc, argv);

  passphrase = conf.getOption("passphrase").asStringRef();
  weight_ram = conf.getInteger("ram");
  weight_cpu = conf.getInteger("cpu");
  weight_bw = conf.getInteger("bw");
  weight_bonus = conf.getInteger("extra");
  fallback = conf.getString("fallback");

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
  WARN_MSG("Load balancer activating. Balancing between %lu nodes.", (unsigned long)hostsCounter);

  conf.serveThreadedSocket(handleRequest);
  if (!conf.is_active){
    WARN_MSG("Load balancer shutting down; received shutdown signal");
  }else{
    WARN_MSG("Load balancer shutting down; socket problem");
  }
  conf.is_active = false;

  // Join all threads
  for (HOSTLOOP){cleanupHost(HOST(i));}
}

void initHost(hostEntry &H, const std::string &N){
  // Cancel if this host has no name set
  if (!N.size()){return;}
  H.state = STATE_BOOT;
  H.details = new hostDetails();
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
  memset(H.name, 0, sizeof(H.name));
  H.state = STATE_OFF;
}

