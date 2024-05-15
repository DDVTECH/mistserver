/// \file controller.cpp
/// Contains all code for the controller executable.

#include "controller_api.h"
#include "controller_external_writers.h"
#include "controller_capabilities.h"
#include "controller_connectors.h"
#include "controller_push.h"
#include "controller_statistics.h"
#include "controller_storage.h"
#include "controller_streams.h"
#include "controller_variables.h"
#include <ctime>
#include <fstream> //for ram space check
#include <iostream>
#include <mist/auth.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/procs.h>
#include <mist/shared_memory.h>
#include <mist/socket.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/tinythread.h>
#include <mist/util.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/statvfs.h> //for shm space check
#include <sys/wait.h>
#include "controller_updater.h"
#include "controller_uplink.h"
#include <mist/triggers.h>

#ifndef COMPILED_USERNAME
#define COMPILED_USERNAME ""
#define COMPILED_PASSWORD ""
#endif

uint64_t lastConfRead = 0;

/// the following function is a simple check if the user wants to proceed to fix (y), ignore (n) or
/// abort on (a) a question
static inline char yna(std::string &user_input){
  switch (user_input[0]){
  case 'y':
  case 'Y': return 'y'; break;
  case 'n':
  case 'N': return 'n'; break;
  case 'a':
  case 'A': return 'a'; break;
  case 't':
  case 'T': return 't'; break;
  default: return 'x'; break;
  }
}

/// createAccount accepts a string in the form of username:account
/// and creates an account.
void createAccount(std::string account){
  if (account.size() > 0){
    size_t colon = account.find(':');
    if (colon != std::string::npos && colon != 0 && colon != account.size()){
      std::string uname = account.substr(0, colon);
      std::string pword = account.substr(colon + 1, std::string::npos);
      Controller::Log("CONF", "Created account " + uname + " through console interface");
      Controller::Storage["account"][uname]["password"] = Secure::md5(pword);
    }
  }
}

/// Bitmask:
/// 1 = Auto-read config from disk
/// 2 = Auto-write config to disk
static int configrw;

/// Status monitoring thread.
/// Checks status of "protocols" (listening outputs)
/// Updates config from disk when changed
/// Writes config to disk after some time of no changes
void statusMonitor(void *np){
  Controller::loadActiveConnectors();
  while (Controller::conf.is_active){

    // Check configuration file last changed time
    uint64_t confTime = Util::getFileUnixTime(Controller::conf.getString("configFile"));
    if (Controller::Storage.isMember("config_split")){
      jsonForEach(Controller::Storage["config_split"], cs){
        if (cs->isString()){
          uint64_t subTime = Util::getFileUnixTime(cs->asStringRef());
          if (subTime && subTime > confTime){confTime = subTime;}
        }
      }
    }
    // If we recently wrote, assume we know the contents since that time, too.
    if (lastConfRead < Controller::lastConfigWrite){lastConfRead = Controller::lastConfigWrite;}
    // If the config has changed, update Controller::lastConfigChange
    {
      JSON::Value currConfig;
      Controller::getConfigAsWritten(currConfig);
      if (Controller::lastConfigSeen != currConfig){
        Controller::lastConfigChange = Util::epoch();
        Controller::lastConfigSeen = currConfig;
      }
    }
    if (configrw & 1){
      // Read from disk if they are newer than our last read
      if (confTime && confTime > lastConfRead){
        INFO_MSG("Configuration files changed - reloading configuration from disk");
        tthread::lock_guard<tthread::mutex> guard(Controller::configMutex);
        Controller::readConfigFromDisk();
        lastConfRead = Controller::lastConfigChange;
      }
    }
    if (configrw & 2){
      // Write to disk if we have made no changes in the last 60 seconds and the files are older than the last change
      if (Controller::lastConfigChange > Controller::lastConfigWrite && Controller::lastConfigChange < Util::epoch() - 60){
        tthread::lock_guard<tthread::mutex> guard(Controller::configMutex);
        Controller::writeConfigToDisk();
        if (lastConfRead < Controller::lastConfigWrite){lastConfRead = Controller::lastConfigWrite;}
      }
    }

    { // this scope prevents the configMutex from being locked constantly
      tthread::lock_guard<tthread::mutex> guard(Controller::configMutex);
      // checks online protocols, reports changes to status
      if (Controller::CheckProtocols(Controller::Storage["config"]["protocols"], Controller::capabilities)){
        Controller::writeProtocols();
      }
      // checks stream statuses, reports changes to status
      Controller::CheckAllStreams(Controller::Storage["streams"]);
    }

    Util::sleep(3000); // wait at most 3 seconds
  }
  if (Util::Config::is_restarting){
    Controller::prepareActiveConnectorsForReload();
  }else{
    Controller::prepareActiveConnectorsForShutdown();
  }
}

static unsigned long mix(unsigned long a, unsigned long b, unsigned long c){
  a = a - b;
  a = a - c;
  a = a ^ (c >> 13);
  b = b - c;
  b = b - a;
  b = b ^ (a << 8);
  c = c - a;
  c = c - b;
  c = c ^ (b >> 13);
  a = a - b;
  a = a - c;
  a = a ^ (c >> 12);
  b = b - c;
  b = b - a;
  b = b ^ (a << 16);
  c = c - a;
  c = c - b;
  c = c ^ (b >> 5);
  a = a - b;
  a = a - c;
  a = a ^ (c >> 3);
  b = b - c;
  b = b - a;
  b = b ^ (a << 10);
  c = c - a;
  c = c - b;
  c = c ^ (b >> 15);
  return c;
}

void handleUSR1(int signum, siginfo_t *sigInfo, void *ignore){
  Controller::Log("CONF", "USR1 received - restarting controller");
  Util::Config::is_restarting = true;
  raise(SIGINT); // trigger restart
}

void handleUSR1Parent(int signum, siginfo_t *sigInfo, void *ignore){
  Controller::Log("CONF", "USR1 received - passing on to child");
  Util::Config::is_restarting = true;
}

bool interactiveFirstTimeSetup(){
  // check for username
  if (!Controller::Storage.isMember("account") || Controller::Storage["account"].size() < 1){
    std::string in_string = "";
    while (yna(in_string) == 'x' && Controller::conf.is_active){
      std::cout << "Account not set, do you want to create an account? (y)es, (n)o, (a)bort: ";
      std::cout.flush();
      std::getline(std::cin, in_string);
      switch (yna(in_string)){
      case 'y':{
        // create account
        std::string usr_string = "";
        while (!(Controller::Storage.isMember("account") && Controller::Storage["account"].size() > 0) &&
               Controller::conf.is_active){
          std::cout << "Please type in the username, a colon and a password in the following "
                       "format; username:password"
                    << std::endl
                    << ": ";
          std::cout.flush();
          std::getline(std::cin, usr_string);
          createAccount(usr_string);
        }
      }break;
      case 'a': return false; // abort bootup
      case 't':{
        createAccount("test:test");
        if ((Controller::capabilities["connectors"].size()) &&
            (!Controller::Storage.isMember("config") || !Controller::Storage["config"].isMember("protocols") ||
             Controller::Storage["config"]["protocols"].size() < 1)){
          // create protocols
          jsonForEach(Controller::capabilities["connectors"], it){
            if (!it->isMember("required")){
              JSON::Value newProtocol;
              newProtocol["connector"] = it.key();
              Controller::Storage["config"]["protocols"].append(newProtocol);
            }
          }
        }
      }break;
      }
    }
  }
  // check for protocols
  if ((Controller::capabilities["connectors"].size()) &&
      (!Controller::Storage.isMember("config") || !Controller::Storage["config"].isMember("protocols") ||
       Controller::Storage["config"]["protocols"].size() < 1)){
    std::string in_string = "";
    while (yna(in_string) == 'x' && Controller::conf.is_active){
      std::cout << "Protocols not set, do you want to enable default protocols? (y)es, (n)o, (a)bort: ";
      std::cout.flush();
      std::getline(std::cin, in_string);
      if (yna(in_string) == 'y'){
        // create protocols
        jsonForEach(Controller::capabilities["connectors"], it){
          if (!it->isMember("required")){
            JSON::Value newProtocol;
            newProtocol["connector"] = it.key();
            Controller::Storage["config"]["protocols"].append(newProtocol);
          }
        }
      }else if (yna(in_string) == 'a'){
        // abort controller startup
        return false;
      }
    }
  }
  return true;
}

///\brief The main loop for the controller.
int main_loop(int argc, char **argv){
  {
    struct sigaction new_action;
    new_action.sa_sigaction = handleUSR1;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGUSR1, &new_action, NULL);
  }
  Controller::isTerminal = Controller::isColorized = isatty(fileno(stdout));
  if (!isatty(fileno(stdin))){Controller::isTerminal = false;}
  Controller::Storage = JSON::fromFile("config.json");
  JSON::Value stored_port =
      JSON::fromString("{\"long\":\"port\", \"short\":\"p\", \"arg\":\"integer\", \"help\":\"TCP "
                       "port to listen on.\"}");
  stored_port["default"] = Controller::Storage["config"]["controller"]["port"];
  if (!stored_port["default"]){stored_port["default"] = 4242;}
  JSON::Value stored_interface = JSON::fromString(
      "{\"long\":\"interface\", \"short\":\"i\", \"arg\":\"string\", \"help\":\"Interface address "
      "to listen on, or 0.0.0.0 for all available interfaces.\"}");
  stored_interface["default"] = Controller::Storage["config"]["controller"]["interface"];
  if (!stored_interface["default"]){stored_interface["default"] = "0.0.0.0";}
  JSON::Value stored_user =
      JSON::fromString("{\"long\":\"username\", \"short\":\"u\", \"arg\":\"string\", "
                       "\"help\":\"Username to transfer privileges to, default is root.\"}");
  stored_user["default"] = Controller::Storage["config"]["controller"]["username"];
  if (!stored_user["default"]){stored_user["default"] = "root";}
  Controller::conf.addOption("port", stored_port);
  Controller::conf.addOption("interface", stored_interface);
  Controller::conf.addOption("username", stored_user);
  Controller::conf.addOption(
      "account", JSON::fromString("{\"long\":\"account\", \"short\":\"a\", \"arg\":\"string\" "
                                  "\"default\":\"\", \"help\":\"A username:password string to "
                                  "create a new account with.\"}"));
  Controller::conf.addOption(
      "logfile", JSON::fromString("{\"long\":\"logfile\", \"short\":\"L\", \"arg\":\"string\" "
                                  "\"default\":\"\",\"help\":\"Redirect all standard output to a "
                                  "log file, provided with an argument.\"}"));
  Controller::conf.addOption(
      "accesslog", JSON::fromString("{\"long\":\"accesslog\", \"short\":\"A\", \"arg\":\"string\" "
                                    "\"default\":\"LOG\",\"help\":\"Where to write the access log. "
                                    "If set to 'LOG' (the default), writes to wherever the log is "
                                    "written to. If empty, access logging is turned off. "
                                    "Otherwise, writes to the given filename.\"}"));
  Controller::conf.addOption(
      "configFile", JSON::fromString("{\"long\":\"config\", \"short\":\"c\", \"arg\":\"string\" "
                                     "\"default\":\"config.json\", \"help\":\"Specify a config "
                                     "file other than default.\"}"));

  Controller::conf.addOption(
      "configrw", JSON::fromString("{\"long\":\"configrw\", \"short\":\"C\", \"arg\":\"string\" "
                                     "\"default\":\"rw\", \"help\":\"If 'r', read config changes from disk. If 'w', writes them to disk after 60 seconds of no changes. If 'rw', does both (default). In all other cases does neither.\"}"));
#ifdef UPDATER
  Controller::conf.addOption(
      "update", JSON::fromString("{\"default\":0, \"help\":\"Check for and install updates before "
                                 "starting.\", \"short\":\"D\", \"long\":\"update\"}")); /*LTS*/
#endif
  Controller::conf.addOption(
      "uplink",
      JSON::fromString("{\"default\":\"\", \"arg\":\"string\", \"help\":\"MistSteward uplink host "
                       "and port.\", \"short\":\"U\", \"long\":\"uplink\"}")); /*LTS*/
  Controller::conf.addOption("uplink-name", JSON::fromString("{\"default\":\"" COMPILED_USERNAME "\", \"arg\":\"string\", \"help\":\"MistSteward "
                                                             "uplink username.\", \"short\":\"N\", "
                                                             "\"long\":\"uplink-name\"}")); /*LTS*/
  Controller::conf.addOption("uplink-pass", JSON::fromString("{\"default\":\"" COMPILED_PASSWORD "\", \"arg\":\"string\", \"help\":\"MistSteward "
                                                             "uplink password.\", \"short\":\"P\", "
                                                             "\"long\":\"uplink-pass\"}")); /*LTS*/
  Controller::conf.addOption(
      "prometheus",
      JSON::fromString("{\"long\":\"prometheus\", \"short\":\"S\", \"arg\":\"string\" "
                       "\"default\":\"\", \"help\":\"If set, allows collecting of Prometheus-style "
                       "stats on the given path over the API port.\"}"));
  Controller::conf.parseArgs(argc, argv);
  if (Controller::conf.getString("logfile") != ""){
    // open logfile, dup stdout to logfile
    int output = open(Controller::conf.getString("logfile").c_str(), O_APPEND | O_CREAT | O_WRONLY, S_IRWXU);
    if (output < 0){
      DEBUG_MSG(DLVL_ERROR, "Could not redirect output to %s: %s",
                Controller::conf.getString("logfile").c_str(), strerror(errno));
      return 7;
    }else{
      Controller::isTerminal = Controller::isColorized = false;
      dup2(output, STDOUT_FILENO);
      dup2(output, STDERR_FILENO);
      time_t rawtime;
      struct tm *timeinfo;
      struct tm tmptime;
      char buffer[25];
      time(&rawtime);
      timeinfo = localtime_r(&rawtime, &tmptime);
      strftime(buffer, 25, "%c", timeinfo);
      std::cerr << std::endl
                << std::endl
                << "!----" APPNAME " Started at " << buffer << " ----!" << std::endl;
    }
  }

  {// spawn thread that reads stderr of process
    std::string logPipe = Util::getTmpFolder() + "MstLog";
    if (mkfifo(logPipe.c_str(), S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH) != 0){
      if (errno != EEXIST){
        ERROR_MSG("Could not create log message pipe %s: %s", logPipe.c_str(), strerror(errno));
      }
    }
    int inFD = -1;
    if ((inFD = open(logPipe.c_str(), O_RDONLY | O_NONBLOCK)) == -1){
      ERROR_MSG("Could not open log message pipe %s: %s; falling back to unnamed pipe",
                logPipe.c_str(), strerror(errno));
      int pipeErr[2];
      if (pipe(pipeErr) >= 0){
        dup2(pipeErr[1], STDERR_FILENO); // cause stderr to write to the pipe
        close(pipeErr[1]);               // close the unneeded pipe file descriptor
        // Start reading log messages from the unnamed pipe
        Util::Procs::socketList.insert(pipeErr[0]); // Mark this FD as needing to be closed before forking
        tthread::thread msghandler(Controller::handleMsg, (void *)(((char *)0) + pipeErr[0]));
        msghandler.detach();
      }
    }else{
      // Set the read end to blocking mode
      int inFDflags = fcntl(inFD, F_GETFL, 0);
      fcntl(inFD, F_SETFL, inFDflags & (~O_NONBLOCK));
      // Start reading log messages from the named pipe
      Util::Procs::socketList.insert(inFD); // Mark this FD as needing to be closed before forking
      tthread::thread msghandler(Controller::handleMsg, (void *)(((char *)0) + inFD));
      msghandler.detach();
      // Attempt to open and redirect log messages to named pipe
      int outFD = -1;
      if (getenv("MIST_NO_PRETTY_LOGGING")) {
        WARN_MSG(
            "MIST_NO_PRETTY_LOGGING is active, printing lots of pipes");
      }
      else if ((outFD = open(logPipe.c_str(), O_WRONLY)) == -1){
        ERROR_MSG(
            "Could not open log message pipe %s for writing! %s; falling back to standard error",
            logPipe.c_str(), strerror(errno));
      }else{
        dup2(outFD, STDERR_FILENO); // cause stderr to write to the pipe
        close(outFD);               // close the unneeded pipe file descriptor
      }
    }
    setenv("MIST_CONTROL", "1", 0); // Signal in the environment that the controller handles all children
  }
  
  Controller::readConfigFromDisk();
  Controller::writeConfig();
  if (!Controller::conf.is_active){return 0;}
  Controller::checkAvailProtocols();
  Controller::checkAvailTriggers();
  Controller::writeCapabilities();
  Controller::updateBandwidthConfig();
  createAccount(Controller::conf.getString("account"));
  Controller::conf.activate(); // activate early, so threads aren't killed.

#if !defined(__CYGWIN__) && !defined(_WIN32)
  {
    uint64_t mem_total = 0, mem_free = 0, mem_bufcache = 0, shm_total = 0, shm_free = 0;
    std::ifstream meminfo("/proc/meminfo");
    if (meminfo){
      char line[300];
      while (meminfo.good()){
        meminfo.getline(line, 300);
        if (meminfo.fail()){
          // empty lines? ignore them, clear flags, continue
          if (!meminfo.eof()){
            meminfo.ignore();
            meminfo.clear();
          }
          continue;
        }
        long long int i;
        if (sscanf(line, "MemTotal : %lli kB", &i) == 1){mem_total = i;}
        if (sscanf(line, "MemFree : %lli kB", &i) == 1){mem_free = i;}
        if (sscanf(line, "Buffers : %lli kB", &i) == 1){mem_bufcache += i;}
        if (sscanf(line, "Cached : %lli kB", &i) == 1){mem_bufcache += i;}
      }
    }
    struct statvfs shmd;
    IPC::sharedPage tmpCapa(SHM_CAPA, DEFAULT_CONF_PAGE_SIZE, false, false);
    if (tmpCapa.mapped && tmpCapa.handle){
      fstatvfs(tmpCapa.handle, &shmd);
      shm_free = (shmd.f_bfree * shmd.f_frsize) / 1024;
      shm_total = (shmd.f_blocks * shmd.f_frsize) / 1024;
    }

    if (mem_free + mem_bufcache < 1024 * 1024){
      WARN_MSG("You have very little free RAM available (%" PRIu64
               " MiB). While Mist will run just fine with this amount, do note that random crashes "
               "may occur should you ever run out of free RAM. Please be pro-active and keep an "
               "eye on the RAM usage!", (mem_free + mem_bufcache)/1024);
    }
    if (shm_free < 1024 * 1024 && mem_total > 1024 * 1024 * 1.12){
      WARN_MSG("You have very little shared memory available (%" PRIu64
               " MiB). Mist heavily relies on shared memory: please ensure your shared memory is "
               "set to a high value, preferably ~95%% of your total available RAM.",
               shm_free / 1024);
      if (shm_total == 65536){
        WARN_MSG("Tip: If you are using docker, e.g. add the `--shm-size=%" PRIu64
                 "m` parameter to your `docker run` command to fix this.",
                 (uint64_t)(mem_total * 0.95 / 1024));
      }else{
        WARN_MSG("Tip: In most cases, you can change the shared memory size by running `mount -o "
                 "remount,size=%" PRIu64
                 "m /dev/shm` as root. Doing this automatically every boot depends on your "
                 "distribution: please check your distro's documentation for instructions.",
                 (uint64_t)(mem_total * 0.95 / 1024));
      }
    }else if (shm_total <= mem_total / 2){
      WARN_MSG("Your shared memory is half or less of your RAM (%" PRIu64 " / %" PRIu64
               " MiB). Mist heavily relies on shared memory: please ensure your shared memory is "
               "set to a high value, preferably ~95%% of your total available RAM.",
               shm_total / 1024, mem_total / 1024);
      if (shm_total == 65536){
        WARN_MSG("Tip: If you are using docker, e.g. add the `--shm-size=%" PRIu64
                 "m` parameter to your `docker run` command to fix this.",
                 (uint64_t)(mem_total * 0.95 / 1024));
      }else{
        WARN_MSG("Tip: In most cases, you can change the shared memory size by running `mount -o "
                 "remount,size=%" PRIu64
                 "m /dev/shm` as root. Doing this automatically every boot depends on your "
                 "distribution: please check your distro's documentation for instructions.",
                 (uint64_t)(mem_total * 0.95 / 1024));
      }
    }
  }
#endif

  // if a terminal is connected, check for first time setup
  if (Controller::isTerminal){
    if (!interactiveFirstTimeSetup()){return 0;}
  }

  // Check if we have a usable server, if not, print messages with helpful hints
  {
    std::string web_port = JSON::Value(Controller::conf.getInteger("port")).asString();
    // check for username
    if (!Controller::Storage.isMember("account") || Controller::Storage["account"].size() < 1){
      Controller::Log("CONF",
                      "No login configured. To create one, attempt to login through the web "
                      "interface on port " +
                          web_port + " and follow the instructions.");
    }
    // check for protocols
    if (!Controller::Storage.isMember("config") || !Controller::Storage["config"].isMember("protocols") ||
        Controller::Storage["config"]["protocols"].size() < 1){
      Controller::Log("CONF", "No protocols enabled, remember to set them up through the web "
                              "interface on port " +
                                  web_port + " or API.");
    }
    // check for streams - regardless of logfile setting
    if (!Controller::Storage.isMember("streams") || Controller::Storage["streams"].size() < 1){
      Controller::Log("CONF", "No streams configured, remember to set up streams through the web "
                              "interface on port " +
                                  web_port + " or API.");
    }
  }

  // Generate instanceId once per boot.
  if (Controller::instanceId == ""){
    srand(mix(clock(), time(0), getpid()));
    do{
      Controller::instanceId += (char)(64 + rand() % 62);
    }while (Controller::instanceId.size() < 16);
  }

  // Set configrw to correct value
  {
    configrw = 0;
    if (Controller::conf.getString("configrw") == "r"){
      configrw = 1;
    }else if (Controller::conf.getString("configrw") == "w"){
      configrw = 2;
    }else if (Controller::conf.getString("configrw") == "rw"){
      configrw = 3;
    }
  }

  // start stats thread
  tthread::thread statsThread(Controller::SharedMemStats, &Controller::conf);
  // start monitoring thread
  tthread::thread monitorThread(statusMonitor, 0);
  // start UDP API thread
  tthread::thread UDPAPIThread(Controller::handleUDPAPI, 0);
  // start monitoring thread /*LTS*/
  tthread::thread uplinkThread(Controller::uplinkConnection, 0); /*LTS*/
  // start push checking thread
  tthread::thread pushThread(Controller::pushCheckLoop, 0);
  // start variable checking thread
  tthread::thread variableThread(Controller::variableCheckLoop, 0);
#ifdef UPDATER
  // start updater thread
  tthread::thread updaterThread(Controller::updateThread, 0);
#endif

  Controller::Log("CONF", "Controller started");
  /*LTS-START*/
#ifdef UPDATER
  if (Controller::conf.getBool("update")){Controller::checkUpdates();}
#endif
  /*LTS-END*/

  // Init external writer config
  Controller::externalWritersToShm();

  // start main loop
  while (Controller::conf.is_active){
    Controller::conf.serveThreadedSocket(Controller::handleAPIConnection);
    // print shutdown reason
    std::string shutdown_reason;
    if (!Controller::conf.is_active){
      shutdown_reason = "user request (received shutdown signal)";
    }else{
      shutdown_reason = "socket problem (API port closed)";
    }
    if (Util::Config::is_restarting){shutdown_reason = "restart (on request)";}
/*LTS-START*/
    if (Triggers::shouldTrigger("SYSTEM_STOP")){
      if (!Triggers::doTrigger("SYSTEM_STOP", shutdown_reason)){
        Controller::conf.is_active = true;
        Util::Config::is_restarting = false;
        Util::sleep(1000);
      }else{
        Controller::conf.is_active = false;
        Controller::Log("CONF", "Controller shutting down because of " + shutdown_reason);
      }
    }else{
      /*LTS-END*/
      Controller::conf.is_active = false;
      Controller::Log("CONF", "Controller shutting down because of " + shutdown_reason);
      /*LTS-START*/
    }
    /*LTS-END*/
  }
  // join all joinable threads
  HIGH_MSG("Joining stats thread...");
  statsThread.join();
  HIGH_MSG("Joining monitor thread...");
  monitorThread.join();
  HIGH_MSG("Joining UDP API thread...");
  UDPAPIThread.join();
  /*LTS-START*/
  HIGH_MSG("Joining uplink thread...");
  uplinkThread.join();
  HIGH_MSG("Joining push thread...");
  pushThread.join();
  HIGH_MSG("Joining variable thread...");
  variableThread.join();
#ifdef UPDATER
  HIGH_MSG("Joining updater thread...");
  updaterThread.join();
#endif
  /*LTS-END*/
  // write config
  tthread::lock_guard<tthread::mutex> guard(Controller::logMutex);
  Controller::writeConfigToDisk(true);
  // stop all child processes
  Util::Procs::StopAll();
  // give everything some time to print messages
  Util::wait(100);
  std::cout << "Killed all processes, wrote config to disk. Exiting." << std::endl;
  if (Util::Config::is_restarting){return 42;}
  // close stderr to make the stderr reading thread exit
  close(STDERR_FILENO);
  return 0;
}

///\brief The controller angel process.
/// Starts a forked main_loop in a loop. Yes, you read that right.
int ControllerMain(int argc, char **argv){
  Util::Procs::setHandler(); // set child handler
  {
    struct sigaction new_action;
    new_action.sa_sigaction = handleUSR1Parent;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGUSR1, &new_action, NULL);
  }

  Controller::conf = Util::Config(argv[0]);
  Util::Config::binaryType = Util::CONTROLLER;
  Controller::conf.activate();
  if (getenv("ATHEIST")){return main_loop(argc, argv);}
  uint64_t reTimer = 0;
  while (Controller::conf.is_active){
    Util::Procs::fork_prepare();
    pid_t pid = fork();
    if (pid == 0){
      Util::Procs::fork_complete();
      return main_loop(argc, argv);
    }
    Util::Procs::fork_complete();
    if (pid == -1){
      FAIL_MSG("Unable to spawn controller process!");
      return 2;
    }
    // wait for the process to exit
    int status;
    while (waitpid(pid, &status, 0) != pid && errno == EINTR){
      if (Util::Config::is_restarting){
        Controller::conf.is_active = true;
        Util::Config::is_restarting = false;
        kill(pid, SIGUSR1);
      }
      if (!Controller::conf.is_active){
        INFO_MSG("Shutting down controller because of signal interrupt...");
        Util::Procs::Stop(pid);
      }
      continue;
    }
    // if the exit was clean, don't restart it
    if (WIFEXITED(status) && (WEXITSTATUS(status) == 0 && !Util::Config::is_restarting)){
      MEDIUM_MSG("Controller shut down cleanly");
      break;
    }
    if (WIFEXITED(status) && (WEXITSTATUS(status) == 42 || Util::Config::is_restarting)){
      WARN_MSG("Refreshing angel process for update");
      std::string myFile = Util::getMyPath() + "MistController";
      execvp(myFile.c_str(), argv);
      FAIL_MSG("Error restarting: %s", strerror(errno));
    }
    INFO_MSG("Controller uncleanly shut down! Restarting in %" PRIu64 "...", reTimer);
    Util::wait(reTimer);
    reTimer += 1000;
  }
  return 0;
}

#ifndef ONE_BINARY
int main(int argc, char **argv){
  return ControllerMain(argc, argv);
}
#endif
