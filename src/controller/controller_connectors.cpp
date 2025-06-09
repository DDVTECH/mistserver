#include "controller_connectors.h"

#include "controller_storage.h"

#include <mist/config.h>
#include <mist/defines.h>
#include <mist/json.h>
#include <mist/procs.h>
#include <mist/shared_memory.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/triggers.h>
#include <mist/util.h>

#include <cstring> // strcpy
#include <signal.h>
#include <stdio.h> // cout, cerr
#include <string>
#include <sys/stat.h> //stat
#include <unistd.h>

///\brief Holds everything unique to the controller.
namespace Controller{

  static std::set<size_t> needsReload; ///< List of connector indices that needs a reload
  static std::map<std::string, pid_t> currentConnectors; ///< The currently running connectors.

  void reloadProtocol(size_t indice){needsReload.insert(indice);}

  /// Updates the shared memory page with active connectors
  void saveActiveConnectors(bool forceOverride){
    IPC::sharedPage f(SHM_CONNECTORS, 128 * 1024, forceOverride, false);
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
      A.addField("cmd", RAX_512STRING);
      A.addField("pid", RAX_64UINT);
      A.setReady();
    }
    A.setRCount(currentConnectors.size());
    uint32_t count = 0;
    for (const auto & it : currentConnectors){
      A.setString("cmd", it.first, count);
      A.setInt("pid", it.second, count);
      ++count;
    }
    A.addRecords(count);
    f.master = false; // Keep the shm page around, don't kill it
  }

  /// Reads active connectors from the shared memory pages
  void loadActiveConnectors(){
    IPC::sharedPage f(SHM_CONNECTORS, 4096, false, false);
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
    IPC::sharedPage f(SHM_CONNECTORS, 4096, false, false);
    if (f){f.master = true;}
  }

  /// Forgets all active connectors, preventing them from being killed,
  /// in preparation of reload.
  void prepareActiveConnectorsForReload(){
    saveActiveConnectors();
    for (const auto & it : currentConnectors) { Util::Procs::forget(it.second); }
    currentConnectors.clear();
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
          WARN_MSG("%s connector is enabled but doesn't exist on system! Ignoring connector.", connName.c_str());
        }
        continue;
      }
      if (capabilities["connectors"][connName].isMember("PUSHONLY")){
        (*ait)["online"] = "Push-only";
        if ((*ait)["online"].asString() != prevOnline){
          WARN_MSG("%s connector is enabled but can only be used by the pushing system! Ignoring connector.", connName.c_str());
        }
        continue;
      }
      // list connectors that go through HTTP as 'enabled' without actually running them.
      const JSON::Value &connCapa = capabilities["connectors"][connName];
      if (connCapa.isMember("socket") || (connCapa.isMember("deps") && connCapa["deps"].asStringRef() == "HTTP" && !connCapa.isMember("provides_dependency"))){
        (*ait)["online"] = "Enabled";
        continue;
      }
      // check required parameters, skip if anything is missing
      if (connCapa.isMember("required")){
        bool gotAll = true;
        jsonForEachConst(connCapa["required"], it){
          if (!(*ait).isMember(it.key()) || (*ait)[it.key()].isNull() ||
              ((*ait)[it.key()].isString() && !(*ait)[it.key()].asStringRef().size()) ||
              ((*ait)[it.key()].isArray() && (!(*ait)[it.key()].size() || !(*ait)[it.key()][0u].isString())) ||
              (!(*ait)[it.key()].isString() && !(*ait)[it.key()].isArray())) {
            gotAll = false;
            (*ait)["online"] = "Invalid configuration";
            if ((*ait)["online"].asString() != prevOnline){
              WARN_MSG("%s connector is missing required parameter %s! Ignoring connector.", connName.c_str(), it.key().c_str());
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
            LOG_MSG("CONF", "Stopping connector %s", it->first.c_str());
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

        JSON::Value cnf = JSON::fromString(*runningConns.begin());

        std::string tmparg;
        struct stat buf;
        tmparg = Util::getMyPath() + "MistOut" + cnf["connector"].asStringRef();
        // Abort if binary not found
        if (::stat(tmparg.c_str(), &buf)) { continue; }

        std::deque<std::string> args;
        args.push_back(tmparg);
        Util::optionsToArguments(cnf, capabilities["connectors"][cnf["connector"].asStringRef()], args);

        int err = 2; // stderr goes to current stderr
        pid_t newPid = Util::Procs::StartPiped(args, 0, 0, &err);
        if (newPid) {
          LOG_MSG("CONF", "Started connector: %s", runningConns.begin()->c_str());
          action = true;
          currentConnectors[*runningConns.begin()] = newPid;
          Triggers::doTrigger("OUTPUT_START", *runningConns.begin());
        } else {
          WARN_MSG("Started connector: %s", runningConns.begin()->c_str());
        }
      }
      runningConns.erase(runningConns.begin());
    }
    if (action){saveActiveConnectors();}
    return action;
  }

}// namespace Controller
