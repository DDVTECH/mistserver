#include <stdio.h> // cout, cerr
#include <string> 
#include <cstring>   // strcpy
#include <sys/stat.h> //stat
#include <mist/json.h>
#include <mist/config.h>
#include <mist/procs.h>
#include <mist/timing.h>
#include <mist/tinythread.h>
#include <mist/defines.h>
#include "controller_storage.h"
#include "controller_connectors.h"
#include <mist/triggers.h>
#include <mist/shared_memory.h>
#include <mist/util.h>

#include <iostream>
#include <unistd.h>


///\brief Holds everything unique to the controller.
namespace Controller {

  static std::map<std::string, pid_t> currentConnectors; ///<The currently running connectors.

  /// Updates the shared memory page with active connectors
  void saveActiveConnectors(){
    IPC::sharedPage f("MstCnns", 4096, true, false);
    if (!f.mapped){
      FAIL_MSG("Could not store connector data!");
      return;
    }
    memset(f.mapped, 0, 32);
    Util::RelAccX A(f.mapped, false);
    A.addField("cmd", RAX_128STRING);
    A.addField("pid", RAX_64UINT);
    A.setReady();
    uint32_t count = 0;
    std::map<std::string, pid_t>::iterator it;
    for (it = currentConnectors.begin(); it != currentConnectors.end(); ++it){
      A.setString("cmd", it->first, count);
      A.setInt("pid", it->second, count);
      ++count;
    }
    A.setRCount(count);
    f.master = false;//Keep the shm page around, don't kill it
  }

  /// Reads active connectors from the shared memory pages
  void loadActiveConnectors(){
    IPC::sharedPage f("MstCnns", 4096, false, false);
    const Util::RelAccX A(f.mapped, false);
    if (A.isReady()){
      for (uint32_t i = 0; i < A.getRCount(); ++i){
        char * p = A.getPointer("cmd", i);
        if (p != 0 && p[0] != 0){
          currentConnectors[p] = A.getInt("pid", i);
          Util::Procs::remember(A.getInt("pid", i));
        }
      }
    }
  }

  /// Deletes the shared memory page with connector information
  /// in preparation of shutdown.
  void prepareActiveConnectorsForShutdown(){
    IPC::sharedPage f("MstCnns", 4096, true, false);
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

  ///\brief Checks if the binary mentioned in the protocol argument is currently active, if so, restarts it.
  ///\param protocol The protocol to check.
  void UpdateProtocol(std::string protocol){
    std::map<std::string, pid_t>::iterator iter;
    for (iter = currentConnectors.begin(); iter != currentConnectors.end(); iter++){
      if (iter->first.substr(0, protocol.size()) == protocol){
        Log("CONF", "Killing connector for update: " + iter->first);
        Util::Procs::Stop(iter->second);
      }
    }
  }
  
  static inline void builPipedPart(JSON::Value & p, char * argarr[], int & argnum, const JSON::Value & argset){
    jsonForEachConst(argset, it) {
      if (it->isMember("option")){
        if (p.isMember(it.key())){
          p[it.key()] = p[it.key()].asString();
          if (p[it.key()].asStringRef().size() > 0){
            argarr[argnum++] = (char*)((*it)["option"].asStringRef().c_str());
            argarr[argnum++] = (char*)(p[it.key()].asStringRef().c_str());
          }
        }else{
          if (it.key() == "debug"){
            static std::string debugLvlStr;
            debugLvlStr = JSON::Value((long long)Util::Config::printDebugLevel).asString();
            argarr[argnum++] = (char*)((*it)["option"].asStringRef().c_str());
            argarr[argnum++] = (char*)debugLvlStr.c_str();
          }
        }
      }
    }
  }
  
  static inline void buildPipedArguments(JSON::Value & p, char * argarr[], const JSON::Value & capabilities){
    int argnum = 0;
    static std::string tmparg;
    tmparg = Util::getMyPath() + std::string("MistOut") + p["connector"].asStringRef();
    struct stat buf;
    if (::stat(tmparg.c_str(), &buf) != 0){
      tmparg = Util::getMyPath() + std::string("MistConn") + p["connector"].asStringRef();
    }
    if (::stat(tmparg.c_str(), &buf) != 0){
      return;
    }
    argarr[argnum++] = (char*)tmparg.c_str();
    const JSON::Value & pipedCapa = capabilities["connectors"][p["connector"].asStringRef()];
    if (pipedCapa.isMember("required")){builPipedPart(p, argarr, argnum, pipedCapa["required"]);}
    if (pipedCapa.isMember("optional")){builPipedPart(p, argarr, argnum, pipedCapa["optional"]);}
  }
  
  ///\brief Checks current protocol configuration, updates state of enabled connectors if neccessary.
  ///\param p An object containing all protocols.
  ///\param capabilities An object containing the detected capabilities.
  ///\returns True if any action was taken
  /// 
  /// \triggers 
  /// The `"OUTPUT_START"` trigger is global, and is ran whenever a new protocol listener is started. It cannot be cancelled. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// output listener commandline
  /// ~~~~~~~~~~~~~~~
  /// The `"OUTPUT_STOP"` trigger is global, and is ran whenever a protocol listener is terminated. It cannot be cancelled. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// output listener commandline
  /// ~~~~~~~~~~~~~~~
  bool CheckProtocols(JSON::Value & p, const JSON::Value & capabilities){
    std::set<std::string> runningConns;

    // used for building args
    int zero = 0;
    int out = fileno(stdout);
    int err = fileno(stderr);
    char * argarr[15];	// approx max # of args (with a wide margin)
    int i;

    std::string tmp;

    jsonForEach(p, ait) {
      std::string prevOnline = (*ait)["online"].asString();
      const std::string & connName = (*ait)["connector"].asStringRef();
      //do not further parse if there's no connector name
      if ( !(*ait).isMember("connector") || connName == ""){
        ( *ait)["online"] = "Missing connector name";
        continue;
      }
      //ignore connectors that are not installed
      if (!capabilities.isMember("connectors") || !capabilities["connectors"].isMember(connName)){
        ( *ait)["online"] = "Not installed";
        if (( *ait)["online"].asString() != prevOnline){
          Log("WARN", connName + " connector is enabled but doesn't exist on system! Ignoring connector.");
        }
        continue;
      }
      //list connectors that go through HTTP as 'enabled' without actually running them.
      const JSON::Value & connCapa = capabilities["connectors"][connName];
      if (connCapa.isMember("socket") || (connCapa.isMember("deps") && connCapa["deps"].asStringRef() == "HTTP")){
        ( *ait)["online"] = "Enabled";
        continue;
      }
      //check required parameters, skip if anything is missing
      if (connCapa.isMember("required")){
        bool gotAll = true;
        jsonForEachConst(connCapa["required"], it) {
          if ( !(*ait).isMember(it.key()) || (*ait)[it.key()].asStringRef().size() < 1){
            gotAll = false;
            ( *ait)["online"] = "Invalid configuration";
            if (( *ait)["online"].asString() != prevOnline){
              Log("WARN", connName + " connector is missing required parameter " + it.key() + "! Ignoring connector.");
            }
            break;
          }
        }
        if (!gotAll){continue;}
      }
      //remove current online status
      ( *ait).removeMember("online");
      /// \todo Check dependencies?
      //set current online status
      std::string myCmd = (*ait).toString();
      runningConns.insert(myCmd);
      if (currentConnectors.count(myCmd) && Util::Procs::isActive(currentConnectors[myCmd])){
        ( *ait)["online"] = 1;
      }else{
        ( *ait)["online"] = 0;
      }
    }

    bool action = false;
    //shut down deleted/changed connectors
    std::map<std::string, pid_t>::iterator it;
    if (currentConnectors.size()){
      for (it = currentConnectors.begin(); it != currentConnectors.end(); it++){
        if (!runningConns.count(it->first)){
          if (Util::Procs::isActive(it->second)){
            Log("CONF", "Stopping connector " + it->first);
            action = true;
            Util::Procs::Stop(it->second);
            Triggers::doTrigger("OUTPUT_STOP",it->first); //LTS
          }
          currentConnectors.erase(it);
          if (!currentConnectors.size()){
            break;
          }
          it = currentConnectors.begin();
        }
      }
    }

    //start up new/changed connectors
    while (runningConns.size() && conf.is_active){
      if (!currentConnectors.count(*runningConns.begin()) || !Util::Procs::isActive(currentConnectors[*runningConns.begin()])){
        Log("CONF", "Starting connector: " + *runningConns.begin());
        action = true;
        // clear out old args
        for (i=0; i<15; i++){argarr[i] = 0;}
        // get args for this connector
        JSON::Value p = JSON::fromString(*runningConns.begin());
        buildPipedArguments(p, (char **)&argarr, capabilities);
        // start piped w/ generated args
        currentConnectors[*runningConns.begin()] = Util::Procs::StartPiped(argarr, &zero, &out, &err);
        Triggers::doTrigger("OUTPUT_START", *runningConns.begin());//LTS
      }
      runningConns.erase(runningConns.begin());
    }
    if (action){saveActiveConnectors();}
    return action;
  }

}

