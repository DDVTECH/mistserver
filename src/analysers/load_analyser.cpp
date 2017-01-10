#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <set>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/timing.h>
#include <mist/http_parser.h>
#include <mist/tinythread.h>

Util::Config * cfg = 0;
std::string passphrase;
std::string fallback;

unsigned int weight_cpu = 500;
unsigned int weight_ram = 500;
unsigned int weight_bw = 1000;
unsigned int weight_bonus = 50;

struct streamDetails{
  unsigned int total;
  unsigned int bandwidth;
  unsigned long long prevTotal;
};

class hostDetails{
  private:
    tthread::mutex * hostMutex;
    std::map<std::string, struct streamDetails> streams;
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
      availBandwidth = 128 * 1024 * 1024;//assume 1G connections
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
    ///Returns the count of viewers for a given stream s.
    unsigned long long count(std::string & s){
      if (!hostMutex){hostMutex = new tthread::mutex();}
      tthread::lock_guard<tthread::mutex> guard(*hostMutex);
      if (streams.count(s)){
        return streams[s].total;
      }else{
        return 0;
      }
    }
    ///Fills out a by reference given JSON::Value with current state.
    void fillState(JSON::Value & r){
      if (!hostMutex){hostMutex = new tthread::mutex();}
      tthread::lock_guard<tthread::mutex> guard(*hostMutex);
      r["cpu"] = (long long)(cpu/10);
      if (ramMax){r["ram"] = (long long)((ramCurr*100) / ramMax);}
      r["up"] = (long long)upSpeed;
      r["up_add"] = (long long)addBandwidth;
      r["down"] = (long long)downSpeed;
      r["streams"] = (long long)streams.size();
      r["viewers"] = (long long)total;
      if (ramMax && availBandwidth){
        r["score"]["cpu"] = (long long)(weight_cpu - (cpu*weight_cpu)/1000);
        r["score"]["ram"] = (long long)(weight_ram - ((ramCurr * weight_ram) / ramMax));
        r["score"]["bw"] = (long long)(weight_bw - (((upSpeed + addBandwidth) * weight_bw) / availBandwidth));
      }
    }
    ///Fills out a by reference given JSON::Value with current streams.
    void fillStreams(JSON::Value & r){
      if (!hostMutex){hostMutex = new tthread::mutex();}
      tthread::lock_guard<tthread::mutex> guard(*hostMutex);
      for (std::map<std::string, struct streamDetails>::iterator jt = streams.begin(); jt != streams.end(); ++jt){
        r[jt->first] = r[jt->first].asInt() + jt->second.total;
      }
    }
    ///Scores a potential new connection to this server
    ///0 means not possible, the higher the better.
    unsigned int rate(std::string & s){
      if (!hostMutex){hostMutex = new tthread::mutex();}
      tthread::lock_guard<tthread::mutex> guard(*hostMutex);
      if (!ramMax || !availBandwidth){
        WARN_MSG("Host %s invalid: RAM %llu, BW %llu", host.c_str(), ramMax, availBandwidth);
        return 0;
      }
      if (upSpeed >= availBandwidth || (upSpeed + addBandwidth) >= availBandwidth){
        INFO_MSG("Host %s over bandwidth: %llu+%llu >= %llu", host.c_str(), upSpeed, addBandwidth, availBandwidth);
        return 0;
      }
      //Calculate score
      unsigned int cpu_score = (weight_cpu - (cpu*weight_cpu)/1000);
      unsigned int ram_score = (weight_ram - ((ramCurr * weight_ram) / ramMax));
      unsigned int bw_score = (weight_bw - (((upSpeed + addBandwidth) * weight_bw) / availBandwidth));
      unsigned int score = cpu_score + ram_score + bw_score + (streams.count(s)?weight_bonus:0);
      //Print info on host
      MEDIUM_MSG("%s: CPU %u, RAM %u, Stream %u, BW %u (max %llu MB/s) -> %u", host.c_str(), cpu_score, ram_score, streams.count(s)?weight_bonus:0, bw_score, availBandwidth / 1024 / 1024, score);
      return score;
    }
    void addViewer(std::string & s){
      if (!hostMutex){hostMutex = new tthread::mutex();}
      tthread::lock_guard<tthread::mutex> guard(*hostMutex);
      unsigned long long toAdd = 0;
      if (streams.count(s)){
        toAdd = streams[s].bandwidth;
      }else{
        if (total){
          toAdd = (upSpeed + downSpeed) / total;
        }else{
          toAdd = 131072;//assume 1mbps
        }
      }
      //ensure reasonable limits of bandwidth guesses
      if (toAdd < 64*1024){toAdd = 64*1024;}//minimum of 0.5 mbps
      if (toAdd > 1024*1024){toAdd = 1024*1024;}//maximum of 8 mbps
      addBandwidth += toAdd;
    }
    void update(JSON::Value & d){
      if (!hostMutex){hostMutex = new tthread::mutex();}
      tthread::lock_guard<tthread::mutex> guard(*hostMutex);
      cpu = d["cpu"].asInt();
      long long nRamMax = d["mem_total"].asInt();
      long long nRamCur = d["mem_used"].asInt();
      long long nShmMax = d["shm_total"].asInt();
      long long nShmCur = d["shm_used"].asInt();
      if (!nRamMax){nRamMax = 1;}
      if (!nShmMax){nShmMax = 1;}
      if (((nRamCur + nShmCur)*1000) / nRamMax > (nShmCur*1000) / nShmMax){
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
          unsigned int count = (*it)["curr"][0u].asInt() + (*it)["curr"][1u].asInt() + (*it)["curr"][2u].asInt();
          if (!count){
            if (streams.count(it.key())){
              streams.erase(it.key());
            }
            continue;
          }
          struct streamDetails & strm = streams[it.key()];
          strm.total = (*it)["curr"][0u].asInt();
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
          for (std::map<std::string, struct streamDetails>::iterator it = streams.begin(); it != streams.end(); ++it){
            if (!d["streams"].isMember(it->first)){
              eraseList.insert(it->first);
            }
          }
          for (std::set<std::string>::iterator it = eraseList.begin(); it != eraseList.end(); ++it){
            streams.erase(*it);
          }
        }
      }else{
        streams.clear();
      }
      addBandwidth *= 0.75;
    }
};

std::map<std::string, hostDetails> hosts;


int handleRequest(Socket::Connection & conn){
  HTTP::Parser H;
  while (conn){
    if ((conn.spool() || conn.Received().size()) && H.Read(conn)){
      if (H.url.size() == 1){
        std::string host = H.GetVar("host");
        std::string stream = H.GetVar("stream");
        std::string viewers = H.GetVar("viewers");
        H.Clean();
        H.SetHeader("Content-Type", "text/plain");
        JSON::Value ret;
        if (viewers.size()){
          for (std::map<std::string, hostDetails>::iterator it = hosts.begin(); it != hosts.end(); ++it){
            it->second.fillStreams(ret);
          }
          H.SetBody(ret.toPrettyString());
          H.SendResponse("200", "OK", conn);
          H.Clean();
          continue;
        }else{
          if (!host.size() && !stream.size()){
              for (std::map<std::string, hostDetails>::iterator it = hosts.begin(); it != hosts.end(); ++it){
                it->second.fillState(ret[it->first]);
              }
          }else{
            if (hosts.count(host)){
              hosts[host].fillState(ret);
            }
          }
          H.SetBody(ret.toPrettyString());
          H.SendResponse("200", "OK", conn);
          H.Clean();
          continue;
        }
      }
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
      std::string bestHost = "";
      unsigned int bestScore = 0;
      for (std::map<std::string, hostDetails>::iterator it = hosts.begin(); it != hosts.end(); ++it){
        unsigned int score = it->second.rate(stream);
        if (score > bestScore){
          bestHost = it->first;
          bestScore = score;
        }
      }
      if (bestScore == 0){
        bestHost = fallback;
        FAIL_MSG("All servers seem to be out of bandwidth!");
      }else{
        INFO_MSG("Winner: %s scores %u", bestHost.c_str(), bestScore);
        hosts[bestHost].addViewer(stream);
      }
      H.SetBody(bestHost);
      H.SendResponse("200", "OK", conn);
      H.Clean();
    }//if HTTP request received
  }
  conn.close();
  return 0;
}

void handleServer(void * servName){
  std::string & name = *(std::string*)servName;
  
  HTTP::Parser H;
  std::string host;
  int port = 4242;
  JSON::Value bandwidth = 128 * 1024 * 1024ll;//assume 1G connection

  size_t slash = name.find('/');
  if (slash != std::string::npos){
    bandwidth = name.substr(slash+1, std::string::npos);
    bandwidth = bandwidth.asInt() * 1024 * 1024;
    name = name.substr(0, slash);
  }

  size_t colon = name.find(':');
  if (colon != std::string::npos && colon != 0 && colon != name.size()) {
    host = name.substr(0, colon);
    port = atoi(name.substr(colon + 1, std::string::npos).c_str());
  }else{
    host = name;
  }

  hosts[host].availBandwidth = bandwidth.asInt();
  hosts[host].host = host;

  INFO_MSG("Monitoring %s on port %d.", host.c_str(), port, passphrase.c_str());
  bool down = true;

  Socket::Connection servConn(host, port, false);
  while (cfg->is_active){
    if (!servConn){
      HIGH_MSG("Reconnecting to %s", host.c_str());
      servConn = Socket::Connection(host, port, false);
    }
    if (!servConn){
      MEDIUM_MSG("Can't reach server %s", host.c_str());
      hosts[host].badNess();
      Util::wait(5000);
      down = true;
      continue;
    }

    //retrieve update information
    H.url = "/" + passphrase + ".json";
    H.method = "GET";
    H.SendRequest(servConn);
    H.Clean();
    unsigned int startTime = Util::epoch();
    while (cfg->is_active && servConn && !((servConn.spool() || servConn.Received().size()) && H.Read(servConn))){
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
      hosts[host].badNess();
      servConn.close();
    }else{
      if (down){
        WARN_MSG("Connection established with %s", host.c_str());
        down = false;
      }
      hosts[host].update(servData);
    }
    H.Clean();
    Util::wait(5000);
  }
  servConn.close();
}

int main(int argc, char ** argv){
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

  JSON::Value & nodes = conf.getOption("server", true);
  WARN_MSG("Load balancer activating. Balancing between %llu nodes.", nodes.size());
  conf.activate();

  std::map<std::string, tthread::thread *> threads;
  jsonForEach(nodes, it){
    threads[it->asStringRef()] = new tthread::thread(handleServer, (void*)&(it->asStringRef()));
  }

  conf.serveThreadedSocket(handleRequest);
  if (!conf.is_active){
    WARN_MSG("Load balancer shutting down; received shutdown signal");
  }else{
    WARN_MSG("Load balancer shutting down; socket problem");
  }

  if (threads.size()){
    for (std::map<std::string, tthread::thread *>::iterator it = threads.begin(); it != threads.end(); ++it){
      it->second->join();
    }
  }

}

