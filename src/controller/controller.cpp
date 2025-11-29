#include "controller_api.h"
#include "controller_capabilities.h"
#include "controller_connectors.h"
#include "controller_external_writers.h"
#include "controller_push.h"
#include "controller_statistics.h"
#include "controller_storage.h"
#include "controller_streams.h"
#include "controller_updater.h"
#include "controller_uplink.h"
#include "controller_variables.h"

#include <mist/auth.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/ev.h>
#include <mist/http_parser.h>
#include <mist/procs.h>
#include <mist/shared_memory.h>
#include <mist/socket.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/triggers.h>
#include <mist/util.h>

#include <ctime>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <sstream>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>

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
      LOG_MSG("CONF", "Created account %s through console interface", uname.c_str());
      Controller::Storage["account"][uname]["password"] = Secure::md5(pword);
    }
  }
}

/// Bitmask:
/// 1 = Auto-read config from disk
/// 2 = Auto-write config to disk
static int configrw;

/// Status monitoring runner.
/// Checks status of "protocols" (listening outputs)
/// Updates config from disk when changed
/// Writes config to disk after some time of no changes
size_t statusMonitor() {
  // Check configuration file last changed time
  uint64_t confTime = Util::getFileUnixTime(Controller::conf.getString("configFile"));
  if (Controller::Storage.isMember("config_split")) {
    jsonForEach (Controller::Storage["config_split"], cs) {
      if (cs->isString()) {
        uint64_t subTime = Util::getFileUnixTime(cs->asStringRef());
        if (subTime && subTime > confTime) { confTime = subTime; }
      }
    }
  }
  // If we recently wrote, assume we know the contents since that time, too.
  if (lastConfRead < Controller::lastConfigWrite) { lastConfRead = Controller::lastConfigWrite; }
  // If the config has changed, update Controller::lastConfigChange
  {
    JSON::Value currConfig;
    Controller::getConfigAsWritten(currConfig);
    if (Controller::lastConfigSeen != currConfig) {
      Controller::lastConfigChange = Util::epoch();
      Controller::lastConfigSeen = currConfig;
    }
  }
  if (configrw & 1) {
    // Read from disk if they are newer than our last read
    if (confTime && confTime > lastConfRead) {
      INFO_MSG("Configuration files changed - reloading configuration from disk");
      std::lock_guard<std::mutex> guard(Controller::configMutex);
      Controller::readConfigFromDisk();
      lastConfRead = Controller::lastConfigChange;
    }
  }
  if (configrw & 2) {
    // Write to disk if we have made no changes in the last 60 seconds and the files are older than the last change
    if (Controller::lastConfigChange > Controller::lastConfigWrite && Controller::lastConfigChange < Util::epoch() - 60) {
      std::lock_guard<std::mutex> guard(Controller::configMutex);
      Controller::writeConfigToDisk();
      if (lastConfRead < Controller::lastConfigWrite) { lastConfRead = Controller::lastConfigWrite; }
    }
  }

  // checks online protocols, reports changes to status
  if (Controller::CheckProtocols(Controller::Storage["config"]["protocols"], Controller::capabilities)) {
    Controller::writeProtocols();
  }
  // checks stream status
  jsonForEach (Controller::Storage["streams"], jit) { Controller::checkStream(jit.key(), *jit); }
  return 3000;
}

void handleUSR1(int signum, siginfo_t *sigInfo, void *ignore){
  LOG_MSG("CONF", "USR1 received - restarting controller");
  Util::logExitReason(ER_CLEAN_RESTART, "USR1 received - restarting");
  Util::Config::is_restarting = true;
  raise(SIGINT); // trigger restart
}

void handleUSR1Parent(int signum, siginfo_t *sigInfo, void *ignore){
  LOG_MSG("CONF", "USR1 received - passing on to child");
  Util::Config::is_restarting = true;
}

bool interactiveFirstTimeSetup(){
  bool waited = false;
  // check for username
  if (!Controller::Storage.isMember("account") || Controller::Storage["account"].size() < 1){
    std::string in_string = "";
    while (yna(in_string) == 'x' && Controller::conf.is_active){
      if (!waited){
        Util::wait(1000);
        waited = true;
      }
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
             !Controller::Storage["config"]["protocols"].size())) {
          // create protocols
          jsonForEach(Controller::capabilities["connectors"], it){
            if (it->isMember("PUSHONLY")){continue;}
            if (it->isMember("NODEFAULT")){continue;}
            if (!it->isMember("required")){
              JSON::Value newProtocol;
              newProtocol["connector"] = it.key();
              newProtocol["debug"] = 4;
              Controller::Storage["config"]["protocols"].append(newProtocol);
            }
          }
        }
        Controller::Storage["streams"]["live"]["source"] = "push://";
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
      if (!waited){
        Util::wait(1000);
        waited = true;
      }
      std::cout << "Protocols not set, do you want to enable default protocols? (y)es, (n)o, (a)bort: ";
      std::cout.flush();
      std::getline(std::cin, in_string);
      if (yna(in_string) == 'y'){
        // create protocols
        jsonForEach(Controller::capabilities["connectors"], it){
          if (it->isMember("PUSHONLY")){continue;}
          if (it->isMember("NODEFAULT")){continue;}
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
  
  // We need to do this before we start the log reader, since the log reader might parse messages
  // from pushes, which block if this list is not read yet.
  Controller::initPushCheck();

  int logInput = -1;
  { // spawn thread that reads stderr of process
    std::string logPipe = Util::getTmpFolder() + "MstLog";
    if (mkfifo(logPipe.c_str(), S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH) != 0) {
      if (errno != EEXIST) { ERROR_MSG("Could not create log message pipe %s: %s", logPipe.c_str(), strerror(errno)); }
    }
    if ((logInput = open(logPipe.c_str(), O_RDONLY | O_NONBLOCK)) == -1) {
      ERROR_MSG("Could not open log message pipe %s: %s; falling back to unnamed pipe", logPipe.c_str(), strerror(errno));
      int pipeErr[2];
      if (!pipe(pipeErr)) {
        dup2(pipeErr[1], STDERR_FILENO); // cause stderr to write to the pipe
        close(pipeErr[1]); // close the unneeded pipe file descriptor
        logInput = pipeErr[0];
      }
    } else {
      // Set the read end to blocking mode
      int inFDflags = fcntl(logInput, F_GETFL, 0);
      fcntl(logInput, F_SETFL, inFDflags & (~O_NONBLOCK));

      // Attempt to open and redirect log messages to named pipe
      int outFD = -1;
      if ((outFD = open(logPipe.c_str(), O_WRONLY)) == -1) {
        ERROR_MSG("Can't write to log pipe %s: %s; falling back to standard error", logPipe.c_str(), strerror(errno));
      } else {
        dup2(outFD, STDERR_FILENO); // cause stderr to write to the pipe
        close(outFD); // close the unneeded pipe file descriptor
      }
    }
  }
  if (logInput == -1) {
    FAIL_MSG("Could nog start logger; aborting run!");
    return 3;
  }
  // Start reading log messages
  Util::Procs::socketList.insert(logInput); // Mark this FD as needing to be closed before forking
  std::thread logThread(Controller::handleMsg, logInput);
  setenv("MIST_CONTROL", "1", 0); // Signal in the environment that the controller handles all children

#ifdef __CYGWIN__
  // Wipe shared memory, unless NO_WIPE_SHM is set
  if (!getenv("NO_WIPE_SHM")){
    Util::Config::wipeShm();
    setenv("NO_WIPE_SHM", "1", 1);
  }
#endif

  Controller::E.setup();

  Controller::readConfigFromDisk();
  Controller::writeConfig();
  if (!Controller::conf.is_active){return 0;}
  Controller::checkAvailProtocols();
  Controller::checkAvailTriggers();
  Controller::writeCapabilities();
  Controller::updateBandwidthConfig();
  createAccount(Controller::conf.getString("account"));
  Controller::conf.setMutexAborter(&Controller::configMutex);
  
  // Check initial load stats
  Controller::updateLoad();
  // Warn if we're running with very little resources available
  Controller::checkSpecs();


  // if a terminal is connected, check for first time setup
  if (Controller::isTerminal){
    if (!interactiveFirstTimeSetup()){return 0;}
  }

  // Generate instanceId once per boot.
  if (Controller::instanceId == ""){
    do{
      uint32_t ranNum;
      Util::getRandomBytes(&ranNum, 4);
      Controller::instanceId += (char)(64 + ranNum % 62);
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

  // Listen on API port
  Socket::Server apiSock;
  if (!Controller::conf.setupServerSocket(apiSock)) {
    Util::logExitReason(ER_READ_START_FAILURE, "Could not listen on API port - aborting");
    return 0;
  }

  // Check if we have a usable server, if not, print messages with helpful hints
  {
    std::string msg;
    // check for username
    if (!Controller::Storage.isMember("account") || Controller::Storage["account"].size() < 1) {
      msg += "No login configured. ";
    }
    // check for protocols
    if (!Controller::Storage.isMember("config") || !Controller::Storage["config"].isMember("protocols") ||
        Controller::Storage["config"]["protocols"].size() < 1) {
      msg += "No protocols enabled. ";
    }
    // check for streams - regardless of logfile setting
    if (!Controller::Storage.isMember("streams") || Controller::Storage["streams"].size() < 1) {
      msg += "No streams configured. ";
    }
    if (msg.size()) {
      msg += "This can be configured through the web interface on port " +
        JSON::Value(Controller::conf.boundServer.port()).asString() + " or through the API";
      LOG_MSG("CONF", "%s", msg.c_str());
    }
  }

  Controller::initStats();
  Controller::initStorage();
  Controller::loadActiveConnectors();
  Controller::externalWritersToShm();

  Controller::E.addInterval(Controller::jwkUriCheck, 1000);
  Controller::E.addInterval(Controller::runStats, 1000);
  Controller::E.addInterval(Controller::updateLoad, 1000);
  Controller::variableTimer = Controller::E.addInterval(Controller::variableRun, 750);
  Controller::E.addInterval(Controller::runPushCheck, 1000);
  Controller::E.addInterval(statusMonitor, 3000);
  Controller::E.addSocket(apiSock.getSocket(), [&apiSock](void *) {
    APIConn *aConn = new APIConn(Controller::E, apiSock);
    if (!aConn) {
      FAIL_MSG("Could not create new API connection: out of memory");
      return;
    }
  }, 0);

  // Set up UDP API socket
  Socket::UDPConnection uSock(true);
  Util::Procs::socketList.insert(uSock.getSock());
  uint16_t boundPort = 0;
  bool warned = false;
  auto attemptBind = [&]() {
    Util::Procs::socketList.erase(uSock.getSock());
    HTTP::URL udpApiAddr("udp://localhost:4242");
    if (getenv("UDP_API")) { udpApiAddr = HTTP::URL(getenv("UDP_API")); }
    boundPort = uSock.bind(udpApiAddr.getPort(), udpApiAddr.host);
    if (!boundPort) {
      boundPort = uSock.bind(0, udpApiAddr.host);
      if (!boundPort) {
        std::stringstream newHost;
        char ranNums[3];
        Util::getRandomBytes(ranNums, 3);
        newHost << "127." << (int)ranNums[0] << "." << (int)ranNums[1] << "." << (int)ranNums[2];
        boundPort = uSock.bind(0, newHost.str());
        if (!boundPort) {
          WARN_MSG("Could not open local UDP API socket; scheduling retry");
          warned = true;
          return 10000;
        }
        WARN_MSG("Could not open UDP API port on any port on %s - bound instead to %s", udpApiAddr.host.c_str(),
                 uSock.getBoundAddr().toString().c_str());
      } else {
        WARN_MSG("Could not open local UDP API socket on %s:%" PRIu16 " - bound to ephemeral port %" PRIu16 " instead",
                 udpApiAddr.host.c_str(), udpApiAddr.getPort(), boundPort);
      }
    } else {
      if (warned) {
        WARN_MSG("Local UDP API bound successfully on %s", uSock.getBoundAddr().toString().c_str());
      } else {
        INFO_MSG("Local UDP API bound on %s", uSock.getBoundAddr().toString().c_str());
      }
    }
    HTTP::URL boundAddr;
    boundAddr.protocol = "udp";
    boundAddr.setPort(boundPort);
    boundAddr.host = uSock.getBoundAddr().host();
    {
      std::lock_guard<std::mutex> guard(Controller::configMutex);
      Controller::udpApiBindAddr = boundAddr.getUrl();
      Controller::writeConfig();
    }
    uSock.allocateDestination();
    Util::Procs::socketList.insert(uSock.getSock());
    Controller::E.addSocket(uSock.getSock(), [&](void *) {
      while (uSock.Receive()) {
        MEDIUM_MSG("UDP API: %.*s", (int)uSock.data.size(), (const char *)uSock.data);
        JSON::Value Request = JSON::fromString(uSock.data, uSock.data.size());
        Request["minimal"] = true;
        JSON::Value Response;
        if (Request.isObject()) {
          std::lock_guard<std::mutex> guard(Controller::configMutex);
          Response["authorize"]["local"] = true;
          Controller::handleAPICommands(Request, Response);
          Response.removeMember("authorize");
          // Only reply if the request does not come from our own port (prevent loops)
          if (uSock.getRemoteAddr().port() != boundPort) { uSock.SendNow(Response.toString()); }
        } else {
          WARN_MSG("Invalid API command received over UDP: %s", (const char *)uSock.data);
        }
      }
    }, 0);
    return 0;
  };
  attemptBind();
  // Retry bind every 10s if needed
  if (!boundPort) { Controller::E.addInterval(attemptBind, 10000); }

  Controller::conf.activate();

#ifdef UPDATER
  Controller::E.addInterval(Controller::updaterCheck, 3600000);
  if (Controller::conf.getBool("update")) {
    Controller::rollingUpdate();
    Controller::updaterCheck();
  }
#endif

  // start main event loop
  LOG_MSG("CONF", "Controller started");
  while (Controller::conf.is_active) {
    // Handle events
    Controller::E.await(5000);

    // Catch up on logs
    Controller::logParser();

    // Check if we're shutting down, if so, check SYSTEM_STOP trigger and allow cancelling it
    if (!Controller::conf.is_active) {
      if (Triggers::shouldTrigger("SYSTEM_STOP")) {
        if (!Triggers::doTrigger("SYSTEM_STOP", Util::exitReason)) {
          Controller::conf.is_active = true;
          Util::Config::is_restarting = false;
          LOG_MSG("CONF", "Shutdown prevented by SYSTEM_STOP trigger");
        }
      }
    }
  }

  INFO_MSG("Shutdown reason: %s", Util::exitReason);

  Controller::deinitStats();
  Controller::deinitPushCheck();
  Controller::variableDeinit();
  if (Util::Config::is_restarting) {
    Controller::prepareActiveConnectorsForReload();
  } else {
    Controller::prepareActiveConnectorsForShutdown();
  }

  // write config
  Controller::writeConfigToDisk(true);
  // stop all child processes
  Util::Procs::StopAll();
  // give everything some time to print messages
  Util::wait(100);
  std::cout << "Killed all processes, wrote config to disk. Exiting." << std::endl;
  if (Util::Config::is_restarting){return 42;}

  // close logInput to make the log reading thread exit, then join it
  close(STDERR_FILENO);
  close(logInput);
  logThread.join();

  // Finally de-allocate storage - the log thread uses these structures
  Controller::deinitStorage(Util::Config::is_restarting);
  return 0;
}

/// Controller entry point - either starts main_loop or runs a child process that does.
int main(int argc, char **argv){
#ifdef __CYGWIN__
  // Cygwin is weird and the file permissions often get messed up somehow for different Windows account types.
  // This sets the umask to zero, meaning all file permissions are set wide open.
  // Not sure if this is the right way, but it seems to help!
  umask(0);
#endif

#ifdef DOCKERRUN 
  // Docker-specific code to run any command passed to the controller
  if(argc > 1) {
    char *path = getenv("PATH");
    std::deque<std::string> paths;
    if (path) {
      Util::splitString(path, ':', paths);
    } else {
      paths.emplace_back("/usr/local/bin/");
      paths.emplace_back("/usr/bin/");
    }

    for (const std::string &p : paths) {
      struct stat buf;
      if(stat((p + '/' + argv[1]).c_str(), &buf)) continue;
      if (buf.st_mode & S_IXOTH) execvp(argv[1], argv + 1);
    }
  }
#endif

  Controller::conf = Util::Config(argv[0]);
  Util::Config::binaryType = Util::CONTROLLER;
  Controller::conf.activate();
  Controller::conf.setMutexAborter(&Controller::configMutex);

  // If we're the child process _or_ we're running without angel process, go into the true main loop now.
  if (getenv("ATHEIST") || getenv("CTRL_ATHEIST")) { return main_loop(argc, argv); }

  // We're the angel process - set the environment variable indicating such and start child processes in a loop.
  setenv("CTRL_ATHEIST", "1", 1);

  // Handle signals as parent
  Util::Procs::setHandler();
  {
    struct sigaction new_action;
    new_action.sa_sigaction = handleUSR1Parent;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGUSR1, &new_action, NULL);
  }

  uint64_t reTimer = 0;
  while (Controller::conf.is_active){
    pid_t pid = Util::Procs::StartPiped(argv);
#ifdef __CYGWIN__
    setenv("NO_WIPE_SHM", "1", 1);
#endif
    if (pid == -1){
      FAIL_MSG("Unable to spawn controller process!");
      return 2;
    }
    // wait for the process to exit
    int status;
    while (waitpid(pid, &status, 0) != pid && errno == EINTR){
      if (Util::Config::is_restarting){
        Controller::conf.is_active = true;
        kill(pid, SIGUSR1);
      }
      if (!Controller::conf.is_active){
        INFO_MSG("Shutting down controller because of signal interrupt...");
        Util::Procs::Stop(pid);
      }
      continue;
    }
    // if the exit was clean, don't restart it
    if (WIFEXITED(status) && (WEXITSTATUS(status) == 0) && !Util::Config::is_restarting){
      MEDIUM_MSG("Controller shut down cleanly");
      break;
    }
    if ((WIFEXITED(status) && WEXITSTATUS(status) == 42) || Util::Config::is_restarting){
      WARN_MSG("Refreshing angel process for update");
      std::string myFile = Util::getMyPath() + "MistController";
      execvp(myFile.c_str(), argv);
      FAIL_MSG("Error restarting: %s", strerror(errno));
    }
    Util::Config::is_restarting = false;
    INFO_MSG("Controller uncleanly shut down! Restarting in %" PRIu64 "...", reTimer);
    Util::wait(reTimer);
    reTimer += 1000;
  }
  return 0;
}

