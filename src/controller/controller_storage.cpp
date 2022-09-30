#include "controller_capabilities.h"
#include "controller_storage.h"
#include "controller_push.h" //LTS
#include "controller_streams.h" //LTS
#include <algorithm>
#include <fstream>
#include <iostream>
#include <mist/defines.h>
#include <mist/shared_memory.h>
#include <mist/timing.h>
#include <mist/triggers.h> //LTS
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
  uint64_t logCounter = 0;
  bool configChanged = false;
  bool isTerminal = false;
  bool isColorized = false;
  uint32_t maxLogsRecs = 0;
  uint32_t maxAccsRecs = 0;
  uint64_t firstLog = 0;
  IPC::sharedPage *shmLogs = 0;
  Util::RelAccX *rlxLogs = 0;
  IPC::sharedPage *shmAccs = 0;
  Util::RelAccX *rlxAccs = 0;
  IPC::sharedPage *shmStrm = 0;
  Util::RelAccX *rlxStrm = 0;
  uint64_t systemBoot = Util::unixMS() - Util::bootMS();

  Util::RelAccX *logAccessor(){return rlxLogs;}

  Util::RelAccX *accesslogAccessor(){return rlxAccs;}

  Util::RelAccX *streamsAccessor(){return rlxStrm;}

  ///\brief Store and print a log message.
  ///\param kind The type of message.
  ///\param message The message to be logged.
  void Log(const std::string &kind, const std::string &message, const std::string &stream, uint64_t progPid, bool noWriteToLog){
    if (noWriteToLog){
      tthread::lock_guard<tthread::mutex> guard(logMutex);
      JSON::Value m;
      uint64_t logTime = Util::epoch();
      m.append(logTime);
      m.append(kind);
      m.append(message);
      m.append(stream);
      Storage["log"].append(m);
      Storage["log"].shrink(100); // limit to 100 log messages
      if (isPushActive(progPid)){pushLogMessage(progPid, m);} //LTS
      if (isProcActive(progPid)){procLogMessage(progPid, m);} //LTS
      logCounter++;
      if (rlxLogs && rlxLogs->isReady()){
        if (!firstLog){firstLog = logCounter;}
        rlxLogs->setRCount(logCounter > maxLogsRecs ? maxLogsRecs : logCounter);
        rlxLogs->setDeleted(logCounter > rlxLogs->getRCount() ? logCounter - rlxLogs->getRCount() : firstLog);
        rlxLogs->setInt("time", logTime, logCounter - 1);
        rlxLogs->setString("kind", kind, logCounter - 1);
        rlxLogs->setString("msg", message, logCounter - 1);
        rlxLogs->setString("strm", stream, logCounter - 1);
        rlxLogs->setEndPos(logCounter);
      }
    }else{
      std::cerr << kind << "|MistController|" << getpid() << "|||" << message << "\n";
    }
  }

  void logAccess(const std::string &sessId, const std::string &strm, const std::string &conn,
                 const std::string &host, uint64_t duration, uint64_t up, uint64_t down,
                 const std::string &tags){
    if (rlxAccs && rlxAccs->isReady()){
      uint64_t newEndPos = rlxAccs->getEndPos();
      rlxAccs->setRCount(newEndPos + 1 > maxLogsRecs ? maxAccsRecs : newEndPos + 1);
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

  void normalizeTrustedProxies(JSON::Value &tp){
    // First normalize to arrays
    if (!tp.isArray()){tp.append(tp.asString());}
    // Now, wipe any empty entries, and convert spaces to array entries
    std::set<std::string> n;
    jsonForEach(tp, jit){
      if (!jit->isString()){*jit = jit->asString();}
      if (jit->asStringRef().find(' ') == std::string::npos){
        n.insert(jit->asStringRef());
        continue;
      }
      std::string tmp = jit->asStringRef();
      while (tmp.find(' ') != std::string::npos){
        size_t p = tmp.find(' ');
        n.insert(tmp.substr(0, p));
        tmp.erase(0, p + 1);
      }
      if (tmp.size()){n.insert(tmp);}
    }
    n.erase("");
    // Re-write the entire array, which is now normalized
    tp.shrink(0);
    for (std::set<std::string>::iterator it = n.begin(); it != n.end(); ++it){tp.append(*it);}
  }

  ///\brief Write contents to Filename
  ///\param Filename The full path of the file to write to.
  ///\param contents The data to be written to the file.
  bool WriteFile(std::string Filename, std::string contents){
    if (!Util::createPathFor(Filename)){
      ERROR_MSG("Could not create parent folder for file %s!", Filename.c_str());
      return false;
    }
    std::ofstream File;
    File.open(Filename.c_str());
    File << contents << std::endl;
    File.close();
    return File.good();
  }

  void initState(){
    tthread::lock_guard<tthread::mutex> guard(logMutex);
    shmLogs = new IPC::sharedPage(SHM_STATE_LOGS, 1024 * 1024, false, false); // max 1M of logs cached
    if (!shmLogs || !shmLogs->mapped){
      if (shmLogs){delete shmLogs;}
      shmLogs = new IPC::sharedPage(SHM_STATE_LOGS, 1024 * 1024, true); // max 1M of logs cached
    }
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
      rlxLogs->addField("strm", RAX_128STRING);
      rlxLogs->setReady();
    }
    maxLogsRecs = (1024 * 1024 - rlxLogs->getOffset()) / rlxLogs->getRSize();

    shmAccs = new IPC::sharedPage(SHM_STATE_ACCS, 1024 * 1024, false, false); // max 1M of accesslogs cached
    if (!shmAccs || !shmAccs->mapped){
      if (shmAccs){delete shmAccs;}
      shmAccs = new IPC::sharedPage(SHM_STATE_ACCS, 1024 * 1024, true); // max 1M of accesslogs cached
    }
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
    maxAccsRecs = (1024 * 1024 - rlxAccs->getOffset()) / rlxAccs->getRSize();

    shmStrm = new IPC::sharedPage(SHM_STATE_STREAMS, 1024 * 1024, false, false); // max 1M of stream data
    if (!shmStrm || !shmStrm->mapped){
      if (shmStrm){delete shmStrm;}
      shmStrm = new IPC::sharedPage(SHM_STATE_STREAMS, 1024 * 1024, true); // max 1M of stream data
    }
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
      rlxStrm->addField("unspecified", RAX_64UINT);
      rlxStrm->setReady();
    }
    rlxStrm->setRCount((1024 * 1024 - rlxStrm->getOffset()) / rlxStrm->getRSize());
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
    Util::RelAccX *tmp = rlxLogs;
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

    if (Controller::Storage.isMember("config_split")){
      jsonForEach(Controller::Storage["config_split"], cs){
        if (cs->isString() && tmp.isMember(cs.key())){
          JSON::Value tmpConf = JSON::fromFile(cs->asStringRef());
          tmpConf[cs.key()] = tmp[cs.key()];
          if (!Controller::WriteFile(cs->asStringRef(), tmpConf.toPrettyString(4, false))){
            ERROR_MSG("Error writing config.%s to %s", cs.key().c_str(), cs->asStringRef().c_str());
            std::cout << "**config." << cs.key() <<"**" << std::endl;
            std::cout << tmp[cs.key()].toString() << std::endl;
            std::cout << "**End config." << cs.key() << "**" << std::endl;
          }
          if (cs.key() != "config_split"){tmp.removeMember(cs.key());}
        }
      }
    }
    if (!Controller::WriteFile(Controller::conf.getString("configFile"), tmp.toPrettyString(4, false))){
      ERROR_MSG("Error writing config to %s", Controller::conf.getString("configFile").c_str());
      std::cout << "**Config**" << std::endl;
      std::cout << tmp.toString() << std::endl;
      std::cout << "**End config**" << std::endl;
    }
  }

  void writeCapabilities(){
    std::string temp = capabilities.toPacked();
    static IPC::sharedPage mistCapaOut(SHM_CAPA, temp.size() + 100, false, false);
    if (mistCapaOut){
      Util::RelAccX tmpA(mistCapaOut.mapped, false);
      if (tmpA.isReady()){tmpA.setReload();}
      mistCapaOut.master = true;
      mistCapaOut.close();
    }
    mistCapaOut.init(SHM_CAPA, temp.size() + 100, true, false);
    if (!mistCapaOut.mapped){
      FAIL_MSG("Could not open capabilities config for writing! Is shared memory enabled on your "
               "system?");
      return;
    }
    Util::RelAccX A(mistCapaOut.mapped, false);
    A.addField("dtsc_data", RAX_DTSC, temp.size());
    // write config
    memcpy(A.getPointer("dtsc_data"), temp.data(), temp.size());
    A.setRCount(1);
    A.setEndPos(1);
    A.setReady();
  }

  void writeProtocols(){
    static std::string proxy_written;
    std::string tmpProxy;
    if (Storage["config"]["trustedproxy"].isArray()){
      jsonForEachConst(Storage["config"]["trustedproxy"], jit){
        if (tmpProxy.size()){
          tmpProxy += " " + jit->asString();
        }else{
          tmpProxy = jit->asString();
        }
      }
    }else{
      tmpProxy = Storage["config"]["trustedproxy"].asString();
    }
    if (proxy_written != tmpProxy){
      proxy_written = tmpProxy;
      static IPC::sharedPage mistProxOut(SHM_PROXY, proxy_written.size() + 100, true, false);
      mistProxOut.close();
      mistProxOut.init(SHM_PROXY, proxy_written.size() + 100, false, false);
      if (mistProxOut){
        Util::RelAccX tmpA(mistProxOut.mapped, false);
        if (tmpA.isReady()){tmpA.setReload();}
        mistProxOut.master = true;
        mistProxOut.close();
      }
      mistProxOut.init(SHM_PROXY, proxy_written.size() + 100, true, false);
      if (!mistProxOut.mapped){
        FAIL_MSG("Could not open trusted proxy config for writing! Is shared memory enabled on "
                 "your system?");
        return;
      }else{
        Util::RelAccX A(mistProxOut.mapped, false);
        A.addField("proxy_data", RAX_STRING, proxy_written.size());
        // write config
        memcpy(A.getPointer("proxy_data"), proxy_written.data(), proxy_written.size());
        A.setRCount(1);
        A.setEndPos(1);
        A.setReady();
      }
    }
    static JSON::Value proto_written;
    std::set<std::string> skip;
    skip.insert("online");
    skip.insert("error");
    if (Storage["config"]["protocols"].compareExcept(proto_written, skip)){return;}
    proto_written.assignFrom(Storage["config"]["protocols"], skip);
    std::string temp = proto_written.toPacked();
    static IPC::sharedPage mistProtoOut(SHM_PROTO, temp.size() + 100, false, false);
    if (mistProtoOut){
      Util::RelAccX tmpA(mistProtoOut.mapped, false);
      if (tmpA.isReady()){tmpA.setReload();}
      mistProtoOut.master = true;
      mistProtoOut.close();
    }
    mistProtoOut.init(SHM_PROTO, temp.size() + 100, true, false);
    if (!mistProtoOut.mapped){
      FAIL_MSG(
          "Could not open protocol config for writing! Is shared memory enabled on your system?");
      return;
    }
    // write config
    {
      Util::RelAccX A(mistProtoOut.mapped, false);
      A.addField("dtsc_data", RAX_DTSC, temp.size());
      // write config
      memcpy(A.getPointer("dtsc_data"), temp.data(), temp.size());
      A.setRCount(1);
      A.setEndPos(1);
      A.setReady();
    }
  }

  void writeStream(const std::string &sName, const JSON::Value &sConf){
    static std::map<std::string, JSON::Value> writtenStrms;
    static std::map<std::string, IPC::sharedPage> pages;
    static std::set<std::string> skip;
    if (!skip.size()){
      skip.insert("online");
      skip.insert("error");
      skip.insert("x-LSP-name");
    }
    if (sConf.isNull()){
      writtenStrms.erase(sName);
      pages.erase(sName);
      return;
    }
    if (!writtenStrms.count(sName) || !writtenStrms[sName].compareExcept(sConf, skip)){
      writtenStrms[sName].assignFrom(sConf, skip);
      IPC::sharedPage &P = pages[sName];
      std::string temp = writtenStrms[sName].toPacked();
      P.close();
      char tmpBuf[NAME_BUFFER_SIZE];
      snprintf(tmpBuf, NAME_BUFFER_SIZE, SHM_STREAM_CONF, sName.c_str());
      P.init(tmpBuf, temp.size() + 100, false, false);
      if (P){
        Util::RelAccX tmpA(P.mapped, false);
        if (tmpA.isReady()){tmpA.setReload();}
        P.master = true;
        P.close();
      }
      P.init(tmpBuf, temp.size() + 100, true, false);
      if (!P){
        writtenStrms.erase(sName);
        pages.erase(sName);
        return;
      }
      Util::RelAccX A(P.mapped, false);
      A.addField("dtsc_data", RAX_DTSC, temp.size());
      // write config
      memcpy(A.getPointer("dtsc_data"), temp.data(), temp.size());
      A.setRCount(1);
      A.setEndPos(1);
      A.setReady();
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
    writeProtocols();
    jsonForEach(Storage["streams"], it){
      it->removeNullMembers();
      writeStream(it.key(), *it);
    }

    {
      // Global configuration options, if any
      IPC::sharedPage globCfg;
      globCfg.init(SHM_GLOBAL_CONF, 4096, false, false);
      if (!globCfg.mapped){globCfg.init(SHM_GLOBAL_CONF, 4096, true, false);}
      if (globCfg.mapped){
        Util::RelAccX globAccX(globCfg.mapped, false);

        // if fields missing, recreate the page
        if (globAccX.isReady()){
          if(globAccX.getFieldAccX("systemBoot") && globAccX.getInt("systemBoot")){
            systemBoot = globAccX.getInt("systemBoot");
          }
          if(!globAccX.getFieldAccX("defaultStream")
             || !globAccX.getFieldAccX("systemBoot")
             || !globAccX.getFieldAccX("sessionViewerMode")
             || !globAccX.getFieldAccX("sessionInputMode")
             || !globAccX.getFieldAccX("sessionOutputMode")
             || !globAccX.getFieldAccX("sessionUnspecifiedMode")
             || !globAccX.getFieldAccX("sessionStreamInfoMode")
             || !globAccX.getFieldAccX("tknMode")){
            globAccX.setReload();
            globCfg.master = true;
            globCfg.close();
            globCfg.init(SHM_GLOBAL_CONF, 4096, true, false);
            globAccX = Util::RelAccX(globCfg.mapped, false);
          }
        }
        if (!globAccX.isReady()){
          globAccX.addField("defaultStream", RAX_128STRING);
          globAccX.addField("systemBoot", RAX_64UINT);
          globAccX.addField("sessionViewerMode", RAX_64UINT);
          globAccX.addField("sessionInputMode", RAX_64UINT);
          globAccX.addField("sessionOutputMode", RAX_64UINT);
          globAccX.addField("sessionUnspecifiedMode", RAX_64UINT);
          globAccX.addField("sessionStreamInfoMode", RAX_64UINT);
          globAccX.addField("tknMode", RAX_64UINT);
          globAccX.setRCount(1);
          globAccX.setEndPos(1);
          globAccX.setReady();
        }
        globAccX.setString("defaultStream", Storage["config"]["defaultStream"].asStringRef());
        globAccX.setInt("sessionViewerMode", Storage["config"]["sessionViewerMode"].asInt());
        globAccX.setInt("sessionInputMode", Storage["config"]["sessionInputMode"].asInt());
        globAccX.setInt("sessionOutputMode", Storage["config"]["sessionOutputMode"].asInt());
        globAccX.setInt("sessionUnspecifiedMode", Storage["config"]["sessionUnspecifiedMode"].asInt());
        globAccX.setInt("sessionStreamInfoMode", Storage["config"]["sessionStreamInfoMode"].asInt());
        globAccX.setInt("tknMode", Storage["config"]["tknMode"].asInt());
        globAccX.setInt("systemBoot", systemBoot);
        globCfg.master = false; // leave the page after closing
      }
    }

    /*LTS-START*/
    static std::map<std::string, IPC::sharedPage> pageForType; // should contain one page for every trigger type
    static JSON::Value writtenTrigs;
    char tmpBuf[NAME_BUFFER_SIZE];

    if (writtenTrigs != Storage["config"]["triggers"]){
      writtenTrigs = Storage["config"]["triggers"];
      // for all shm pages that hold triggers
      pageForType.clear();

      if (Storage["config"]["triggers"].size()){
        jsonForEach(Storage["config"]["triggers"], it){
          snprintf(tmpBuf, NAME_BUFFER_SIZE, SHM_TRIGGER, (it.key()).c_str());
          pageForType[it.key()].init(tmpBuf, 32 * 1024, false, false);
          if (pageForType[it.key()]){
            Util::RelAccX tmpA(pageForType[it.key()].mapped, false);
            if (tmpA.isReady()){tmpA.setReload();}
            pageForType[it.key()].master = true;
            pageForType[it.key()].close();
          }
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
                  if (namesArray.size()){
                    memcpy(strmP, namesArray.data(), std::min(namesArray.size(), (size_t)256));
                  }
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
                  if (namesArray.size()){
                    memcpy(strmP, namesArray.data(), std::min(namesArray.size(), (size_t)256));
                  }
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
    }

    static bool serverStartTriggered;
    if (!serverStartTriggered){
      if (Triggers::shouldTrigger("SYSTEM_START")){
        if (!Triggers::doTrigger("SYSTEM_START", PACKAGE_VERSION)){
          INFO_MSG("Shutting down because of SYSTEM_START trigger response");
          conf.is_active = false;
        }
      }
      serverStartTriggered = true;
    }
    /*LTS-END*/
  }
}// namespace Controller
