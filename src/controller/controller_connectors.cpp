#include <stdio.h> // cout, cerr
#include <string> 
#include <cstring>   // strcpy
#include <mist/json.h>
#include <mist/config.h>
#include <mist/procs.h>
#include <mist/timing.h>
#include "controller_storage.h"
#include "controller_connectors.h"

///\brief Holds everything unique to the controller.
namespace Controller {

  static std::map<std::string, std::string> currentConnectors; ///<The currently running connectors.



  ///\brief Checks if the binary mentioned in the protocol argument is currently active, if so, restarts it.
  ///\param protocol The protocol to check.
  void UpdateProtocol(std::string protocol){
    std::map<std::string, std::string>::iterator iter;
    for (iter = currentConnectors.begin(); iter != currentConnectors.end(); iter++){
      if (iter->second.substr(0, protocol.size()) == protocol){
        Log("CONF", "Restarting connector for update: " + iter->second);
        Util::Procs::Stop(iter->first);
        int i = 0;
        while (Util::Procs::isActive(iter->first) && i < 30){
          Util::sleep(100);
        }
        if (i >= 30){
          Log("WARN", "Connector still active 3 seconds after shutdown - delaying restart.");
        }else{
          Util::Procs::Start(iter->first, Util::getMyPath() + iter->second);
        }
        return;
      }
    }
  }



  void buildPipedArguments(JSON::Value & p, std::string conn, char * argarr[]){
    int argnum = 2;   //first two are progname and -n
    std::string arg;

    std::string conname;

    for (JSON::ArrIter ait = p.ArrBegin(); ait != p.ArrEnd(); ait++){

      conname = (std::string("MistConn") + ( *ait)["connector"].asString());
      conn = conn.substr(0, conn.find(" ") );

      if ( !( *ait).isMember("connector") || ( *ait)["connector"].asString() == "" || conn != conname){
        continue;
      }

      std::string tmppath =  Util::getMyPath() + std::string("MistConn") + ( *ait)["connector"].asString();
      argarr[0] = new char[ tmppath.size() + 1]; std::strncpy(argarr[0], tmppath.c_str(), tmppath.size() + 1);

      argarr[1] = new char[3]; std::strncpy(argarr[1], "-n\0", 3);

      if (( *ait).isMember("port") && ( *ait)["port"].asInt() != 0){
        arg = ( *ait)["port"].asString();
        argarr[argnum] = new char[3]; std::strncpy(argarr[argnum], "-p\0", 3); argnum++;
        argarr[argnum] = new char[arg.size() + 1]; std::strncpy(argarr[argnum], arg.c_str(), arg.size() + 1);  argnum++;
      }

      if (( *ait).isMember("interface") && ( *ait)["interface"].asString() != "" && ( *ait)["interface"].asString() != "0.0.0.0"){
        arg = ( *ait)["interface"].asString();
        argarr[argnum] = new char[3]; std::strncpy(argarr[argnum], "-i\0", 3); argnum++;
        argarr[argnum] = new char[arg.size() + 1]; std::strncpy(argarr[argnum], arg.c_str(), arg.size() + 1 );  argnum++;
      }

      if (( *ait).isMember("username") && ( *ait)["username"].asString() != "" && ( *ait)["username"].asString() != "root"){
        arg = ( *ait)["username"].asString();
        argarr[argnum] = new char[3]; std::strncpy(argarr[argnum], "-u\0", 3); argnum++;
        argarr[argnum] = new char[arg.size() + 1]; std::strncpy(argarr[argnum], arg.c_str(), arg.size() + 1);  argnum++;
      }

      if (( *ait).isMember("tracks") && ( *ait)["tracks"].asString() != ""){
        arg = ( *ait)["tracks"].asString();
        argarr[argnum] = new char[3]; std::strncpy(argarr[argnum], "-t\0", 3); argnum++;
        argarr[argnum] = new char[arg.size() + 1]; std::strncpy(argarr[argnum], arg.c_str(), arg.size() + 1);  argnum++;
      }

      if (( *ait).isMember("args") && ( *ait)["args"].asString() != ""){
        arg = ( *ait)["args"].asString();
        argarr[argnum] = new char[arg.size() + 1]; std::strncpy(argarr[argnum], arg.c_str(), arg.size() + 1);  argnum++;
      }
    }

    argarr[argnum] = NULL;
  }



  ///\brief Checks current protocol coguration, updates state of enabled connectors if neccesary.
  ///\param p An object containing all protocols.
  void CheckProtocols(JSON::Value & p){
    std::map<std::string, std::string> new_connectors;
    std::map<std::string, std::string>::iterator iter;
    bool haveHTTPgeneric = false;
    bool haveHTTPspecific = false;

    // used for building args
    int zero = 0;
    int out = fileno(stdout);
    int err = fileno(stderr);
    char * argarr[15];	// approx max # of args (with a wide margin)
    int i;

    std::string tmp;
    JSON::Value counter = (long long int)0;

    for (JSON::ArrIter ait = p.ArrBegin(); ait != p.ArrEnd(); ait++){
      if ( !( *ait).isMember("connector") || ( *ait)["connector"].asString() == ""){
        continue;
      }

      tmp = std::string("MistConn") + ( *ait)["connector"].asString();
      tmp += std::string(" -n");

      if (( *ait)["connector"].asString() == "HTTP"){
        haveHTTPgeneric = true;
      }
      if (( *ait)["connector"].asString() != "HTTP" && ( *ait)["connector"].asString().substr(0, 4) == "HTTP"){
        haveHTTPspecific = true;
      }

      if (( *ait).isMember("port") && ( *ait)["port"].asInt() != 0){
        tmp += std::string(" -p ") + ( *ait)["port"].asString();
      }

      if (( *ait).isMember("interface") && ( *ait)["interface"].asString() != "" && ( *ait)["interface"].asString() != "0.0.0.0"){
        tmp += std::string(" -i ") + ( *ait)["interface"].asString();

      }

      if (( *ait).isMember("username") && ( *ait)["username"].asString() != "" && ( *ait)["username"].asString() != "root"){
        tmp += std::string(" -u ") + ( *ait)["username"].asString();
      }

      if (( *ait).isMember("tracks") && ( *ait)["tracks"].asString() != ""){
        tmp += std::string(" -t \"") + ( *ait)["tracks"].asString() + "\"";
      }

      if (( *ait).isMember("args") && ( *ait)["args"].asString() != ""){
        tmp += std::string(" ") + ( *ait)["args"].asString();
      }

      counter = counter.asInt() + 1;
      new_connectors[std::string("Conn") + counter.asString()] = tmp;
      if (Util::Procs::isActive(std::string("Conn") + counter.asString())){
        ( *ait)["online"] = 1;
      }else{
        ( *ait)["online"] = 0;
      }
    }

    //shut down deleted/changed connectors
    for (iter = currentConnectors.begin(); iter != currentConnectors.end(); iter++){
      if (new_connectors.count(iter->first) != 1 || new_connectors[iter->first] != iter->second){
        Log("CONF", "Stopping connector: " + iter->second);
        Util::Procs::Stop(iter->first);
      }
    }

    //start up new/changed connectors
    for (iter = new_connectors.begin(); iter != new_connectors.end(); iter++){
      if (currentConnectors.count(iter->first) != 1 || currentConnectors[iter->first] != iter->second || !Util::Procs::isActive(iter->first)){
        Log("CONF", "Starting connector: " + iter->second);

        // clear out old args
        for (i=0;i<15;i++)
        {
          argarr[i] = NULL;
        }

        // get args for this connector
        buildPipedArguments(p, iter->second, (char **)&argarr);

        // start piped w/ generated args
	Util::Procs::StartPiped(iter->first, argarr, &zero, &out, &err);

      }
    }

    if (haveHTTPgeneric && !haveHTTPspecific){
      Log("WARN", "HTTP Connector is enabled but no HTTP-based protocols are active!");
    }
    if ( !haveHTTPgeneric && haveHTTPspecific){
      Log("WARN", "HTTP-based protocols will not work without the generic HTTP connector!");
    }

    //store new state
    currentConnectors = new_connectors;
  }

}
