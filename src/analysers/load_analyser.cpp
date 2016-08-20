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
      r["down"] = (long long)downSpeed;
      r["streams"] = (long long)streams.size();
      r["viewers"] = (long long)total;
    }
    ///Fills out a by reference given JSON::Value with current streams.
    void fillStreams(JSON::Value & r){
      if (!hostMutex){hostMutex = new tthread::mutex();}
      tthread::lock_guard<tthread::mutex> guard(*hostMutex);
      for (std::map<std::string, struct streamDetails>::iterator jt = streams.begin(); jt != streams.end(); ++jt){
        r[jt->first] = r[jt->first].asInt() + jt->second.total;
      }
    }
    ///Scores a potential new connection to this server, on a scale from 0 to 3200.
    ///0 is horrible, 3200 is perfect.
    unsigned int rate(std::string & s){
      if (!hostMutex){hostMutex = new tthread::mutex();}
      tthread::lock_guard<tthread::mutex> guard(*hostMutex);
      unsigned int score = 0;
      if (!ramMax){
        return 0;
      }
      //First, add current CPU/RAM left to the score, on a scale from 0 to 1000.
      score += (1000 - cpu) + (1000 - ((ramCurr * 1000) / ramMax));
      //Next, we add 200 points if the stream is already available.
      if (streams.count(s)){score += 200;}
      //Finally, account for bandwidth. We again scale from 0 to 1000 where 1000 is perfect.
      long long bwscore = (1000 - ((upSpeed * 1000) / availBandwidth));
      if (bwscore < 0){bwscore = 0;}
      long long bw_sub = ((addBandwidth * 1000) / availBandwidth);
      if (bwscore - bw_sub < 0){bw_sub = bwscore;}
      if (bwscore - bw_sub > 0){
        score += (bwscore - bw_sub);
      }else{
        score = 0;
      }
      MEDIUM_MSG("CPU %u, RAM %u, Stream %u, BW %u (-%u) (max %llu MB/s) -> %u", 1000-cpu, 1000-((ramCurr * 1000) / ramMax), streams.count(s)?200:0, bwscore, bw_sub, availBandwidth / 1024 / 1024, score);
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
          toAdd = (upSpeed + downSpeed) + 100000;
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
      addBandwidth *= 0.9;
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
        INFO_MSG("%s scores %u", it->first.c_str(), score);
      }
      if (bestScore == 0){
        bestHost = "FULL";
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

  INFO_MSG("Monitoring %s on port %d.", host.c_str(), port, passphrase.c_str());
  bool down = true;

  Socket::Connection servConn(host, port, false);
  while (cfg->is_active){
    if (!servConn){
      WARN_MSG("Reconnecting to %s", host.c_str());
      servConn = Socket::Connection(host, port, false);
    }
    if (!servConn){
      FAIL_MSG("Can't reach server %s", host.c_str());
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
      if (Util::epoch() - startTime > 2){
        servConn.close();
        H.Clean();
      }
      Util::sleep(250);
    }
    JSON::Value servData = JSON::fromString(H.body);
    if (!servData){
      FAIL_MSG("Can't retrieve server %s load information", host.c_str());
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

  conf.parseArgs(argc, argv);
 
  passphrase = conf.getOption("passphrase").asStringRef();

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

