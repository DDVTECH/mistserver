/// \file controller_updater.cpp
/// Contains all code for the controller updater.

#include "controller_updater.h"

#include "controller_connectors.h"
#include "controller_storage.h"

#include <mist/auth.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/downloader.h>
#include <mist/encode.h>
#include <mist/http_parser.h>
#include <mist/procs.h>
#include <mist/timing.h>

#include <signal.h> //for raise
#include <sys/stat.h> //for chmod
#include <unistd.h> //for unlink

#ifndef SHARED_SECRET
#define SHARED_SECRET "empty"
#endif

uint8_t updatePerc = 0;
uint64_t lastUpdateCheck = 0;
JSON::Value updates;
HTTP::Downloader checkerDl;
HTTP::Downloader updaterDl;

namespace Controller {

  bool updateAfterNextCheck = false;

  size_t updaterCheck() {
    // Prevent calling more than once per 10 minutes
    if (Util::bootMS() < lastUpdateCheck + 600000) { return 3600000; }
    lastUpdateCheck = Util::bootMS();

    if (strlen(SHARED_SECRET) < 8 && std::string(RELEASE).substr(0, 4) != "Free") {
      LOG_MSG("UPDR", "Self-compiled build. Updater disabled.");
      updates.null();
      updates["uptodate"] = 1;
      updates["needs_update"].null();
      updates["release"] = "Self-compiled";
      updates["version"] = "Unknown";
      updates["date"] = "Now";
      return 0;
    }

    // Abort if we're already downloading something
    if (checkerDl.isEventLooping() || updaterDl.isEventLooping()) { return 3600000; }

    HTTP::URL url("http://releases.mistserver.org/update.php");
#ifdef SSL
    if (!checkerDl.isProxied()) { url.protocol = "https"; }
#endif
    std::map<std::string, std::string> args;
    args["rel"] = RELEASE;
    args["pass"] = SHARED_SECRET;
    args["iid"] = instanceId;
    url.args = HTTP::argStr(args, false);
    checkerDl.progressCallback = 0;
    checkerDl.getEventLooped(Controller::E, url, 5, [&]() {
      if (!checkerDl.isOk()) {
        updates.null();
        updates["error"] = "Could not retrieve update information from releases server.";
        return;
      }
      JSON::Value updrInfo = JSON::fromString(checkerDl.data());
      if (!updrInfo) { updrInfo["error"] = "Could not retrieve update information from releases server."; }
      if (updrInfo.isMember("error")) {
        LOG_MSG("UPDR", "%s", updrInfo["error"].asStringRef().c_str());
        updates.null();
        updates["error"] = updrInfo["error"];
        return;
      }
      if (!updrInfo.isArray()) {
        updates.null();
        updates["error"] = "Received invalid version list from server. Unknown update status.";
        return;
      }
      updates.null();
      updates["release"] = RELEASE;
      updates["version"] = updrInfo[0u][0u];
      updates["date"] = updrInfo[0u][1u];
      updates["url"] = updrInfo[0u][2u];
      updates["full_list"] = updrInfo;
      if (updrInfo[0u][0u].asStringRef() == PACKAGE_VERSION) {
        updates["uptodate"] = 1;
        LOG_MSG("UPDR", "You're running the latest version");
      } else {
        updates["uptodate"] = 0;
        LOG_MSG("UPDR", "An update is available");
      }
      if (updateAfterNextCheck) {
        rollingUpdate();
        updateAfterNextCheck = false;
      }
      return;
    }, []() {
      LOG_MSG("UPDR", "Error getting update info: %s", checkerDl.getStatusText().c_str());
      updates.null();
      updates["error"] = "Error getting update info: " + checkerDl.getStatusText();
      return;
    });
    return 3600000;
  }

  void rollingUpdate() {
    updatePerc = 0;
    // No update data yet? Check now and schedule an update after the next check.
    if (!updates.isObject()) {
      updateAfterNextCheck = true;
      updaterCheck();
      return;
    }
    if (updates.isMember("release") && updates["release"].asStringRef() == "Self-compiled") {
      LOG_MSG("UPDR", "Self-compiled build. Updater disabled.");
      return;
    }
    if (!updates.isMember("uptodate") || updates["uptodate"].asBool()) {
      LOG_MSG("UPDR", "Version is already up to date; nothing to install");
      return;
    }

    if (!updates.isMember("url") || !updates["url"].isString() || updates["url"].asStringRef().find(".zip") != std::string::npos) {
      LOG_MSG("UPDR", "Cannot auto-install update for this platform. Please install manually");
      updatePerc = 0;
      return;
    }

    // Abort if update already in progress
    if (updaterDl.isEventLooping()) { return; }

    HTTP::URL url = HTTP::URL("http://releases.mistserver.org/update.php").link(updates["url"].asStringRef());
#ifdef SSL
    if (!updaterDl.isProxied()) { url.protocol = "https"; }
#endif
    LOG_MSG("UPDR", "Downloading update from %s", url.getUrl().c_str());
    updaterDl.dataTimeout = 50; // only timeout if no data received for 50 seconds
    updaterDl.progressCallback = []() {
      updatePerc = updaterDl.getHTTP().getPercentage();
      return Util::Config::is_active;
    };
    updaterDl.getEventLooped(Controller::E, url, 6, []() {
      LOG_MSG("UPDR", "Downloaded %zuKiB of update data; Installing...", updaterDl.data().size() / 1024);
      std::string outDir = Util::getMyPath();
      std::deque<std::string> tarArgs = {"tar", "-xzC", outDir};
      int tarIn = -1;
      pid_t tarPid = Util::Procs::StartPiped(tarArgs, &tarIn, 0, 0);
      if (!tarPid) {
        LOG_MSG("UPDR", "Could not extract update (is 'tar' installed..?)");
        updatePerc = 0;
        updaterDl.data().clear();
        return;
      }
      size_t tarProgress = 0;
      while (tarProgress < updaterDl.data().size()) {
        int written =
          write(tarIn, updaterDl.data().data() + tarProgress, std::min((size_t)4096, updaterDl.data().size() - tarProgress));
        if (written < 0) {
          LOG_MSG("UPDR", "Could not (fully) extract update! Aborting.");
          updatePerc = 0;
          return;
        }
        tarProgress += written;
      }
      close(tarIn);
      uint64_t waitCount = 0;
      while (Util::Procs::childRunning(tarPid)) {
        Util::wait(250);
        Util::Procs::reap(); // Ensure we still reap children, regardless of blocking execution
        if (waitCount == 40) {
          tarProgress = 0;
          LOG_MSG("UPDR", "Sending stop signal to tar process (may result in partial update!)");
          Util::Procs::Stop(tarPid);
        }
        if (waitCount == 80) {
          LOG_MSG("UPDR", "Sending kill signal to tar process (may result in partial update!)");
          Util::Procs::Murder(tarPid);
          break;
        }
        ++waitCount;
      }
      updatePerc = 0;
      if (tarProgress == updaterDl.data().size()) {
        LOG_MSG("UPDR", "Install complete, initiating rolling restart.");
        Util::Config::is_restarting = true;
        raise(SIGINT); // trigger restart
      } else {
        LOG_MSG("UPDR", "Install did not fully complete. Not restarting.");
      }
      updaterDl.data().clear();
    }, []() {
      LOG_MSG("UPDR", "Download failed: aborting update");
      updatePerc = 0;
    });
  }

  void insertUpdateInfo(JSON::Value & ret) {
    ret = updates;
    if (updaterDl.isEventLooping()) { ret["installing"] = true; }
    if (updatePerc) { ret["progress"] = (uint16_t)updatePerc; }
  }

  void abortUpdate() {
    if (updaterDl.isEventLooping()) {
      LOG_MSG("UPDR", "Aborting update because of API request");
      updaterDl.clean();
      updatePerc = 0;
    }
  }

} // namespace Controller
