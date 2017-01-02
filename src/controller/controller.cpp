/// \page api API calls
/// \brief Listing of all controller API calls.
/// The controller listens for commands through a JSON-based API. This page describes the API in full.
///
/// A default interface implementing this API as a single HTML page is included in the controller itself. This default interface will be send for invalid API requests, and is thus triggered by default when a browser attempts to access the API port directly.
/// The default API port is 4242 - but this can be changed through both the API and commandline parameters.
///
/// To send an API request, simply send a HTTP request to this port for any file, and include either a GET or POST parameter called `"command"`, containing a JSON object as payload. Nearly all members of the request object are optional, and described below.
/// A simple example request logging in to the system would look like this:
///
///     GET /api?command={"authorize":{"username":"test","password":"941d7b88b2312d4373aff526cf7b6114"}} HTTP/1.0
///
/// Or, when properly URL encoded:
///
///     GET /api?command=%7B%22authorize%22%3A%7B%22username%22%3A%22test%22%2C%22password%22%3A%22941d7b88b2312d4373aff526cf7b6114%22%7D%7D HTTP/1.0
///
/// The server is quite lenient about not URL encoding your strings, but it's a good idea to always do it, anyway.
/// See the `"authorize"` section below for more information about security and logging in.
///
/// As mentioned above, sending an invalid request will trigger a response containing the default interface. As you may not want to receive a big HTML page as response to an invalid request, requesting the file `"/api"` (as done in the example above) will force a JSON response, even when the request is invalid.
///
/// You may also include a `"callback"` or `"jsonp"` HTTP variable, to trigger JSONP compatibility mode. JSONP is useful for getting around the cross-domain scripting protection in most modern browsers. Developers creating non-JavaScript applications will most likely not want to use JSONP mode, though nothing is stopping you if you really want to.
///




/// \file controller.cpp
/// Contains all code for the controller executable.


#include <stdio.h>
#include <iostream>
#include <ctime>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include <mist/config.h>
#include <mist/socket.h>
#include <mist/http_parser.h>
#include <mist/procs.h>
#include <mist/auth.h>
#include <mist/timing.h>
#include <mist/stream.h>
#include <mist/defines.h>
#include <mist/tinythread.h>
#include <mist/shared_memory.h>
#include "controller_storage.h"
#include "controller_streams.h"
#include "controller_capabilities.h"
#include "controller_connectors.h"
#include "controller_statistics.h"
#include "controller_api.h"

#ifndef COMPILED_USERNAME
#define COMPILED_USERNAME ""
#define COMPILED_PASSWORD ""
#endif

/// the following function is a simple check if the user wants to proceed to fix (y), ignore (n) or abort on (a) a question
static inline char yna(std::string & user_input){
  switch (user_input[0]){
    case 'y': case 'Y':
      return 'y';
      break;
    case 'n': case 'N':
      return 'n';
      break;
    case 'a': case 'A':
      return 'a';
      break;
    default:
      return 'x';
      break;
  }
}

/// createAccount accepts a string in the form of username:account
/// and creates an account.
void createAccount (std::string account){
  if (account.size() > 0){
    size_t colon = account.find(':');
    if (colon != std::string::npos && colon != 0 && colon != account.size()){
      std::string uname = account.substr(0, colon);
      std::string pword = account.substr(colon + 1, std::string::npos);
      Controller::Log("CONF", "Created account " + uname + " through commandline option");
      Controller::Storage["account"][uname]["password"] = Secure::md5(pword);
    }
  }
}


/// Status monitoring thread.
/// Will check outputs, inputs and converters every five seconds
void statusMonitor(void * np){
  IPC::semaphore configLock(SEM_CONF, O_CREAT | O_RDWR, ACCESSPERMS, 1);
  Controller::loadActiveConnectors();
  while (Controller::conf.is_active){
    //this scope prevents the configMutex from being locked constantly
    {
      tthread::lock_guard<tthread::mutex> guard(Controller::configMutex);
      bool changed = false;
      //checks online protocols, reports changes to status
      changed |= Controller::CheckProtocols(Controller::Storage["config"]["protocols"], Controller::capabilities);
      //checks stream statuses, reports changes to status
      changed |= Controller::CheckAllStreams(Controller::Storage["streams"]);
      
      //check if the config semaphore is stuck, by trying to lock it for 5 attempts of 1 second...
      if (!configLock.tryWaitOneSecond() && !configLock.tryWaitOneSecond() && !configLock.tryWaitOneSecond() && !configLock.tryWaitOneSecond()){
        //that failed. We now unlock it, no matter what - and print a warning that it was stuck.
        WARN_MSG("Configuration semaphore was stuck. Force-unlocking it and re-writing config.");
        changed = true;
      }
      configLock.post();
      if (changed || Controller::configChanged){
        Controller::writeConfig();
        Controller::configChanged = false;
      }
    }
    Util::sleep(5000);//wait at least 5 seconds
  }
  if (Controller::restarting){
    Controller::prepareActiveConnectorsForReload();
  }else{
    Controller::prepareActiveConnectorsForShutdown();
  }
  configLock.unlink();
}

///\brief The main loop for the controller.
int main_loop(int argc, char ** argv){
  Controller::Storage = JSON::fromFile("config.json");
  JSON::Value stored_port = JSON::fromString("{\"long\":\"port\", \"short\":\"p\", \"arg\":\"integer\", \"help\":\"TCP port to listen on.\"}");
  stored_port["default"] = Controller::Storage["config"]["controller"]["port"];
  if ( !stored_port["default"]){
    stored_port["default"] = 4242;
  }
  JSON::Value stored_interface = JSON::fromString("{\"long\":\"interface\", \"short\":\"i\", \"arg\":\"string\", \"help\":\"Interface address to listen on, or 0.0.0.0 for all available interfaces.\"}");
  stored_interface["default"] = Controller::Storage["config"]["controller"]["interface"];
  if ( !stored_interface["default"]){
    stored_interface["default"] = "0.0.0.0";
  }
  JSON::Value stored_user = JSON::fromString("{\"long\":\"username\", \"short\":\"u\", \"arg\":\"string\", \"help\":\"Username to transfer privileges to, default is root.\"}");
  stored_user["default"] = Controller::Storage["config"]["controller"]["username"];
  if ( !stored_user["default"]){
    stored_user["default"] = "root";
  }
  Controller::conf.addOption("port", stored_port);
  Controller::conf.addOption("interface", stored_interface);
  Controller::conf.addOption("username", stored_user);
  Controller::conf.addOption("account", JSON::fromString("{\"long\":\"account\", \"short\":\"a\", \"arg\":\"string\" \"default\":\"\", \"help\":\"A username:password string to create a new account with.\"}"));
  Controller::conf.addOption("logfile", JSON::fromString("{\"long\":\"logfile\", \"short\":\"L\", \"arg\":\"string\" \"default\":\"\",\"help\":\"Redirect all standard output to a log file, provided with an argument\"}"));
  Controller::conf.addOption("configFile", JSON::fromString("{\"long\":\"config\", \"short\":\"c\", \"arg\":\"string\" \"default\":\"config.json\", \"help\":\"Specify a config file other than default.\"}"));
  Controller::conf.parseArgs(argc, argv);
  if(Controller::conf.getString("logfile")!= ""){
    //open logfile, dup stdout to logfile
    int output = open(Controller::conf.getString("logfile").c_str(),O_APPEND|O_CREAT|O_WRONLY,S_IRWXU);
    if(output < 0){
      DEBUG_MSG(DLVL_ERROR, "Could not redirect output to %s: %s",Controller::conf.getString("logfile").c_str(),strerror(errno));
      return 7;
    }else{
      dup2(output,STDOUT_FILENO);
      dup2(output,STDERR_FILENO);
      time_t rawtime;
      struct tm * timeinfo;
      char buffer [25];
      time (&rawtime);
      timeinfo = localtime (&rawtime);
      strftime (buffer,25,"%c",timeinfo);
      std::cerr << std::endl << std::endl <<"!----MistServer Started at " << buffer << " ----!"  << std::endl;
    }
  }
  //reload config from config file
  Controller::Storage = JSON::fromFile(Controller::conf.getString("configFile"));
  
  {//spawn thread that reads stderr of process
    int pipeErr[2];
    if (pipe(pipeErr) >= 0){
      dup2(pipeErr[1], STDERR_FILENO);//cause stderr to write to the pipe
      close(pipeErr[1]);//close the unneeded pipe file descriptor
      Util::Procs::socketList.insert(pipeErr[0]);
      tthread::thread msghandler(Controller::handleMsg, (void*)(((char*)0) + pipeErr[0]));
      msghandler.detach();
    }
  }
  
  
  if (Controller::conf.getOption("debug",true).size() > 1){
    Controller::Storage["config"]["debug"] = Controller::conf.getInteger("debug");
  }
  if (Controller::Storage.isMember("config") && Controller::Storage["config"].isMember("debug")){
    Util::Config::printDebugLevel = Controller::Storage["config"]["debug"].asInt();
  }
  //check for port, interface and username in arguments
  //if they are not there, take them from config file, if there
  if (Controller::Storage["config"]["controller"]["port"]){
    Controller::conf.getOption("port", true)[0u] = Controller::Storage["config"]["controller"]["port"];
  }
  if (Controller::Storage["config"]["controller"]["interface"]){
    Controller::conf.getOption("interface", true)[0u] = Controller::Storage["config"]["controller"]["interface"];
  }
  if (Controller::Storage["config"]["controller"]["username"]){
    Controller::conf.getOption("username", true)[0u] = Controller::Storage["config"]["controller"]["username"];
  }
  {
    IPC::semaphore configLock(SEM_CONF, O_CREAT | O_RDWR, ACCESSPERMS, 1);
    configLock.unlink();
  }
  Controller::writeConfig();
  Controller::checkAvailProtocols();
  createAccount(Controller::conf.getString("account"));
  
  //if a terminal is connected and we're not logging to file
  if (isatty(fileno(stdin))){
    if (Controller::conf.getString("logfile") == ""){
      //check for username
      if ( !Controller::Storage.isMember("account") || Controller::Storage["account"].size() < 1){
        std::string in_string = "";
        while(yna(in_string) == 'x'){
          std::cout << "Account not set, do you want to create an account? (y)es, (n)o, (a)bort: ";
          std::cout.flush();
          std::getline(std::cin, in_string);
          if (yna(in_string) == 'y'){
            //create account
            std::string usr_string = "";
            while(!(Controller::Storage.isMember("account") && Controller::Storage["account"].size() > 0)){
              std::cout << "Please type in the username, a colon and a password in the following format; username:password" << std::endl << ": ";
              std::cout.flush();
              std::getline(std::cin, usr_string);
              createAccount(usr_string);
            }
          }else if(yna(in_string) == 'a'){
            //abort controller startup
            return 0;
          }
        }
      }
      //check for protocols
      if ( !Controller::Storage.isMember("config") || !Controller::Storage["config"].isMember("protocols") || Controller::Storage["config"]["protocols"].size() < 1){
        std::string in_string = "";
        while(yna(in_string) == 'x'){
          std::cout << "Protocols not set, do you want to enable default protocols? (y)es, (n)o, (a)bort: ";
          std::cout.flush();
          std::getline(std::cin, in_string);
          if (yna(in_string) == 'y'){
            //create protocols
            jsonForEach(Controller::capabilities["connectors"], it) {
              if (!it->isMember("required")){
                JSON::Value newProtocol;
                newProtocol["connector"] = it.key();
                Controller::Storage["config"]["protocols"].append(newProtocol);
              }
            }
          }else if(yna(in_string) == 'a'){
            //abort controller startup
            return 0;
          }
        }
      }
    }else{//logfile is enabled
      //check for username
      if ( !Controller::Storage.isMember("account") || Controller::Storage["account"].size() < 1){
        std::cout << "No login configured. To create one, attempt to login through the web interface on port " << Controller::conf.getInteger("port") << " and follow the instructions." << std::endl;
      }
      //check for protocols
      if ( !Controller::Storage.isMember("config") || !Controller::Storage["config"].isMember("protocols") || Controller::Storage["config"]["protocols"].size() < 1){
        std::cout << "No protocols enabled, remember to set them up through the web interface on port " << Controller::conf.getInteger("port") << " or API." << std::endl;
      }
    }
    //check for streams - regardless of logfile setting
    if ( !Controller::Storage.isMember("streams") || Controller::Storage["streams"].size() < 1){
      std::cout << "No streams configured, remember to set up streams through the web interface on port " << Controller::conf.getInteger("port") << " or API." << std::endl;
    }
  }//connected to a terminal
  
  Controller::Log("CONF", "Controller started");
  Controller::conf.activate();//activate early, so threads aren't killed.

  //start stats thread
  tthread::thread statsThread(Controller::SharedMemStats, &Controller::conf);
  //start monitoring thread
  tthread::thread monitorThread(statusMonitor, 0);
  
  //start main loop
  Controller::conf.serveThreadedSocket(Controller::handleAPIConnection);
  //print shutdown reason
  std::string shutdown_reason;
  if (!Controller::conf.is_active){
    shutdown_reason = "user request (received shutdown signal)";
  }else{
    shutdown_reason = "socket problem (API port closed)";
  }
  if (Controller::restarting){
    shutdown_reason = "restart (on request)";
  }
  Controller::conf.is_active = false;
  Controller::Log("CONF", "Controller shutting down because of "+shutdown_reason);
  //join all joinable threads
  statsThread.join();
  monitorThread.join();
  //write config
  tthread::lock_guard<tthread::mutex> guard(Controller::logMutex);
  Controller::Storage.removeMember("log");
  jsonForEach(Controller::Storage["streams"], it) {
    it->removeMember("meta");
  }
  if ( !Controller::WriteFile(Controller::conf.getString("configFile"), Controller::Storage.toString())){
    std::cerr << "Error writing config " << Controller::conf.getString("configFile") << std::endl;
    std::cerr << "**Config**" << std::endl;
    std::cerr << Controller::Storage.toString() << std::endl;
    std::cerr << "**End config**" << std::endl;
  }
  //stop all child processes
  Util::Procs::StopAll();
  //give everything some time to print messages
  Util::wait(100);
  std::cout << "Killed all processes, wrote config to disk. Exiting." << std::endl;
  if (Controller::restarting){
    return 42;
  }
  //close stderr to make the stderr reading thread exit
  close(STDERR_FILENO);
  return 0;
}

void handleUSR1(int signum, siginfo_t * sigInfo, void * ignore){
  Controller::Log("CONF", "USR1 received - restarting controller");
  Controller::restarting = true;
  raise(SIGINT); //trigger restart
}

///\brief The controller angel process.
///Starts a forked main_loop in a loop. Yes, you read that right.
int main(int argc, char ** argv){
  Util::Procs::setHandler();//set child handler
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
    //wait for the process to exit
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
    //if the exit was clean, don't restart it
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

