#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <mist/timing.h>
#include <mist/shared_memory.h>
#include <mist/defines.h>
#include <mist/util.h>
#include <sys/stat.h>
#include "controller_storage.h"
#include "controller_capabilities.h"

///\brief Holds everything unique to the controller.
namespace Controller {
  std::string instanceId; /// instanceId (previously uniqId) is set in controller.cpp

  Util::Config conf;
  JSON::Value Storage; ///< Global storage of data.
  tthread::mutex configMutex;
  tthread::mutex logMutex;
  bool configChanged = false;
  bool restarting = false;
  bool isTerminal = false;
  bool isColorized = false;

  ///\brief Store and print a log message.
  ///\param kind The type of message.
  ///\param message The message to be logged.
  void Log(std::string kind, std::string message, bool noWriteToLog){
    tthread::lock_guard<tthread::mutex> guard(logMutex);
    JSON::Value m;
    m.append(Util::epoch());
    m.append(kind);
    m.append(message);
    Storage["log"].append(m);
    Storage["log"].shrink(100); // limit to 100 log messages
    if (!noWriteToLog){
      std::cerr << kind << "|MistController|" << getpid() << "||" << message << "\n";
    }
  }

  ///\brief Write contents to Filename
  ///\param Filename The full path of the file to write to.
  ///\param contents The data to be written to the file.
  bool WriteFile(std::string Filename, std::string contents){
    std::ofstream File;
    File.open(Filename.c_str());
    File << contents << std::endl;
    File.close();
    return File.good();
  }
  
  void handleMsg(void *err){
    Util::logParser((long long)err, fileno(stdout), Controller::isColorized, &Log);
  }

  /// Writes the current config to the location set in the configFile setting.
  /// On error, prints an error-level message and the config to stdout.
  void writeConfigToDisk(){
    JSON::Value tmp;
    std::set<std::string> skip;
    skip.insert("log");
    skip.insert("online");
    skip.insert("error");
    tmp.assignFrom(Controller::Storage, skip);
    if ( !Controller::WriteFile(Controller::conf.getString("configFile"), tmp.toString())){
      ERROR_MSG("Error writing config to %s", Controller::conf.getString("configFile").c_str());
      std::cout << "**Config**" << std::endl;
      std::cout << tmp.toString() << std::endl;
      std::cout << "**End config**" << std::endl;
    }
  }

  /// Writes the current config to shared memory to be used in other processes
  void writeConfig(){
    static JSON::Value writeConf;
    bool changed = false;
    std::set<std::string> skip;
    skip.insert("online");
    skip.insert("error");
    if (!writeConf["config"].compareExcept(Storage["config"], skip)){
      writeConf["config"].assignFrom(Storage["config"], skip);
      VERYHIGH_MSG("Saving new config because of edit in server config structure");
      changed = true;
    }
    if (!writeConf["streams"].compareExcept(Storage["streams"], skip)){
      writeConf["streams"].assignFrom(Storage["streams"], skip);
      VERYHIGH_MSG("Saving new config because of edit in streams");
      changed = true;
    }
    if (writeConf["capabilities"] != capabilities){
      writeConf["capabilities"] = capabilities;
      VERYHIGH_MSG("Saving new config because of edit in capabilities");
      changed = true;
    }
    if (!changed){return;}//cancel further processing if no changes

    static IPC::sharedPage mistConfOut(SHM_CONF, DEFAULT_CONF_PAGE_SIZE, true);
    if (!mistConfOut.mapped){
      FAIL_MSG("Could not open config shared memory storage for writing! Is shared memory enabled on your system?");
      return;
    }
    IPC::semaphore configLock(SEM_CONF, O_CREAT | O_RDWR, ACCESSPERMS, 1);
    //lock semaphore
    configLock.wait();
    //write config
    std::string temp = writeConf.toPacked();
    memcpy(mistConfOut.mapped, temp.data(), std::min(temp.size(), (size_t)mistConfOut.len));
    //unlock semaphore
    configLock.post();
  }
  
}
