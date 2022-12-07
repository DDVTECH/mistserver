#include "util_load.h"
#include <mist/downloader.h>
#include <mist/triggers.h>
#include <mist/encryption.h>

// rebalancing
int minstandby = 1;
int maxstandby = 1;
double cappacityTriggerCPUDec = 0.01; // percentage om cpu te verminderen
double cappacitytriggerBWDec = 0.01;  // percentage om bandwidth te verminderen
double cappacityTriggerRAMDec = 0.01; // percentage om ram te verminderen
double cappacityTriggerCPU = 0.9;     // max capacity trigger for balancing cpu
double cappacityTriggerBW = 0.9;      // max capacity trigger for balancing bandwidth
double cappacityTriggerRAM = 0.9;     // max capacity trigger for balancing ram
double highCappacityTriggerCPU =
    0.8; // capacity at which considerd almost full. should be less than cappacityTriggerCPU
double highCappacityTriggerBW =
    0.8; // capacity at which considerd almost full. should be less than cappacityTriggerBW
double highCappacityTriggerRAM =
    0.8; // capacity at which considerd almost full. should be less than cappacityTriggerRAM
double lowCappacityTriggerCPU =
    0.3; // capacity at which considerd almost empty. should be less than cappacityTriggerCPU
double lowCappacityTriggerBW =
    0.3; // capacity at which considerd almost empty. should be less than cappacityTriggerBW
double lowCappacityTriggerRAM =
    0.3; // capacity at which considerd almost empty. should be less than cappacityTriggerRAM
int balancingInterval = 1000;
int serverMonitorLimit;

// file save and loading vars
std::string const fileloc = "config.txt";
tthread::thread *saveTimer;
std::time_t prevconfigChange; // time of last config change
std::time_t prevSaveTime;     // time of last save

// timer vars
int prometheusMaxTimeDiff = 180; // time prometheusnodes stay in system
int prometheusTimeInterval = 10; // time prometheusnodes receive data
int saveTimeInterval = 5;        // time to save after config change in minutes
std::time_t now;

// authentication storage
std::map<std::string, std::pair<std::string, std::string> > userAuth; // username: (passhash, salt)
std::set<std::string> bearerTokens;
std::string passHash;
std::set<std::string> whitelist;
std::map<std::string, std::time_t> activeSalts;

std::string passphrase;
std::string fallback;
std::string myName;
tthread::mutex fileMutex;

prometheusDataNode lastPromethNode;
std::map<time_t, prometheusDataNode> prometheusData;

/**
 * creates new prometheus data node every prometheusTimeInterval
 */
void prometheusTimer(void *){
  while (cfg->is_active){
    // create new data node
    lastPromethNode = prometheusDataNode();
    time(&now);

    std::map<time_t, prometheusDataNode>::reverse_iterator it = prometheusData.rbegin();
    // remove old data
    while (it != prometheusData.rend()){
      double timeDiff = difftime(now, (*it).first);
      if (timeDiff >= 60 * prometheusMaxTimeDiff){
        prometheusData.erase((*it).first);
        it = prometheusData.rbegin();
      }else{
        it++;
      }
    }
    // add new data node to data collection
    prometheusData.insert(std::pair<time_t, prometheusDataNode>(now, lastPromethNode));

    sleep(prometheusTimeInterval * 60);
  }
}
/**
 * return JSON with all prometheus data nodes
 */
JSON::Value handlePrometheus(){
  JSON::Value res;
  for (std::map<time_t, prometheusDataNode>::iterator i = prometheusData.begin();
       i != prometheusData.end(); i++){
    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      JSON::Value serv;
      serv["number of reconnects"] = (*i).second.numReconnectServer[(*it)->name];
      serv["number of successful connects"] = (*i).second.numSuccessConnectServer[(*it)->name];
      serv["number of failed connects"] = (*i).second.numFailedConnectServer[(*it)->name];
      serv["number of new viewers to this server"] = (*i).second.numStreams[(*it)->name];
      res[(*i).first]["servers"][(std::string)(*it)->name] = serv;
    }
    for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
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
void timerAddViewer(void *){
  while (cfg->is_active){
    JSON::Value j;
    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state != STATE_ACTIVE) continue;
      std::string name = (*it)->name;
      j[addViewerKey][name] = (*it)->details->getAddBandwidth();
    }
    for (std::set<LoadBalancer *>::iterator it = loadBalancers.begin(); it != loadBalancers.end(); it++){
      (*it)->send(j.asString());
    }
    sleep(100);
  }
}

/**
 * redirects traffic away
 */
bool redirectServer(hostEntry *H, bool empty){
  int reduceCPU;
  int reduceRAM;
  int reduceBW;
  if (!empty){// decrement
    if (H->details->getRamMax() * cappacityTriggerRAM > H->details->getRamCurr())
      reduceRAM = cappacityTriggerRAMDec * H->details->getRamCurr();
    if (cappacityTriggerCPU * 1000 < H->details->getCpu())
      reduceCPU = cappacityTriggerCPUDec * H->details->getCpu();
    if (cappacityTriggerBW * H->details->getAvailBandwidth() < H->details->getCurrBandwidth())
      reduceBW = cappacitytriggerBWDec * H->details->getCurrBandwidth();
  }else{// remove all users
    reduceRAM = H->details->getRamCurr();
    reduceCPU = H->details->getCpu();
    reduceBW = H->details->getCurrBandwidth();
  }
  std::map<int, hostEntry *> lbw;
  // find host with lowest bw usage
  for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
    if ((*it)->state != STATE_ACTIVE) continue;
    if (highCappacityTriggerBW * (*it)->details->getAvailBandwidth() > ((*it)->details->getCurrBandwidth())){
      lbw.insert(std::pair<uint64_t, hostEntry *>((*it)->details->getCurrBandwidth(), *it));
    }
  }
  if (!lbw.size()) return false;
  std::map<int, hostEntry *>::iterator i = lbw.begin();

  while (i != lbw.end() && (reduceCPU != 0 || reduceBW != 0 || reduceRAM != 0)){// redirect until it finished or can't
    int balancableCpu = highCappacityTriggerCPU * 1000 - (*i).second->details->getCpu();
    int balancableRam = highCappacityTriggerRAM * (*i).second->details->getRamMax() -
                        (*i).second->details->getRamCurr();
    if (balancableCpu > 0 && 0 < balancableRam){
      int balancableBW = highCappacityTriggerBW * (*i).second->details->availBandwidth -
                         (*i).second->details->getCurrBandwidth();

      balancableBW = 100;

      H->details->balanceCPU = balancableCpu;
      H->details->balanceBW = balancableBW;
      H->details->balanceRAM = balancableRam;
      H->details->balanceRedirect = (*i).second->name;
      reduceBW -= balancableBW;
      reduceCPU -= balancableCpu;
      reduceRAM -= balancableRam;
      if (reduceCPU == 0 && reduceBW == 0 && reduceRAM == 0){return true;}
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
  for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
    if (!found && (*it)->state == STATE_ONLINE && !(*it)->standByLock){
      removeStandBy(*it);
      found = true;
    }else if ((*it)->state == STATE_ONLINE)
      counter++;
  }
  if (counter < minstandby){
    JSON::Value serverData;
    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->details){
        serverData[(const char *)((*it)->name)] = (*it)->details->getServerData();
      }
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
void reduceServer(hostEntry *H){
  setStandBy(H, false);
  int counter = 0;
  redirectServer(H, true);
  for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
    if ((*it)->state == STATE_ONLINE && !(*it)->standByLock) counter++;
  }
  if (counter > maxstandby){
    JSON::Value serverData;
    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      serverData[(const char *)((*it)->name)] = (*it)->details->getServerData();
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
void checkNeedRedirect(void *){
  while (cfg->is_active){
    // check if redirect is needed
    bool balancing = false;
    for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
      if ((*it)->state != STATE_ACTIVE) continue;
      if ((*it)->details->getRamMax() * cappacityTriggerRAM > (*it)->details->getRamCurr() ||
          cappacityTriggerCPU * 1000 < (*it)->details->getCpu() ||
          cappacityTriggerBW * (*it)->details->getAvailBandwidth() < (*it)->details->getCurrBandwidth()){
        balancing = redirectServer(*it, false);
      }
    }

    if (!balancing){// dont trigger when still balancing
      // check if reaching capacity
      std::set<hostEntry *> highCapacity;
      std::set<hostEntry *> lowCapacity;
      int counter = 0;
      for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
        if ((*it)->state != STATE_ACTIVE) continue;
        counter++;
        if (highCappacityTriggerCPU * 1000 < (*it)->details->getCpu()){highCapacity.insert(*it);}
        if (highCappacityTriggerRAM * (*it)->details->getRamMax() < (*it)->details->getRamCurr()){
          highCapacity.insert(*it);
        }
        if (highCappacityTriggerBW * (*it)->details->getAvailBandwidth() < (*it)->details->getCurrBandwidth()){
          highCapacity.insert(*it);
        }
        if (lowCappacityTriggerCPU * 1000 > (*it)->details->getCpu()){lowCapacity.insert(*it);}
        if (lowCappacityTriggerRAM * (*it)->details->getRamMax() > (*it)->details->getRamCurr()){
          lowCapacity.insert(*it);
        }
        if (lowCappacityTriggerBW * (*it)->details->getAvailBandwidth() > (*it)->details->getCurrBandwidth()){
          lowCapacity.insert(*it);
        }
      }
      // check if too much capacity
      if (lowCapacity.size() > 1){reduceServer(*lowCapacity.begin());}
      // check if too little capacity
      if (lowCapacity.size() == 0 && highCapacity.size() == counter){extraServer();}
    }

    sleep(balancingInterval);
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
  ((hostDetailsCalc *)(entry->details))->host = url.host;
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
        if (lastPromethNode.numFailedConnectServer.count(entry->name)){
          tmp = lastPromethNode.numFailedConnectServer.at(entry->name);
        }
        lastPromethNode.numFailedConnectServer.insert(std::pair<std::string, int>(entry->name, tmp + 1));
        FAIL_MSG("Can't decode server %s load information", url.host.c_str());
        ((hostDetailsCalc *)(entry->details))->badNess();
        DL.getSocket().close();
        down = true;
        entry->state = STATE_ERROR;
      }else{
        if (down){
          std::string ipStr;
          Socket::hostBytesToStr(DL.getSocket().getBinHost().data(), 16, ipStr);
          WARN_MSG("Connection established with %s (%s)", url.host.c_str(), ipStr.c_str());
          memcpy(((hostDetailsCalc *)(entry->details))->binHost, DL.getSocket().getBinHost().data(), 16);
          entry->state = STATE_ONLINE;
          down = false;
          int tmp = 0;
          if (lastPromethNode.numReconnectServer.count(entry->name)){
            tmp = lastPromethNode.numReconnectServer.at(entry->name);
          }
          lastPromethNode.numReconnectServer.insert(std::pair<std::string, int>(entry->name, tmp + 1));
        }
        ((hostDetailsCalc *)(entry->details))->update(servData);
        int tmp = 0;
        if (lastPromethNode.numSuccessConnectServer.count(entry->name)){
          tmp = lastPromethNode.numSuccessConnectServer.at(entry->name);
        }
        lastPromethNode.numSuccessConnectServer.insert(std::pair<std::string, int>(entry->name, tmp + 1));
      }
    }else{
      int tmp = 0;
      if (lastPromethNode.numFailedConnectServer.count(entry->name)){
        tmp = lastPromethNode.numFailedConnectServer.at(entry->name);
      }
      lastPromethNode.numFailedConnectServer.insert(std::pair<std::string, int>(entry->name, tmp + 1));
      FAIL_MSG("Can't retrieve server %s load information", url.host.c_str());
      ((hostDetailsCalc *)(entry->details))->badNess();
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
  H.details = (hostDetails *)new hostDetailsCalc(H.name);
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

  hostEntry *H = new hostEntry();
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
  if (H.state == STATE_BOOT){
    while (H.state != STATE_ONLINE){}
  }
  H.state = STATE_GODOWN;
  INFO_MSG("Stopping monitoring %s", H.name);
  if (H.thread){
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
  hosts.erase(&H);
  delete &H;
}

/// Fills the given map with the given JSON string of tag adjustments
void fillTagAdjust(std::map<std::string, int32_t> &tags, const std::string &adjust){
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
  for (int i = 0; i < SALTSIZE; i++){out += alphbet[rand() % alphbet.size()];}
  std::time_t t;
  time(&t);
  activeSalts.insert(std::pair<std::string, std::time_t>(out, t));
  return out;
}

/**
 * \returns the identifiers of the load balancers that need to monitor the server in \param H
 */
std::set<std::string> hostNeedsMonitoring(hostEntry H){
  int num = 0; // find start position
  std::set<std::string> hostnames;
  for (std::set<hostEntry *>::iterator i = hosts.begin(); i != hosts.end(); i++){
    hostnames.insert((*i)->name);
  }
  // get offset
  for (std::set<std::string>::iterator i = hostnames.begin(); i != hostnames.end(); i++){
    if (H.name == (*i)) break;
    num++;
  }
  // find indexes
  int trigger = hostnames.size() / identifiers.size();
  if (trigger < 1){trigger = 1;}
  std::set<int> indexs;
  for (int j = 0; j < serverMonitorLimit; j++){
    indexs.insert((num / trigger + j) % identifiers.size());
  }
  // find identifiers
  std::set<std::string> ret;
  std::set<int>::iterator i = indexs.begin();
  for (int x = 0; x < serverMonitorLimit && i != indexs.end(); x++){
    std::set<std::string>::iterator it = identifiers.begin();
    std::advance(it, (*i));
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
  // check for monitoring changes
  std::set<hostEntry *>::iterator it = hosts.begin();
  while (it != hosts.end()){
    std::set<std::string> idents = hostNeedsMonitoring(*(*it));
    std::set<std::string>::iterator i = idents.find(identifier);
    if (i != idents.end()){
      if ((*it)->thread == 0){// check monitored
        std::string name = ((*it)->name);

        // delete old host
        cleanupHost(**it);

        // create new host
        hostEntry *e = new hostEntry();
        initHost(*e, name);
        hosts.insert(e);

        // reset itterator
        it = hosts.begin();
      }else
        it++;
    }else if ((*it)->thread != 0 || (*it)->details == 0){// check not monitored
      // delete old host
      std::string name((*it)->name);

      cleanupHost(**it);

      // create new host
      initForeignHost(name);

      // reset iterator
      it = hosts.begin();
    }else{
      it++;
    }
  }
}
