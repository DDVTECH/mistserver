#include "api.h"
#include "communication_defines.h"
#include "util_load.h"
#include <mist/config.h>
#include <mist/util.h>
#include <string>
#include <mist/auth.h>

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

  std::string password = "default"; // set default password for load balancer communication
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

  if (myName.find(":") == std::string::npos){myName.append(":" + conf.getString("port"));}

  conf.activate();

  loadBalancers = std::set<LoadBalancer *>();
  serverMonitorLimit = 1;
  // setup saving
  saveTimer = 0;
  time(&prevSaveTime);
  // api login
  srand(time(0) + getpid()); // setup random num generator
  std::string salt = generateSalt();
  userAuth.insert(std::pair<std::string, std::pair<std::string, std::string> >(
      "admin", std::pair<std::string, std::string>(Secure::sha256("default" + salt), salt)));
  bearerTokens.insert("test1233");
  // add localhost to whitelist
  if (conf.getBool("localmode")){
    whitelist.insert("localhost");
    whitelist.insert("::1/128");
    whitelist.insert("127.0.0.1/24");
  }

  identifier = generateSalt();
  identifiers.insert(identifier);

  if (load){
    loadFile();
  }else{
    passHash = Secure::sha256(password);
  }

  std::map<std::string, tthread::thread *> threads;

  checkServerMonitors();

  new tthread::thread(timerAddViewer, NULL);
  new tthread::thread(checkNeedRedirect, NULL);
  new tthread::thread(prometheusTimer, NULL);
  conf.serveThreadedSocket(handleRequest);
  if (!conf.is_active){
    WARN_MSG("Load balancer shutting down; received shutdown signal");
  }else{
    WARN_MSG("Load balancer shutting down; socket problem");
  }
  conf.is_active = false;
  saveFile();

  // Join all threads
  for (std::set<hostEntry *>::iterator it = hosts.begin(); it != hosts.end(); it++){
    if (!(*it)->name[0]){continue;}
    (*it)->state = STATE_GODOWN;
  }
  for (std::set<hostEntry *>::iterator i = hosts.begin(); i != hosts.end(); i++){
    cleanupHost(**i);
  }
  std::set<LoadBalancer *>::iterator it = loadBalancers.begin();
  while (loadBalancers.size()){
    (*it)->send("close");
    (*it)->Go_Down = true;
    loadBalancers.erase(it);
    it = loadBalancers.begin();
  }
}
