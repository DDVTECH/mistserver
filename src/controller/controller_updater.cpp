/// \file controller_updater.cpp
/// Contains all code for the controller updater.

#include "controller_connectors.h"
#include "controller_storage.h"
#include "controller_updater.h"
#include <fstream>  //for files
#include <iostream> //for stdio
#include <mist/auth.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/downloader.h>
#include <mist/encode.h>
#include <mist/http_parser.h>
#include <mist/procs.h>
#include <mist/timing.h>
#include <signal.h>   //for raise
#include <sys/stat.h> //for chmod
#include <time.h>     //for time
#include <unistd.h>   //for unlink

#define UPDATE_INTERVAL 3600
#ifndef SHARED_SECRET
#define SHARED_SECRET "empty"
#endif

tthread::mutex updaterMutex;
uint8_t updatePerc = 0;
JSON::Value updates;
HTTP::Downloader DL;

bool updaterProgressCallback(){
  updatePerc = DL.getHTTP().getPercentage() * 95 / 100;
  return Util::Config::is_active;
}

namespace Controller{

  void updateThread(void *np){
    uint64_t updateChecker = Util::epoch() - UPDATE_INTERVAL;
    while (Controller::conf.is_active){
      if (Util::epoch() - updateChecker > UPDATE_INTERVAL || updatePerc){
        JSON::Value result = Controller::checkUpdateInfo();
        if (result.isMember("error")){
          FAIL_MSG("Error retrieving update information: %s", result["error"].asStringRef().c_str());
        }
        {// Lock the mutex, update the updates object
          tthread::lock_guard<tthread::mutex> guard(updaterMutex);
          updates = result;
        }
        if (!result["uptodate"] && updatePerc){
          if (result["url"].asStringRef().find(".zip") != std::string::npos){
            FAIL_MSG("Cannot auto-install update for this platform. Please download and install by "
                     "hand.");
            updatePerc = 0;
            continue;
          }
          Log("UPDR", "Downloading update...");
#ifdef SSL
          HTTP::URL url("https://releases.mistserver.org/update.php");
          if (DL.isProxied()){url.protocol = "http";}
#else
          HTTP::URL url("http://releases.mistserver.org/update.php");
#endif
          DL.dataTimeout = 50; // only timeout if no data received for 50 seconds
          DL.progressCallback = updaterProgressCallback;
          if (!DL.get(url.link(result["url"].asStringRef())) || !DL.isOk() || !DL.data().size()){
            FAIL_MSG("Download failed - aborting update");
            updatePerc = 0;
            continue;
          }
          updatePerc = 50;
          INFO_MSG("Downloaded update archive of %zuKiB", DL.data().size() / 1024);
          Log("UPDR", "Installing update...");
          std::string tmpDir = Util::getMyPath();
          char *tarArgs[4];
          tarArgs[0] = (char *)"tar";
          tarArgs[1] = (char *)"-xzC";
          tarArgs[2] = (char *)tmpDir.c_str();
          tarArgs[3] = 0;
          int tarIn = -1;
          pid_t tarPid = Util::Procs::StartPiped(tarArgs, &tarIn, 0, 0);
          if (!tarPid){
            FAIL_MSG("Could not extract update (is 'tar' installed..?)");
            updatePerc = 0;
            continue;
          }
          size_t tarProgress = 0;
          while (tarProgress < DL.data().size()){
            int written = write(tarIn, DL.data().data() + tarProgress,
                                std::min((size_t)4096, DL.data().size() - tarProgress));
            if (written < 0){
              FAIL_MSG("Could not (fully) extract update! Aborting.");
              break;
            }
            tarProgress += written;
            updatePerc = 95 + (5 * tarProgress) / DL.data().size();
          }
          close(tarIn);
          uint64_t waitCount = 0;
          while (Util::Procs::isActive(tarPid)){
            Util::wait(250);
            if (waitCount == 40){
              tarProgress = 0;
              WARN_MSG("Sending stop signal to tar process (may result in partial update!)");
              Util::Procs::Stop(tarPid);
            }
            if (waitCount == 80){
              WARN_MSG("Sending kill signal to tar process (may result in partial update!)");
              Util::Procs::Murder(tarPid);
              break;
            }
            ++waitCount;
          }
          updatePerc = 0;
          if (tarProgress == DL.data().size()){
            Log("UPDR", "Install complete, initiating rolling restart.");
            Util::Config::is_restarting = true;
            raise(SIGINT); // trigger restart
          }else{
            Log("UPDR", "Install did not fully complete. Not restarting.");
          }
          DL.data() = "";
        }
        updateChecker = Util::epoch();
      }
      Util::sleep(3000);
    }
  }

  void insertUpdateInfo(JSON::Value &ret){
    tthread::lock_guard<tthread::mutex> guard(updaterMutex);
    ret = updates;
    if (updatePerc){ret["progress"] = (uint16_t)updatePerc;}
  }

  /// Downloads the latest details on updates
  JSON::Value checkUpdateInfo(){
    JSON::Value ret;
    if (strlen(SHARED_SECRET) < 8){
      Log("UPDR", "Self-compiled build. Updater disabled.");
      ret["uptodate"] = 1;
      ret["needs_update"].null();
      ret["release"] = "Self-compiled";
      ret["version"] = "Unknown";
      ret["date"] = "Now";
      return ret;
    }
    JSON::Value updrInfo;
#ifdef SSL
    HTTP::URL url("https://releases.mistserver.org/update.php");
    if (DL.isProxied()){url.protocol = "http";}
#else
    HTTP::URL url("http://releases.mistserver.org/update.php");
#endif
    url.args = "rel=" + Encodings::URL::encode(RELEASE) + "&pass=" + Encodings::URL::encode(SHARED_SECRET) +
               "&iid=" + Encodings::URL::encode(instanceId);
    if (DL.get(url) && DL.isOk()){
      updrInfo = JSON::fromString(DL.data());
    }else{
      Log("UPDR", "Error getting update info: " + DL.getStatusText());
      ret["error"] = "Error getting update info: " + DL.getStatusText();
      return ret;
    }
    if (!updrInfo){
      Log("UPDR", "Could not retrieve update information from releases server.");
      ret["error"] = "Could not retrieve update information from releases server.";
    }
    if (updrInfo.isMember("error")){
      Log("UPDR", updrInfo["error"].asStringRef());
      ret["error"] = updrInfo["error"];
      return ret;
    }
    if (!updrInfo.isArray()){
      ret["error"] = "Received invalid version list from server. Unknown update status.";
      return ret;
    }
    ret["release"] = RELEASE;
    ret["version"] = updrInfo[0u][0u];
    ret["date"] = updrInfo[0u][1u];
    ret["url"] = updrInfo[0u][2u];
    ret["full_list"] = updrInfo;
    if (updrInfo[0u][0u].asStringRef() == PACKAGE_VERSION){
      ret["uptodate"] = 1;
    }else{
      ret["uptodate"] = 0;
    }
    return ret;
  }

  /// Causes the updater thread to download an update, if available
  void checkUpdates(){updatePerc = 1;}// CheckUpdates

}// namespace Controller
