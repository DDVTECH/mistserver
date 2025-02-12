#include "controller_connectors.h"
#include "controller_storage.h"
#include <cstring> // strcpy
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/json.h>
#include <mist/procs.h>
#include <mist/shared_memory.h>
#include <mist/timing.h>
#include <mist/tinythread.h>
#include <mist/triggers.h>
#include <mist/util.h>
#include <stdio.h> // cout, cerr
#include <string>
#include <sys/stat.h> //stat

#include <iostream>
#include <unistd.h>

///\brief Holds everything unique to the controller.
namespace Controller{

  static std::set<size_t> needsReload; ///< List of connector indices that needs a reload
  static std::map<std::string, pid_t> currentConnectors; ///< The currently running connectors.

  void reloadProtocol(size_t indice){needsReload.insert(indice);}

  /// Updates the shared memory page with active connectors
  void saveActiveConnectors(bool forceOverride){
    IPC::sharedPage f("MstCnns", 4096, forceOverride, false);
    if (!f.mapped){
      if (!forceOverride){
        saveActiveConnectors(true);
        return;
      }
      if (!f.mapped){
        FAIL_MSG("Could not store connector data!");
        return;
      }
    }
    memset(f.mapped, 0, 32);
    Util::RelAccX A(f.mapped, false);
    if (!A.isReady()){
      A.addField("cmd", RAX_128STRING);
      A.addField("pid", RAX_64UINT);
      A.setReady();
    }
    uint32_t count = 0;
    std::map<std::string, pid_t>::iterator it;
    for (it = currentConnectors.begin(); it != currentConnectors.end(); ++it){
      A.setString("cmd", it->first, count);
      A.setInt("pid", it->second, count);
      ++count;
    }
    A.setRCount(count);
    f.master = false; // Keep the shm page around, don't kill it
  }

  /// Reads active connectors from the shared memory pages
  void loadActiveConnectors(){
    IPC::sharedPage f("MstCnns", 4096, false, false);
    const Util::RelAccX A(f.mapped, false);
    if (A.isReady()){
      INFO_MSG("Reloading existing connectors to complete rolling restart");
      for (uint32_t i = 0; i < A.getRCount(); ++i){
        char *p = A.getPointer("cmd", i);
        if (p != 0 && p[0] != 0){
          currentConnectors[p] = A.getInt("pid", i);
          Util::Procs::remember(A.getInt("pid", i));
          kill(A.getInt("pid", i), SIGUSR1);
        }
      }
    }
  }

  /// Deletes the shared memory page with connector information
  /// in preparation of shutdown.
  void prepareActiveConnectorsForShutdown(){
    IPC::sharedPage f("MstCnns", 4096, false, false);
    if (f){f.master = true;}
  }

  /// Forgets all active connectors, preventing them from being killed,
  /// in preparation of reload.
  void prepareActiveConnectorsForReload(){
    saveActiveConnectors();
    std::map<std::string, pid_t>::iterator it;
    for (it = currentConnectors.begin(); it != currentConnectors.end(); ++it){
      Util::Procs::forget(it->second);
    }
    currentConnectors.clear();
  }

  static inline void buildPipedPart(JSON::Value &p, std::deque<std::string> &argDeq, const JSON::Value &argset){
    jsonForEachConst(argset, it){
      if (it->isMember("option") && p.isMember(it.key())){
        if (!it->isMember("type")){
          if (JSON::Value(p[it.key()]).asBool()){
            argDeq.push_back((*it)["option"]);
          }
          continue;
        }
        if ((*it)["type"].asStringRef() == "str" && !p[it.key()].isString()){
          p[it.key()] = p[it.key()].asString();
        }
        if ((*it)["type"].asStringRef() == "uint" || (*it)["type"].asStringRef() == "int" ||
            (*it)["type"].asStringRef() == "debug"){
          p[it.key()] = JSON::Value(p[it.key()].asInt()).asString();
        }
        if ((*it)["type"].asStringRef() == "inputlist" && p[it.key()].isArray()){
          jsonForEach(p[it.key()], iVal){
            argDeq.push_back((*it)["option"]);
            argDeq.push_back(iVal->asString());
          }
          continue;
        }
        if (p[it.key()].asStringRef().size() > 0){
          argDeq.push_back((*it)["option"]);
          argDeq.push_back(p[it.key()].asString());
        }else{
          argDeq.push_back((*it)["option"]);
        }
      }
    }
  }

  static inline void buildPipedArguments(JSON::Value &p, std::deque<std::string> &argDeq, const JSON::Value &capabilities){
    static std::string tmparg;
    tmparg = std::string("MistOut") + p["connector"].asStringRef();
    if (!Util::Procs::HasMistBinary(tmparg)){
      tmparg = std::string("MistConn") + p["connector"].asStringRef();
      if (!Util::Procs::HasMistBinary(tmparg)) {
        return;
      }
    }
    argDeq.push_back(tmparg);
    const JSON::Value &pipedCapa = capabilities["connectors"][p["connector"].asStringRef()];
    if (pipedCapa.isMember("required")){buildPipedPart(p, argDeq, pipedCapa["required"]);}
    if (pipedCapa.isMember("optional")){buildPipedPart(p, argDeq, pipedCapa["optional"]);}
  }

  ///\brief Checks current protocol configuration, updates state of enabled connectors if
  /// neccessary. \param p An object containing all protocols. \param capabilities An object
  /// containing the detected capabilities. \returns True if any action was taken
  ///
  /// \triggers
  /// The `"OUTPUT_START"` trigger is global, and is ran whenever a new protocol listener is
  /// started. It cannot be cancelled. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// output listener commandline
  /// ~~~~~~~~~~~~~~~
  /// The `"OUTPUT_STOP"` trigger is global, and is ran whenever a protocol listener is terminated.
  /// It cannot be cancelled. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// output listener commandline
  /// ~~~~~~~~~~~~~~~
  bool CheckProtocols(JSON::Value &p, const JSON::Value &capabilities){
    std::set<std::string> runningConns;

    // used for building args
    int err = fileno(stderr);

    std::string tmp;

    jsonForEach(p, ait){
      std::string prevOnline = (*ait)["online"].asString();
      const std::string &connName = (*ait)["connector"].asStringRef();
      // do not further parse if there's no connector name
      if (!(*ait).isMember("connector") || connName == ""){
        (*ait)["online"] = "Missing connector name";
        continue;
      }
      // ignore connectors that are not installed
      if (!capabilities.isMember("connectors") || !capabilities["connectors"].isMember(connName)){
        (*ait)["online"] = "Not installed";
        if ((*ait)["online"].asString() != prevOnline){
          Log("WARN",
              connName + " connector is enabled but doesn't exist on system! Ignoring connector.");
        }
        continue;
      }
      if (capabilities["connectors"][connName].isMember("PUSHONLY")){
        (*ait)["online"] = "Push-only";
        if ((*ait)["online"].asString() != prevOnline){
          Log("WARN",
              connName + " connector is enabled but can only be used by the pushing system! Ignoring connector.");
        }
        continue;
      }
      // list connectors that go through HTTP as 'enabled' without actually running them.
      const JSON::Value &connCapa = capabilities["connectors"][connName];
      if (connCapa.isMember("socket") || (connCapa.isMember("deps") && connCapa["deps"].asStringRef() == "HTTP")){
        (*ait)["online"] = "Enabled";
        continue;
      }
      // check required parameters, skip if anything is missing
      if (connCapa.isMember("required")){
        bool gotAll = true;
        jsonForEachConst(connCapa["required"], it){
          if (!(*ait).isMember(it.key()) || (*ait)[it.key()].asStringRef().size() < 1){
            gotAll = false;
            (*ait)["online"] = "Invalid configuration";
            if ((*ait)["online"].asString() != prevOnline){
              Log("WARN", connName + " connector is missing required parameter " + it.key() + "! Ignoring connector.");
            }
            break;
          }
        }
        if (!gotAll){continue;}
      }
      // remove current online status
      (*ait).removeMember("online");
      /// \todo Check dependencies?
      // set current online status
      std::string myCmd = (*ait).toString();
      runningConns.insert(myCmd);
      if (currentConnectors.count(myCmd) && Util::Procs::isActive(currentConnectors[myCmd])){
        (*ait)["online"] = 1;
        // Reload connectors that need it
        if (needsReload.count(ait.num())){
          kill(currentConnectors[myCmd], SIGUSR1);
          needsReload.erase(ait.num());
        }
      }else{
        (*ait)["online"] = 0;
      }
    }

    bool action = false;
    // shut down deleted/changed connectors
    std::map<std::string, pid_t>::iterator it;
    if (currentConnectors.size()){
      for (it = currentConnectors.begin(); it != currentConnectors.end(); it++){
        if (!runningConns.count(it->first)){
          if (Util::Procs::isActive(it->second)){
            Log("CONF", "Stopping connector " + it->first);
            action = true;
            Util::Procs::Stop(it->second);
            Triggers::doTrigger("OUTPUT_STOP", it->first); // LTS
          }
          currentConnectors.erase(it);
          if (!currentConnectors.size()){break;}
          it = currentConnectors.begin();
        }
      }
    }

    // start up new/changed connectors
    while (runningConns.size() && conf.is_active){
      if (!currentConnectors.count(*runningConns.begin()) ||
          !Util::Procs::isActive(currentConnectors[*runningConns.begin()])){
        Log("CONF", "Starting connector: " + *runningConns.begin());
        action = true;
        std::deque<std::string> argDeq;
        // get args for this connector
        JSON::Value p = JSON::fromString(*runningConns.begin());
        buildPipedArguments(p, argDeq, capabilities);
        // start piped w/ generated args
        currentConnectors[*runningConns.begin()] = Util::Procs::StartPipedMist(argDeq, 0, 0, &err);
        Triggers::doTrigger("OUTPUT_START", *runningConns.begin()); // LTS
      }
      runningConns.erase(runningConns.begin());
    }
    if (action){saveActiveConnectors();}
    return action;
  }

}// namespace Controller
