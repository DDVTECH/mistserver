/// \file controller_updater.cpp
/// Contains all code for the controller updater.

#include <fstream> //for files
#include <iostream> //for stdio
#include <unistd.h> //for unlink
#include <sys/stat.h> //for chmod
#include <stdlib.h> //for srand, rand
#include <time.h> //for time
#include <signal.h> //for raise
#include <mist/http_parser.h>
#include <mist/socket.h>
#include <mist/auth.h>
#include <mist/timing.h>
#include <mist/config.h>
#include "controller_storage.h"
#include "controller_connectors.h"
#include "controller_updater.h"

namespace Controller {
  bool restarting = false;
  JSON::Value updates;
  std::string uniqId;

  std::string readFile(std::string filename){
    std::ifstream file(filename.c_str());
    if ( !file.good()){
      return "";
    }
    file.seekg(0, std::ios::end);
    unsigned int len = file.tellg();
    file.seekg(0, std::ios::beg);
    std::string out;
    out.reserve(len);
    unsigned int i = 0;
    while (file.good() && i++ < len){
      out += file.get();
    }
    file.close();
    return out;
  } //readFile

  bool writeFile(std::string filename, std::string & contents){
    unlink(filename.c_str());
    std::ofstream file(filename.c_str(), std::ios_base::trunc | std::ios_base::out);
    if ( !file.is_open()){
      return false;
    }
    file << contents;
    file.close();
    chmod(filename.c_str(), S_IRWXU | S_IRWXG);
    return true;
  } //writeFile

  /// \api
  /// `"update"` and `"checkupdate"` requests (LTS-only) are responded to as:
  /// ~~~~~~~~~~~~~~~{.js}
  /// {
  ///   "error": "Something went wrong", // 'Optional'
  ///   "release": "LTS64_99",
  ///   "version": "1.2 / 6.0.0",
  ///   "date": "January 5th, 2014",
  ///   "uptodate": 0,
  ///   "needs_update": ["MistBuffer", "MistController"], //Controller is guaranteed to be last
  ///   "MistController": "abcdef1234567890", //md5 sum of latest version
  ///   //... all other MD5 sums follow
  /// }
  /// ~~~~~~~~~~~~~~~
  /// Note that `"update"` will only list known information, while `"checkupdate"` triggers an information refresh from the update server.
  JSON::Value CheckUpdateInfo(){
    JSON::Value ret;
    
    if (uniqId == ""){
      srand(time(NULL));
      do{
        char meh = 64 + rand() % 62;
        uniqId += meh;
      }while(uniqId.size() < 16);
    }

    //initialize connection
    HTTP::Parser http;
    JSON::Value updrInfo;
    Socket::Connection updrConn("releases.mistserver.org", 80, true);
    if ( !updrConn){
      Log("UPDR", "Could not connect to releases.mistserver.org to get update information.");
      ret["error"] = "Could not connect to releases.mistserver.org to get update information.";
      return ret;
    }

    //retrieve update information
    http.url = "/getsums.php?verinfo=1&rel=" RELEASE "&pass=" SHARED_SECRET "&uniqId=" + uniqId;
    http.method = "GET";
    http.SetHeader("Host", "releases.mistserver.org");
    http.SetHeader("X-Version", PACKAGE_VERSION);
    updrConn.SendNow(http.BuildRequest());
    http.Clean();
    unsigned int startTime = Util::epoch();
    while ((Util::epoch() - startTime < 10) && (updrConn || updrConn.Received().size())){
      if (updrConn.spool() || updrConn.Received().size()){
        if ( *(updrConn.Received().get().rbegin()) != '\n'){
          std::string tmp = updrConn.Received().get();
          updrConn.Received().get().clear();
          if (updrConn.Received().size()){
            updrConn.Received().get().insert(0, tmp);
          }else{
            updrConn.Received().append(tmp);
          }
          continue;
        }
        if (http.Read(updrConn.Received().get())){
          updrInfo = JSON::fromString(http.body);
          break; //break out of while loop
        }
      }
    }
    updrConn.close();

    if (updrInfo){
      if (updrInfo.isMember("error")){
        Log("UPDR", updrInfo["error"].asStringRef());
        ret["error"] = updrInfo["error"];
        ret["uptodate"] = 1;
        return ret;
      }
      ret["release"] = RELEASE;
      if (updrInfo.isMember("version")){
        ret["version"] = updrInfo["version"];
      }
      if (updrInfo.isMember("date")){
        ret["date"] = updrInfo["date"];
      }
      ret["uptodate"] = 1;
      ret["needs_update"].null();
      
      // check if everything is up to date or not
      for (JSON::ObjIter it = updrInfo.ObjBegin(); it != updrInfo.ObjEnd(); it++){
        if (it->first.substr(0, 4) != "Mist"){
          continue;
        }
        ret[it->first] = it->second;
        if (it->second.asString() != Secure::md5(readFile(Util::getMyPath() + it->first))){
          ret["uptodate"] = 0;
          if (it->first.substr(0, 14) == "MistController"){
            ret["needs_update"].append(it->first);
          }else{
            ret["needs_update"].prepend(it->first);
          }
        }
      }
    }else{
      Log("UPDR", "Could not retrieve update information from releases server.");
      ret["error"] = "Could not retrieve update information from releases server.";
    }
    return ret;
  }

  /// Calls CheckUpdateInfo(), uses the resulting JSON::Value to download any needed updates.
  /// Will shut down the server if the JSON::Value contained a "shutdown" member.
  void CheckUpdates(){
    JSON::Value updrInfo = CheckUpdateInfo();
    if (updrInfo.isMember("error")){
      Log("UPDR", "Error retrieving update information: " + updrInfo["error"].asString());
      return;
    }

    if (updrInfo.isMember("shutdown")){
      Log("DDVT", "Shutting down: " + updrInfo["shutdown"].asString());
      restarting = false;
      raise(SIGINT); //trigger shutdown
      return;
    }

    if (updrInfo["uptodate"]){
      //nothing to do
      return;
    }

    //initialize connection
    Socket::Connection updrConn("releases.mistserver.org", 80, true);
    if ( !updrConn){
      Log("UPDR", "Could not connect to releases.mistserver.org.");
      return;
    }

    //loop through the available components, update them
    for (JSON::ArrIter it = updrInfo["needs_update"].ArrBegin(); it != updrInfo["needs_update"].ArrEnd(); it++){
      updateComponent(it->asStringRef(), updrInfo[it->asStringRef()].asStringRef(), updrConn);
    }
    updrConn.close();
  } //CheckUpdates
  
  /// Attempts to download an update for the listed component.
  /// \param component Filename of the component being checked.
  /// \param md5sum The MD5 sum of the latest version of this file.
  /// \param updrConn An connection to releases.mistserver.org to (re)use. Will be (re)opened if closed.
  void updateComponent(const std::string & component, const std::string & md5sum, Socket::Connection & updrConn){
    Log("UPDR", "Downloading update for " + component);
    std::string new_file;
    HTTP::Parser http;
    http.url = "/getfile.php?rel=" RELEASE "&pass=" SHARED_SECRET "&file=" + component;
    http.method = "GET";
    http.SetHeader("Host", "releases.mistserver.org");
    if ( !updrConn){
      updrConn = Socket::Connection("releases.mistserver.org", 80, true);
      if ( !updrConn){
        Log("UPDR", "Could not connect to releases.mistserver.org for file download.");
        return;
      }
    }
    http.SendRequest(updrConn);
    http.Clean();
    unsigned int startTime = Util::epoch();
    while ((Util::epoch() - startTime < 90) && (updrConn || updrConn.Received().size())){
      if (updrConn.spool() || updrConn.Received().size()){
        if ( *(updrConn.Received().get().rbegin()) != '\n'){
          std::string tmp = updrConn.Received().get();
          updrConn.Received().get().clear();
          if (updrConn.Received().size()){
            updrConn.Received().get().insert(0, tmp);
          }else{
            updrConn.Received().append(tmp);
          }
        }
        if (http.Read(updrConn.Received().get())){
          new_file = http.body;
          break; //break out of while loop
        }
      }
    }
    http.Clean();
    if (new_file == ""){
      Log("UPDR", "Could not retrieve new version of " + component + " - retrying next time.");
      return;
    }
    if (Secure::md5(new_file) != md5sum){
      Log("UPDR", "Checksum "+Secure::md5(new_file)+" of " + component + " does not match "+md5sum+" - retrying next time.");
      return;
    }
    if (writeFile(Util::getMyPath() + component, new_file)){
      Controller::UpdateProtocol(component);
      if (component == "MistController"){
        restarting = true;
        raise(SIGINT); //trigger restart
      }
      Log("UPDR", "New version of " + component + " installed.");
    }else{
      Log("UPDR", component + " could not be updated! (No write access to file?)");
    }
  }
  
  
} //Controller namespace
