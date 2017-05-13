#include "controller_storage.h"
#include "controller_capabilities.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <mist/defines.h>
#include <mist/shared_memory.h>
#include <mist/timing.h>
#include <mist/triggers.h> //LTS
#include <mist/util.h>     //LTS
#include <sys/stat.h>

///\brief Holds everything unique to the controller.
namespace Controller{
  std::string instanceId; /// instanceId (previously uniqId) is set in controller.cpp
  std::string prometheus;
  std::string accesslog;
  Util::Config conf;
  JSON::Value Storage; ///< Global storage of data.
  tthread::mutex configMutex;
  tthread::mutex logMutex;
  unsigned long long logCounter = 0;
  bool configChanged = false;
  bool restarting = false;
  bool isTerminal = false;
  bool isColorized = false;

  ///\brief Store and print a log message.
  ///\param kind The type of message.
  ///\param message The message to be logged.
  void Log(std::string kind, std::string message){
    tthread::lock_guard<tthread::mutex> guard(logMutex);
    std::string color_time, color_msg, color_end;
    if (Controller::isColorized){
      color_end = "\033[0m";
      color_time = "\033[2m";
      color_msg = color_end;
      if (kind == "CONF"){color_msg = "\033[0;1;37m";}
      if (kind == "FAIL"){color_msg = "\033[0;1;31m";}
      if (kind == "ERROR"){color_msg = "\033[0;31m";}
      if (kind == "WARN"){color_msg = "\033[0;1;33m";}
      if (kind == "INFO"){color_msg = "\033[0;36m";}
    }
    JSON::Value m;
    m.append(Util::epoch());
    m.append(kind);
    m.append(message);
    Storage["log"].append(m);
    Storage["log"].shrink(100); // limit to 100 log messages
    time_t rawtime;
    struct tm *timeinfo;
    char buffer[100];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buffer, 100, "%F %H:%M:%S", timeinfo);
    std::cout << color_time << "[" << buffer << "] " << color_msg << kind << ": " << message << color_end << std::endl;
    logCounter++;
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

  /// Handles output of a Mist application, detecting and catching debug messages.
  /// Debug messages are automatically converted into Log messages.
  /// Closes the file descriptor on read error.
  /// \param err File descriptor of the stderr output of the process to monitor.
  void handleMsg(void *err){
    char buf[1024];
    FILE *output = fdopen((long long int)err, "r");
    while (fgets(buf, 1024, output)){
      unsigned int i = 0;
      while (i < 9 && buf[i] != '|' && buf[i] != 0){++i;}
      unsigned int j = i;
      while (j < 1024 && buf[j] != '\n' && buf[j] != 0){++j;}
      buf[j] = 0;
      if (i < 9){
        buf[i] = 0;
        Log(buf, buf + i + 1);
      }else{
        printf("%s", buf);
      }
    }
    Log("LOG", "Logger exiting");
    fclose(output);
    close((long long int)err);
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
  /// \triggers
  /// The `"SYSTEM_START"` trigger is global, and is ran as soon as the server configuration is first stable. It has no payload. If cancelled,
  /// the system immediately shuts down again.
  /// \n
  /// The `"SYSTEM_CONFIG"` trigger is global, and is ran every time the server configuration is updated. Its payload is the new configuration in
  /// JSON format. This trigger cannot be cancelled.
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
    if (!changed){return;}// cancel further processing if no changes

    static IPC::sharedPage mistConfOut(SHM_CONF, DEFAULT_CONF_PAGE_SIZE, true);
    if (!mistConfOut.mapped){
      FAIL_MSG("Could not open config shared memory storage for writing! Is shared memory enabled on your system?");
      return;
    }
    IPC::semaphore configLock(SEM_CONF, O_CREAT | O_RDWR, ACCESSPERMS, 1);
    // lock semaphore
    configLock.wait();
    // write config
    std::string temp = writeConf.toPacked();
    memcpy(mistConfOut.mapped, temp.data(), std::min(temp.size(), (size_t)mistConfOut.len));
    // unlock semaphore
    configLock.post();

    /*LTS-START*/
    static std::map<std::string, IPC::sharedPage> pageForType; // should contain one page for every trigger type
    char tmpBuf[NAME_BUFFER_SIZE];

    // for all shm pages that hold triggers
    pageForType.clear();

    if (writeConf["config"]["triggers"].size()){
      jsonForEach(writeConf["config"]["triggers"], it){
        snprintf(tmpBuf, NAME_BUFFER_SIZE, SHM_TRIGGER, (it.key()).c_str());
        pageForType[it.key()].init(tmpBuf, 32 * 1024, true, false);
        Util::RelAccX tPage(pageForType[it.key()].mapped, false);
        tPage.addField("url", RAX_128STRING);
        tPage.addField("sync", RAX_UINT);
        tPage.addField("streams", RAX_256RAW);
        tPage.addField("params", RAX_128STRING);
        tPage.setReady();
        uint32_t i = 0;
        uint32_t max = (32 * 1024 - tPage.getOffset()) / tPage.getRSize();

        // write data to page
        jsonForEach(*it, triggIt){
          if (i >= max){
            ERROR_MSG("Not all %s triggers fit on the memory page!", (it.key()).c_str());
            break;
          }

          if (triggIt->isArray()){
            tPage.setString("url", (*triggIt)[0u].asStringRef(), i);
            tPage.setInt("sync", ((*triggIt)[1u].asBool() ? 1 : 0), i);
            char *strmP = tPage.getPointer("streams", i);
            if (strmP){
              ((unsigned int *)strmP)[0] = 0; // reset first 4 bytes of stream list pointer
              if ((triggIt->size() >= 3) && (*triggIt)[2u].size()){
                std::string namesArray;
                jsonForEach((*triggIt)[2u], shIt){
                  ((unsigned int *)tmpBuf)[0] = shIt->asString().size();
                  namesArray.append(tmpBuf, 4);
                  namesArray.append(shIt->asString());
                }
                if (namesArray.size()){memcpy(strmP, namesArray.data(), std::min(namesArray.size(), (size_t)256));}
              }
            }
          }

          if (triggIt->isObject()){
            if (!triggIt->isMember("handler") || (*triggIt)["handler"].isNull()){continue;}
            tPage.setString("url", (*triggIt)["handler"].asStringRef(), i);
            tPage.setInt("sync", ((*triggIt)["sync"].asBool() ? 1 : 0), i);
            char *strmP = tPage.getPointer("streams", i);
            if (strmP){
              ((unsigned int *)strmP)[0] = 0; // reset first 4 bytes of stream list pointer
              if ((triggIt->isMember("streams")) && (*triggIt)["streams"].size()){
                std::string namesArray;
                jsonForEach((*triggIt)["streams"], shIt){
                  ((unsigned int *)tmpBuf)[0] = shIt->asString().size();
                  namesArray.append(tmpBuf, 4);
                  namesArray.append(shIt->asString());
                }
                if (namesArray.size()){memcpy(strmP, namesArray.data(), std::min(namesArray.size(), (size_t)256));}
              }
            }
            if (triggIt->isMember("params") && !(*triggIt)["params"].isNull()){
              tPage.setString("params", (*triggIt)["params"].asStringRef(), i);
            }else{
              tPage.setString("params", "", i);
            }
          }

          ++i;
        }
        tPage.setRCount(std::min(i, max));
      }
    }

    static bool serverStartTriggered;
    if (!serverStartTriggered){
      if (!Triggers::doTrigger("SYSTEM_START")){conf.is_active = false;}
      serverStartTriggered++;
    }
    if (Triggers::shouldTrigger("SYSTEM_CONFIG")){
      std::string payload = writeConf.toString();
      Triggers::doTrigger("SYSTEM_CONFIG", payload);
    }
    /*LTS-END*/
  }
}

