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

#include <iostream>
#include <unistd.h>


///\brief Holds everything unique to the controller.
namespace Controller {

  static std::map<std::string, pid_t> currentConnectors; ///<The currently running connectors.

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
  
  static inline void builPipedPart(JSON::Value & p, char * argarr[], int & argnum, JSON::Value & argset){
    for (JSON::ObjIter it = argset.ObjBegin(); it != argset.ObjEnd(); ++it){
      if (it->second.isMember("option")){
        if (p.isMember(it->first)){
          if (it->second.isMember("type")){
            if (it->second["type"].asStringRef() == "str" && !p[it->first].isString()){
              p[it->first] = p[it->first].asString();
            }
            if ((it->second["type"].asStringRef() == "uint" || it->second["type"].asStringRef() == "int") && !p[it->first].isInt()){
              p[it->first] = p[it->first].asString();
            }
          }
          if (p[it->first].asStringRef().size() > 0){
            argarr[argnum++] = (char*)(it->second["option"].c_str());
            argarr[argnum++] = (char*)(p[it->first].c_str());
          }
        }else{
          if (it->first == "debug"){
            static std::string debugLvlStr;
            debugLvlStr = JSON::Value((long long)Util::Config::printDebugLevel).asString();
            argarr[argnum++] = (char*)(it->second["option"].c_str());
            argarr[argnum++] = (char*)debugLvlStr.c_str();
          }
        }
      }
    }
  }
  
  static inline void buildPipedArguments(const std::string & proto, char * argarr[], JSON::Value & capabilities){
    JSON::Value p = JSON::fromString(proto);
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
    JSON::Value & pipedCapa = capabilities["connectors"][p["connector"].asStringRef()];
    if (pipedCapa.isMember("required")){builPipedPart(p, argarr, argnum, pipedCapa["required"]);}
    if (pipedCapa.isMember("optional")){builPipedPart(p, argarr, argnum, pipedCapa["optional"]);}
  }
  
  ///\brief Checks current protocol configuration, updates state of enabled connectors if neccessary.
  ///\param p An object containing all protocols.
  ///\param capabilities An object containing the detected capabilities.
  ///\returns True if any action was taken
  bool CheckProtocols(JSON::Value & p, JSON::Value & capabilities){
    std::set<std::string> runningConns;

    // used for building args
    int zero = 0;
    int out = fileno(stdout);
    int err = fileno(stderr);
    char * argarr[15];	// approx max # of args (with a wide margin)
    int i;

    std::string tmp;
    long long counter = 0;

    for (JSON::ArrIter ait = p.ArrBegin(); ait != p.ArrEnd(); ait++){
      counter = ait - p.ArrBegin();
      std::string prevOnline = ( *ait)["online"].asString();
      const std::string & connName = (*ait)["connector"].asStringRef();
      //do not further parse if there's no connector name
      if ( !(*ait).isMember("connector") || connName == ""){
        ( *ait)["online"] = "Missing connector name";
        continue;
      }
      //ignore connectors that are not installed
      if ( !capabilities["connectors"].isMember(connName)){
        ( *ait)["online"] = "Not installed";
        if (( *ait)["online"].asString() != prevOnline){
          Log("WARN", connName + " connector is enabled but doesn't exist on system! Ignoring connector.");
        }
        continue;
      }
      //list connectors that go through HTTP as 'enabled' without actually running them.
      JSON::Value & connCapa = capabilities["connectors"][connName];
      if (connCapa.isMember("socket") || (connCapa.isMember("deps") && connCapa["deps"].asStringRef() == "HTTP")){
        ( *ait)["online"] = "Enabled";
        continue;
      }
      //check required parameters, skip if anything is missing
      if (connCapa.isMember("required")){
        bool gotAll = true;
        for (JSON::ObjIter it = connCapa["required"].ObjBegin(); it != connCapa["required"].ObjEnd(); ++it){
          if ( !(*ait).isMember(it->first) || (*ait)[it->first].asStringRef().size() < 1){
            gotAll = false;
            ( *ait)["online"] = "Invalid configuration";
            if (( *ait)["online"].asString() != prevOnline){
              Log("WARN", connName + " connector is missing required parameter " + it->first + "! Ignoring connector.");
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
    for (it = currentConnectors.begin(); it != currentConnectors.end(); it++){
      if (!runningConns.count(it->first)){
        if (Util::Procs::isActive(it->second)){
          Log("CONF", "Stopping connector " + it->first);
          action = true;
          Util::Procs::Stop(it->second);
        }
        currentConnectors.erase(it);
        it = currentConnectors.begin();
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
        buildPipedArguments(*runningConns.begin(), (char **)&argarr, capabilities);
        // start piped w/ generated args
        currentConnectors[*runningConns.begin()] = Util::Procs::StartPiped(argarr, &zero, &out, &err);
      }
      runningConns.erase(runningConns.begin());
    }
    return action;
  }

}

