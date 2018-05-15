#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <mist/timing.h>
#include <mist/shared_memory.h>
#include <mist/defines.h>
#include <mist/timing.h>
#include <mist/triggers.h> //LTS
#include "controller_storage.h"
#include "controller_capabilities.h"

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
  uint32_t maxLogsRecs = 0;
  uint32_t maxAccsRecs = 0;
  uint64_t firstLog = 0;
  IPC::sharedPage * shmLogs = 0;
  Util::RelAccX * rlxLogs = 0;
  IPC::sharedPage * shmAccs = 0;
  Util::RelAccX * rlxAccs = 0;
  IPC::sharedPage * shmStrm = 0;
  Util::RelAccX * rlxStrm = 0;

  Util::RelAccX * logAccessor(){
    return rlxLogs;
  }

  Util::RelAccX * accesslogAccessor(){
    return rlxAccs;
  }

  Util::RelAccX * streamsAccessor(){
    return rlxStrm;
  }

  ///\brief Store and print a log message.
  ///\param kind The type of message.
  ///\param message The message to be logged.
  void Log(std::string kind, std::string message, bool noWriteToLog){
    if (noWriteToLog){
      tthread::lock_guard<tthread::mutex> guard(logMutex);
      JSON::Value m;
      uint64_t logTime = Util::epoch();
      m.append((long long)logTime);
      m.append(kind);
      m.append(message);
      Storage["log"].append(m);
      Storage["log"].shrink(100); // limit to 100 log messages
      logCounter++;
      if (rlxLogs && rlxLogs->isReady()){
        if (!firstLog){
          firstLog = logCounter;
        }
        rlxLogs->setRCount(logCounter > maxLogsRecs ? maxLogsRecs : logCounter);
        rlxLogs->setDeleted(logCounter > rlxLogs->getRCount() ? logCounter - rlxLogs->getRCount() : firstLog);
        rlxLogs->setInt("time", logTime, logCounter-1);
        rlxLogs->setString("kind", kind, logCounter-1);
        rlxLogs->setString("msg", message, logCounter-1);
        rlxLogs->setEndPos(logCounter);
      }
    }else{
      std::cerr << kind << "|MistController|" << getpid() << "||" << message << "\n";
    }
  }

  void logAccess(const std::string & sessId, const std::string & strm, const std::string & conn, const std::string & host, uint64_t duration, uint64_t up, uint64_t down, const std::string & tags){
    if (rlxAccs && rlxAccs->isReady()){
      uint64_t newEndPos = rlxAccs->getEndPos();
      rlxAccs->setRCount(newEndPos+1 > maxLogsRecs ? maxAccsRecs : newEndPos+1);
      rlxAccs->setDeleted(newEndPos + 1 > maxAccsRecs ? newEndPos + 1 - maxAccsRecs : 0);
      rlxAccs->setInt("time", Util::epoch(), newEndPos);
      rlxAccs->setString("session", sessId, newEndPos);
      rlxAccs->setString("stream", strm, newEndPos);
      rlxAccs->setString("connector", conn, newEndPos);
      rlxAccs->setString("host", host, newEndPos);
      rlxAccs->setInt("duration", duration, newEndPos);
      rlxAccs->setInt("up", up, newEndPos);
      rlxAccs->setInt("down", down, newEndPos);
      rlxAccs->setString("tags", tags, newEndPos);
      rlxAccs->setEndPos(newEndPos + 1);
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

  void initState(){
    tthread::lock_guard<tthread::mutex> guard(logMutex);
    shmLogs = new IPC::sharedPage(SHM_STATE_LOGS, 1024*1024, true);//max 1M of logs cached
    if (!shmLogs->mapped){
      FAIL_MSG("Could not open memory page for logs buffer");
      return;
    }
    rlxLogs = new Util::RelAccX(shmLogs->mapped, false);
    if (rlxLogs->isReady()){
      logCounter = rlxLogs->getEndPos();
    }else{
      rlxLogs->addField("time", RAX_64UINT);
      rlxLogs->addField("kind", RAX_32STRING);
      rlxLogs->addField("msg", RAX_512STRING);
      rlxLogs->setReady();
    }
    maxLogsRecs = (1024*1024 - rlxLogs->getOffset()) / rlxLogs->getRSize();

    shmAccs = new IPC::sharedPage(SHM_STATE_ACCS, 1024*1024, true);//max 1M of accesslogs cached
    if (!shmAccs->mapped){
      FAIL_MSG("Could not open memory page for access logs buffer");
      return;
    }
    rlxAccs = new Util::RelAccX(shmAccs->mapped, false);
    if (!rlxAccs->isReady()){
      rlxAccs->addField("time", RAX_64UINT);
      rlxAccs->addField("session", RAX_32STRING);
      rlxAccs->addField("stream", RAX_128STRING);
      rlxAccs->addField("connector", RAX_32STRING);
      rlxAccs->addField("host", RAX_64STRING);
      rlxAccs->addField("duration", RAX_32UINT);
      rlxAccs->addField("up", RAX_64UINT);
      rlxAccs->addField("down", RAX_64UINT);
      rlxAccs->addField("tags", RAX_256STRING);
      rlxAccs->setReady();
    }
    maxAccsRecs = (1024*1024 - rlxAccs->getOffset()) / rlxAccs->getRSize();

    shmStrm = new IPC::sharedPage(SHM_STATE_STREAMS, 1024*1024, true);//max 1M of stream data
    if (!shmStrm->mapped){
      FAIL_MSG("Could not open memory page for stream data");
      return;
    }
    rlxStrm = new Util::RelAccX(shmStrm->mapped, false);
    if (!rlxStrm->isReady()){
      rlxStrm->addField("stream", RAX_128STRING);
      rlxStrm->addField("status", RAX_UINT, 1);
      rlxStrm->addField("viewers", RAX_64UINT);
      rlxStrm->addField("inputs", RAX_64UINT);
      rlxStrm->addField("outputs", RAX_64UINT);
      rlxStrm->setReady();
    }
    rlxStrm->setRCount((1024*1024 - rlxStrm->getOffset()) / rlxStrm->getRSize());
  }

  void deinitState(bool leaveBehind){
    tthread::lock_guard<tthread::mutex> guard(logMutex);
    if (!leaveBehind){
      rlxLogs->setExit();
      shmLogs->master = true;
      rlxAccs->setExit();
      shmAccs->master = true;
      rlxStrm->setExit();
      shmStrm->master = true;
    }else{
      shmLogs->master = false;
      shmAccs->master = false;
      shmStrm->master = false;
    }
    Util::RelAccX * tmp = rlxLogs;
    rlxLogs = 0;
    delete tmp;
    delete shmLogs;
    shmLogs = 0;
    tmp = rlxAccs;
    rlxAccs = 0;
    delete tmp;
    delete shmAccs;
    shmAccs = 0;
    tmp = rlxStrm;
    rlxStrm = 0;
    delete tmp;
    delete shmStrm;
    shmStrm = 0;
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
        tPage.addField("default", RAX_128STRING);
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
            if (triggIt->isMember("default") && !(*triggIt)["default"].isNull()){
              tPage.setString("default", (*triggIt)["default"].asStringRef(), i);
            }else{
              tPage.setString("default", "", i);
            }
          }

          ++i;
        }
        tPage.setRCount(std::min(i, max));
        tPage.setEndPos(std::min(i, max));
      }
    }

    static bool serverStartTriggered;
    if (!serverStartTriggered){
      if (!Triggers::doTrigger("SYSTEM_START")){conf.is_active = false;}
      serverStartTriggered = true;
    }
    if (Triggers::shouldTrigger("SYSTEM_CONFIG")){
      std::string payload = writeConf.toString();
      Triggers::doTrigger("SYSTEM_CONFIG", payload);
    }
    /*LTS-END*/
  }
}

