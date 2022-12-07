#include "controller_services.h"
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

  static std::set<size_t> needsReload; ///< List of service indices that needs a reload
  static std::map<std::string, pid_t> currentservices; ///< The currently running services.

  void reloadService(size_t indice){
    needsReload.insert(indice);
  }

  /// Updates the shared memory page with active services
  void saveActiveServices(bool forceOverride){
    IPC::sharedPage f("MstServices", 4096, forceOverride, false);
    if (!f.mapped){
      if (!forceOverride){
        saveActiveServices(true);
        return;
      }
      if (!f.mapped){
        FAIL_MSG("Could not store service data!");
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
    for (it = currentservices.begin(); it != currentservices.end(); ++it){
      A.setString("cmd", it->first, count);
      A.setInt("pid", it->second, count);
      ++count;
    }
    A.setRCount(count);
    f.master = false; // Keep the shm page around, don't kill it
  }

  /// Reads active services from the shared memory pages
  void loadActiveServices(){
    IPC::sharedPage f("MstServices", 4096, false, false);
    const Util::RelAccX A(f.mapped, false);
    if (A.isReady()){
      INFO_MSG("Reloading existing services to complete rolling restart");
      for (uint32_t i = 0; i < A.getRCount(); ++i){
        char *p = A.getPointer("cmd", i);
        if (p != 0 && p[0] != 0){
          currentservices[p] = A.getInt("pid", i);
          Util::Procs::remember(A.getInt("pid", i));
          kill(A.getInt("pid", i), SIGUSR1);
        }
      }
    }
  }

  /// Deletes the shared memory page with service information
  /// in preparation of shutdown.
  void prepareActiveServicesForShutdown(){
    IPC::sharedPage f("MstServices", 4096, false, false);
    if (f){f.master = true;}
  }

  /// Forgets all active services, preventing them from being killed,
  /// in preparation of reload.
  void prepareActiveServicesForReload(){
    saveActiveServices();
    std::map<std::string, pid_t>::iterator it;
    for (it = currentservices.begin(); it != currentservices.end(); ++it){
      Util::Procs::forget(it->second);
    }
    currentservices.clear();
  }

  static inline void builPipedPart(JSON::Value &p, char *argarr[], int &argnum){
    jsonForEachConst(p["options"], it){
      if (it->isMember("option")){
        if (!it->isMember("type")){
          if (JSON::Value(p["options"]["value"]).asBool()){
            argarr[argnum++] = (char *)((*it)["option"].c_str());
          }
          continue;
        }
        if ((*it)["type"].asStringRef() == "str" && !p[it.key()].isString()){
          p["options"]["value"] = p["options"]["value"].asString();
        }
        if ((*it)["type"].asStringRef() == "uint" || (*it)["type"].asStringRef() == "int" ||
            (*it)["type"].asStringRef() == "debug"){
          p["options"]["value"] = JSON::Value(p["options"]["value"].asInt()).asString();
        }
        if ((*it)["type"].asStringRef() == "inputlist" && p["options"]["value"].isArray()){
          jsonForEach(p["options"]["value"], iVal){
            (*iVal) = iVal->asString();
            argarr[argnum++] = (char *)((*it)["option"].c_str());
            argarr[argnum++] = (char *)((*iVal).c_str());
          }
          continue;
        }
        if (p["options"]["value"].asStringRef().size() > 0){
          argarr[argnum++] = (char *)((*it)["option"].c_str());
          argarr[argnum++] = (char *)(p["options"]["value"].c_str());
        }else{
          argarr[argnum++] = (char *)((*it)["option"].c_str());
        }
      }
    }
  }

  static inline void buildPipedArguments(JSON::Value &p, char *argarr[]){
    int argnum = 0;
    static std::string tmparg;
    tmparg = Util::getMyPath() + p["cmd"].asStringRef();
    struct stat buf;
    if (::stat(tmparg.c_str(), &buf) != 0){return;}
    argarr[argnum++] = (char *)tmparg.c_str();
    builPipedPart(p, argarr, argnum);
  }

  ///\brief Checks current service configuration, updates state of enabled services if
  /// neccessary. \param p An object containing all services. \param capabilities An object
  /// containing the detected capabilities. \returns True if any action was taken
  ///
  /// \triggers
  /// The `"OUTPUT_START"` trigger is global, and is ran whenever a new service listener is
  /// started. It cannot be cancelled. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// output listener commandline
  /// ~~~~~~~~~~~~~~~
  /// The `"OUTPUT_STOP"` trigger is global, and is ran whenever a service listener is terminated.
  /// It cannot be cancelled. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// output listener commandline
  /// ~~~~~~~~~~~~~~~
  bool CheckService(JSON::Value &p){
    std::set<std::string> runningServices;

    // used for building args
    int err = fileno(stderr);
    char *argarr[15]; // approx max # of args (with a wide margin)
    int i;

    std::string tmp;

    jsonForEach(p, ait){
      std::string prevOnline = (*ait)["online"].asString();
      // do not further parse if there's no service name
      if (!(*ait).isMember("cmd") || !(*ait)["cmd"].asStringRef().size()){
        (*ait)["online"] = "Missing service name";
        continue;
      }

      // remove current online status
      (*ait).removeMember("online");
      /// \todo Check dependencies?
      // set current online status
      std::string myCmd = (*ait).toString();
      runningServices.insert(myCmd);
      if (currentservices.count(myCmd) && Util::Procs::isActive(currentservices[myCmd])){
        (*ait)["online"] = 1;
        // Reload services that need it
        if (needsReload.count(ait.num())){
          kill(currentservices[myCmd], SIGUSR1);
          needsReload.erase(ait.num());
        }
      }else{
        (*ait)["online"] = 0;
      }
    }

    bool action = false;
    // shut down deleted/changed services
    std::map<std::string, pid_t>::iterator it;
    if (currentservices.size()){
      for (it = currentservices.begin(); it != currentservices.end(); it++){
        if (!runningServices.count(it->first)){
          if (Util::Procs::isActive(it->second)){
            Log("CONF", "Stopping service " + it->first);
            action = true;
            Util::Procs::Stop(it->second);
            Triggers::doTrigger("OUTPUT_STOP", it->first); // LTS
          }
          currentservices.erase(it);
          if (!currentservices.size()){break;}
          it = currentservices.begin();
        }
      }
    }

    // start up new/changed services
    while (runningServices.size() && conf.is_active){
      if (!currentservices.count(*runningServices.begin()) ||
          !Util::Procs::isActive(currentservices[*runningServices.begin()])){
        Log("CONF", "Starting service: " + *runningServices.begin());
        action = true;
        // clear out old args
        for (i = 0; i < 15; i++){argarr[i] = 0;}
        // get args for this service
        JSON::Value p = JSON::fromString(*runningServices.begin());
        buildPipedArguments(p, (char **)&argarr);
        // start piped w/ generated args
        currentservices[*runningServices.begin()] = Util::Procs::StartPiped(argarr, 0, 0, &err);
        Triggers::doTrigger("OUTPUT_START", *runningServices.begin()); // LTS
      }
      runningServices.erase(runningServices.begin());
    }
    if (action){saveActiveServices();}
    return action;
  }

}// namespace Controller
