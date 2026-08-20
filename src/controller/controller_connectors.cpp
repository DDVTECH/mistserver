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
#include <string>
#include <sys/stat.h> //stat
#include <unistd.h>

///\brief Holds everything unique to the controller.
namespace Controller{

  class ConnectorStartAttempt {
    public:
      std::string cmd;
      pid_t pid;
      uint64_t bMs;
  };
  static std::deque<ConnectorStartAttempt> startAttempts; ///< Tracks recent connector start attempts

  static std::map<std::string, pid_t> currentConnectors; ///< The currently running connectors.

  static std::string effectiveInterfaceAndPort(const JSON::Value & cnf, const JSON::Value & capabilities) {
    if (!cnf.isMember("connector") || !cnf["connector"]) { return ""; }
    const JSON::Value & connCapa = capabilities["connectors"][cnf["connector"].asStringRef()];
    if (!connCapa.isMember("optional") || !connCapa["optional"].isMember("port") || !connCapa["optional"].isMember("interface")) {
      return "";
    }
    std::string iFace = connCapa["optional"]["interface"]["default"].asString();
    std::string port = connCapa["optional"]["port"]["default"].asString();
    if (cnf.isMember("port")) { port = cnf["port"].asString(); }
    if (cnf.isMember("interface")) { iFace = cnf["interface"].asString(); }
    return iFace + ":" + port;
  }

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
    bool action = false;
    std::set<std::string> hostPorts;
    std::set<std::string> runningConns;

    // Clear start attempts more than a minute ago
    while (startAttempts.size() && startAttempts.begin()->bMs + 60000 < Util::bootMS()) { startAttempts.pop_front(); }

    {
      std::set<std::string> toDelete;
      // shut down deleted/changed connectors
      for (auto & it : currentConnectors) {
        if (Util::Procs::isActive(it.second)) {
          // Mark the port in use
          std::string ifacePort = effectiveInterfaceAndPort(JSON::Value(PARSEJSON, it.first), capabilities);
          if (ifacePort.size()) { hostPorts.insert(ifacePort); }
        } else {
          // Not running - remove the entry
          toDelete.insert(it.first);
        }
      }
      // Delete entries we want to remove (outside of the loop so the iterator doesn't get broken)
      for (auto & it : toDelete) { currentConnectors.erase(it); }
    }

    jsonForEach (p, ait) {
      std::string prevError = ait->isMember("error") ? (*ait)["error"].asString() : "";
      const std::string & connName = (*ait)["connector"].asStringRef();
      // do not further parse if there's no connector name
      if (!(*ait).isMember("connector") || connName == "") {
        (*ait)["online"] = 0;
        (*ait)["error"] = "Missing connector name";
        continue;
      }
      // ignore connectors that are not installed
      if (!capabilities.isMember("connectors") || !capabilities["connectors"].isMember(connName)) {
        (*ait)["online"] = 0;
        (*ait)["error"] = "Not installed";
        if ((*ait)["error"].asString() != prevError) {
          WARN_MSG("%s connector is enabled but doesn't exist on system! Ignoring connector.", connName.c_str());
        }
        continue;
      }
      if (capabilities["connectors"][connName].isMember("PUSHONLY")) {
        (*ait)["online"] = 0;
        (*ait)["error"] = "Push-only";
        if ((*ait)["error"].asString() != prevError) {
          WARN_MSG("%s connector is enabled but can only be used by the pushing system! Ignoring connector.", connName.c_str());
        }
        continue;
      }
      // list connectors that go through HTTP as 'enabled' without actually running them.
      const JSON::Value & connCapa = capabilities["connectors"][connName];
      if (connCapa.isMember("socket") ||
          (connCapa.isMember("deps") && connCapa["deps"].asStringRef() == "HTTP" && !connCapa.isMember("provides_dependency"))) {
        (*ait)["online"] = 1; // These are not actively running, consider them "available"
        continue;
      }
      // check required parameters, skip if anything is missing
      if (connCapa.isMember("required")) {
        bool gotAll = true;
        jsonForEachConst (connCapa["required"], it) {
          if (!(*ait).isMember(it.key()) || (*ait)[it.key()].isNull() ||
              ((*ait)[it.key()].isString() && !(*ait)[it.key()].asStringRef().size()) ||
              ((*ait)[it.key()].isArray() && (!(*ait)[it.key()].size() || !(*ait)[it.key()][0u].isString())) ||
              (!(*ait)[it.key()].isString() && !(*ait)[it.key()].isArray())) {
            gotAll = false;
            (*ait)["online"] = 0;
            (*ait)["error"] = "Invalid configuration";
            if ((*ait)["error"].asString() != prevError) {
              WARN_MSG("%s connector is missing required parameter %s! Ignoring connector.", connName.c_str(), it.key().c_str());
            }
            break;
          }
        }
        if (!gotAll) { continue; }
      }
      // remove current online status and/or error
      (*ait).removeMember("online");
      (*ait).removeMember("error");
      /// \todo Check dependencies?
      // set current online status
      std::string myCmd = (*ait).toString();
      runningConns.insert(myCmd);
      if (currentConnectors.count(myCmd)) {
        (*ait)["online"] = 1;
      } else {

        std::string bin = Util::getMyPath() + "MistOut" + (*ait)["connector"].asStringRef();

        // Abort if binary not found
        struct stat buf;
        if (::stat(bin.c_str(), &buf)) {
          INFO_MSG("Cannot find connector executable: %s", bin.c_str());
          (*ait)["online"] = 0;
          (*ait)["error"] = "Binary not found";
          if ((*ait)["error"].asString() != prevError) {
            WARN_MSG("%s connector binary is missing!", connName.c_str());
          }
          continue;
        }

        std::string ifacePort = effectiveInterfaceAndPort(*ait, capabilities);
        if (hostPorts.count(ifacePort)) {
          INFO_MSG("Interface / port combination already in use: %s", ifacePort.c_str());
          (*ait)["online"] = 0;
          (*ait)["error"] = "Port in use";
          if ((*ait)["error"].asString() != prevError) {
            WARN_MSG("%s connector port already in use!", connName.c_str());
          }
          continue;
        }

        size_t recentAttempts = 0;
        uint64_t lastAttempt = 0;
        std::set<uint64_t> prevPids;
        for (auto & it : startAttempts) {
          if (it.cmd == myCmd) {
            lastAttempt = it.bMs;
            ++recentAttempts;
            prevPids.insert(it.pid);
          }
        }
        if (Util::bootMS() - lastAttempt < recentAttempts * 1000) {
          (*ait)["online"] = 0;
          (*ait)["error"] = findLastErrorForPids(prevPids);
          if (!(*ait)["error"]) { (*ait)["error"] = "Restart looping"; }
          continue;
        }

        std::deque<std::string> args;
        args.push_back(bin);
        Util::optionsToArguments(*ait, capabilities["connectors"][(*ait)["connector"].asStringRef()], args);

        int err = 2; // stderr goes to current stderr
        pid_t newPid = Util::Procs::StartPiped(args, 0, 0, &err);
        if (newPid > 1) {
          LOG_MSG("CONF", "Started connector: %s", myCmd.c_str());
          action = true;
          currentConnectors[myCmd] = newPid;
          Triggers::doTrigger("OUTPUT_START", myCmd);
          hostPorts.insert(ifacePort);
          (*ait)["online"] = 2;
          (*ait)["error"] = "Starting...";
          startAttempts.push_back({myCmd, newPid, Util::bootMS()});
        } else {
          WARN_MSG("Failed to start connector: %s", myCmd.c_str());
          (*ait)["online"] = 0;
          (*ait)["error"] = "Cannot execute process";
        }
      }
    }

    // Shut down anything running that shouldn't be
    for (auto & it : currentConnectors) {
      // If it should not be running - shut it down
      if (!runningConns.count(it.first)) {
        LOG_MSG("CONF", "Stopping connector %s", it.first.c_str());
        action = true;
        Util::Procs::Stop(it.second);
        Triggers::doTrigger("OUTPUT_STOP", it.first);
      }
    }

    if (action){saveActiveConnectors();}
    return action;
  }

}// namespace Controller
