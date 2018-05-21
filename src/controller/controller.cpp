/// \file controller.cpp
/// Contains all code for the controller executable.

#include <mist/util.h>
#include "controller_api.h"
#include "controller_capabilities.h"
#include "controller_connectors.h"
#include "controller_statistics.h"
#include "controller_storage.h"
#include "controller_streams.h"
#include <ctime>
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
#include <stdio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <vector>

#ifndef COMPILED_USERNAME
#define COMPILED_USERNAME ""
#define COMPILED_PASSWORD ""
#endif

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

/// Status monitoring thread.
/// Will check outputs, inputs and converters every five seconds
void statusMonitor(void *np){
  IPC::semaphore configLock(SEM_CONF, O_CREAT | O_RDWR, ACCESSPERMS, 1);
  Controller::loadActiveConnectors();
  while (Controller::conf.is_active){
    // this scope prevents the configMutex from being locked constantly
    {
      tthread::lock_guard<tthread::mutex> guard(Controller::configMutex);
      bool changed = false;
      // checks online protocols, reports changes to status
      changed |= Controller::CheckProtocols(Controller::Storage["config"]["protocols"],
                                            Controller::capabilities);
      // checks stream statuses, reports changes to status
      changed |= Controller::CheckAllStreams(Controller::Storage["streams"]);

      // check if the config semaphore is stuck, by trying to lock it for 5 attempts of 1 second...
      if (!configLock.tryWaitOneSecond() && !configLock.tryWaitOneSecond() &&
          !configLock.tryWaitOneSecond() && !configLock.tryWaitOneSecond()){
        // that failed. We now unlock it, no matter what - and print a warning that it was stuck.
        WARN_MSG("Configuration semaphore was stuck. Force-unlocking it and re-writing config.");
        changed = true;
      }
      configLock.post();
      if (changed || Controller::configChanged){
        Controller::writeConfig();
        Controller::configChanged = false;
      }
    }
    Util::sleep(5000); // wait at least 5 seconds
  }
  if (Controller::restarting){
    Controller::prepareActiveConnectorsForReload();
  }else{
    Controller::prepareActiveConnectorsForShutdown();
  }
  configLock.unlink();
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

///\brief The main loop for the controller.
int main_loop(int argc, char **argv){
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
                                  "log file, provided with an argument\"}"));
  Controller::conf.addOption(
      "configFile", JSON::fromString("{\"long\":\"config\", \"short\":\"c\", \"arg\":\"string\" "
                                     "\"default\":\"config.json\", \"help\":\"Specify a config "
                                     "file other than default.\"}"));
  Controller::conf.parseArgs(argc, argv);
  if (Controller::conf.getString("logfile") != ""){
    // open logfile, dup stdout to logfile
    int output =
        open(Controller::conf.getString("logfile").c_str(), O_APPEND | O_CREAT | O_WRONLY, S_IRWXU);
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
                << "!----MistServer Started at " << buffer << " ----!" << std::endl;
    }
  }
  // reload config from config file
  Controller::Storage = JSON::fromFile(Controller::conf.getString("configFile"));

  {// spawn thread that reads stderr of process
    std::string logPipe = Util::getTmpFolder()+"MstLog";
    if (mkfifo(logPipe.c_str(), S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH) != 0){
      if (errno != EEXIST){
        ERROR_MSG("Could not create log message pipe %s: %s", logPipe.c_str(), strerror(errno));
      }
    }
    int inFD = -1;
    if ((inFD = open(logPipe.c_str(), O_RDONLY | O_NONBLOCK)) == -1){
      ERROR_MSG("Could not open log message pipe %s: %s; falling back to unnamed pipe", logPipe.c_str(), strerror(errno));
      int pipeErr[2];
      if (pipe(pipeErr) >= 0){
        dup2(pipeErr[1], STDERR_FILENO); // cause stderr to write to the pipe
        close(pipeErr[1]);               // close the unneeded pipe file descriptor
        //Start reading log messages from the unnamed pipe
        Util::Procs::socketList.insert(pipeErr[0]); //Mark this FD as needing to be closed before forking
        tthread::thread msghandler(Controller::handleMsg, (void *)(((char *)0) + pipeErr[0]));
        msghandler.detach();
      }
    }else{
      //Set the read end to blocking mode
      int inFDflags = fcntl(inFD, F_GETFL, 0);
      fcntl(inFD, F_SETFL, inFDflags & (~O_NONBLOCK));
      //Start reading log messages from the named pipe
      Util::Procs::socketList.insert(inFD); //Mark this FD as needing to be closed before forking
      tthread::thread msghandler(Controller::handleMsg, (void *)(((char *)0) + inFD));
      msghandler.detach();
      //Attempt to open and redirect log messages to named pipe
      int outFD = -1;
      if ((outFD = open(logPipe.c_str(), O_WRONLY)) == -1){
        ERROR_MSG("Could not open log message pipe %s for writing! %s; falling back to standard error", logPipe.c_str(), strerror(errno));
      }else{
        dup2(outFD, STDERR_FILENO); // cause stderr to write to the pipe
        close(outFD);               // close the unneeded pipe file descriptor
      }
    }
    setenv("MIST_CONTROL", "1", 0);//Signal in the environment that the controller handles all children
  }

  if (Controller::conf.getOption("debug", true).size() > 1){
    Controller::Storage["config"]["debug"] = Controller::conf.getInteger("debug");
  }
  if (Controller::Storage.isMember("config") && Controller::Storage["config"].isMember("debug") &&
      Controller::Storage["config"]["debug"].isInt()){
    Util::Config::printDebugLevel = Controller::Storage["config"]["debug"].asInt();
  }
  // check for port, interface and username in arguments
  // if they are not there, take them from config file, if there
  if (Controller::Storage["config"]["controller"]["port"]){
    Controller::conf.getOption("port", true)[0u] =
        Controller::Storage["config"]["controller"]["port"];
  }
  if (Controller::Storage["config"]["controller"]["interface"]){
    Controller::conf.getOption("interface", true)[0u] =
        Controller::Storage["config"]["controller"]["interface"];
  }
  if (Controller::Storage["config"]["controller"]["username"]){
    Controller::conf.getOption("username", true)[0u] =
        Controller::Storage["config"]["controller"]["username"];
  }
  {
    IPC::semaphore configLock(SEM_CONF, O_CREAT | O_RDWR, ACCESSPERMS, 1);
    configLock.unlink();
  }
  Controller::writeConfig();
  Controller::checkAvailProtocols();
  createAccount(Controller::conf.getString("account"));
  Controller::conf.activate(); // activate early, so threads aren't killed.

  // if a terminal is connected and we're not logging to file
  if (Controller::isTerminal){
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
          while (!(Controller::Storage.isMember("account") &&
                   Controller::Storage["account"].size() > 0) &&
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
        case 'a':
          return 0; // abort bootup
        case 't':{
          createAccount("test:test");
          if ((Controller::capabilities["connectors"].size()) &&
              (!Controller::Storage.isMember("config") ||
               !Controller::Storage["config"].isMember("protocols") ||
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
        (!Controller::Storage.isMember("config") ||
         !Controller::Storage["config"].isMember("protocols") ||
         Controller::Storage["config"]["protocols"].size() < 1)){
      std::string in_string = "";
      while (yna(in_string) == 'x' && Controller::conf.is_active){
        std::cout
            << "Protocols not set, do you want to enable default protocols? (y)es, (n)o, (a)bort: ";
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
          return 0;
        }
      }
    }
  }

  // Check if we have a usable server, if not, print messages with helpful hints
  {
    std::string web_port = JSON::Value((long long)Controller::conf.getInteger("port")).asString();
    // check for username
    if (!Controller::Storage.isMember("account") || Controller::Storage["account"].size() < 1){
      Controller::Log("CONF",
                      "No login configured. To create one, attempt to login through the web "
                      "interface on port " +
                          web_port + " and follow the instructions.");
    }
    // check for protocols
    if (!Controller::Storage.isMember("config") ||
        !Controller::Storage["config"].isMember("protocols") ||
        Controller::Storage["config"]["protocols"].size() < 1){
      Controller::Log(
          "CONF",
          "No protocols enabled, remember to set them up through the web interface on port " +
              web_port + " or API.");
    }
    // check for streams - regardless of logfile setting
    if (!Controller::Storage.isMember("streams") || Controller::Storage["streams"].size() < 1){
      Controller::Log(
          "CONF",
          "No streams configured, remember to set up streams through the web interface on port " +
              web_port + " or API.");
    }
  }

  Controller::Log("CONF", "Controller started");
  // Generate instanceId once per boot.
  if (Controller::instanceId == ""){
    srand(mix(clock(), time(0), getpid()));
    do{
      Controller::instanceId += (char)(64 + rand() % 62);
    }while (Controller::instanceId.size() < 16);
  }

  // start stats thread
  tthread::thread statsThread(Controller::SharedMemStats, &Controller::conf);
  // start monitoring thread
  tthread::thread monitorThread(statusMonitor, 0);

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
    if (Controller::restarting){shutdown_reason = "restart (on request)";}
    Controller::conf.is_active = false;
    Controller::Log("CONF", "Controller shutting down because of " + shutdown_reason);
  }
  // join all joinable threads
  HIGH_MSG("Joining stats thread...");
  statsThread.join();
  HIGH_MSG("Joining monitor thread...");
  monitorThread.join();
  // write config
  tthread::lock_guard<tthread::mutex> guard(Controller::logMutex);
  Controller::writeConfigToDisk();
  // stop all child processes
  Util::Procs::StopAll();
  // give everything some time to print messages
  Util::wait(100);
  std::cout << "Killed all processes, wrote config to disk. Exiting." << std::endl;
  if (Controller::restarting){return 42;}
  // close stderr to make the stderr reading thread exit
  close(STDERR_FILENO);
  return 0;
}

void handleUSR1(int signum, siginfo_t *sigInfo, void *ignore){
  Controller::Log("CONF", "USR1 received - restarting controller");
  Controller::restarting = true;
  raise(SIGINT); // trigger restart
}

///\brief The controller angel process.
/// Starts a forked main_loop in a loop. Yes, you read that right.
int main(int argc, char **argv){
  Util::Procs::setHandler(); // set child handler
  {
    struct sigaction new_action;
    struct sigaction cur_action;
    new_action.sa_sigaction = handleUSR1;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGUSR1, &new_action, NULL);
  }

  Controller::conf = Util::Config(argv[0]);
  Controller::conf.activate();
  uint64_t reTimer = 0;
  while (Controller::conf.is_active){
    pid_t pid = fork();
    if (pid == 0){
      Util::Procs::handler_set = false;
      Util::Procs::reaper_thread = 0;
      {
        struct sigaction new_action;
        struct sigaction cur_action;
        new_action.sa_sigaction = handleUSR1;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGUSR1, &new_action, NULL);
      }
      return main_loop(argc, argv);
    }
    if (pid == -1){
      FAIL_MSG("Unable to spawn controller process!");
      return 2;
    }
    // wait for the process to exit
    int status;
    while (waitpid(pid, &status, 0) != pid && errno == EINTR){
      if (Controller::restarting){
        Controller::conf.is_active = true;
        Controller::restarting = false;
        kill(pid, SIGUSR1);
      }
      if (!Controller::conf.is_active){
        INFO_MSG("Shutting down controller because of signal interrupt...");
        Util::Procs::Stop(pid);
      }
      continue;
    }
    // if the exit was clean, don't restart it
    if (WIFEXITED(status) && (WEXITSTATUS(status) == 0)){
      MEDIUM_MSG("Controller shut down cleanly");
      break;
    }
    if (WIFEXITED(status) && (WEXITSTATUS(status) == 42)){
      WARN_MSG("Refreshing angel process for update");
      std::string myFile = Util::getMyPath() + "MistController";
      execvp(myFile.c_str(), argv);
      FAIL_MSG("Error restarting: %s", strerror(errno));
    }
    INFO_MSG("Controller uncleanly shut down! Restarting in %llu...", reTimer);
    Util::wait(reTimer);
    reTimer += 1000;
  }
  return 0;
}

