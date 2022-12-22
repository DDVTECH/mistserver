#include "input_playlist.h"
#include <algorithm>
#include <mist/procs.h>
#include <mist/stream.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

namespace Mist{
  inputPlaylist::inputPlaylist(Util::Config *cfg) : Input(cfg){
    capa["name"] = "Playlist";
    capa["desc"] = "Enables Playlist Input";
    capa["source_match"] = "*.pls";
    capa["always_match"] = "*.pls";
    capa["variables_match"] = "*.pls";
    capa["priority"] = 9;
    capa["hardcoded"]["resume"] = 1;

    playlistIndex = 0xFFFFFFFEull; // Not FFFFFFFF on purpose!
  }

  bool inputPlaylist::checkArguments(){
    if (config->getString("input") == "-"){
      Util::logExitReason(ER_FORMAT_SPECIFIC, "Input from stdin not yet supported");
      return false;
    }
    if (!config->getString("streamname").size()){
      if (config->getString("output") == "-"){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "Output to stdout not yet supported");
        return false;
      }
    }else{
      if (config->getString("output") != "-"){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "File output in player mode not supported");
        return false;
      }
    }
    return true;
  }

  void inputPlaylist::streamMainLoop(){
    bool seenValidEntry = true;
    Comms::Users killSwitch;
    killSwitch.reload(streamName, (size_t)INVALID_TRACK_ID, (uint8_t)(COMM_STATUS_ACTIVE | COMM_STATUS_DONOTTRACK));
    while (config->is_active){
      if (killSwitch && killSwitch.getStatus() & COMM_STATUS_REQDISCONNECT){
        Util::logExitReason(ER_CLEAN_LIVE_BUFFER_REQ, "buffer requested shutdown");
        config->is_active = false;
        break;
      }
      struct tm *wTime;
      time_t nowTime = time(0);
      wTime = localtime(&nowTime);
      wallTime = wTime->tm_hour * 60 + wTime->tm_min;
      reloadPlaylist();
      if (!playlist.size()){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "No entries in playlist");
        return;
      }
      ++playlistIndex;
      if (playlistIndex >= playlist.size()){
        if (!seenValidEntry){
          HIGH_MSG("Parsed entire playlist without seeing a valid entry, waiting for any entry to "
                   "become available");
          Util::sleep(1000);
        }
        playlistIndex = 0;
        seenValidEntry = false;
      }
      if (minIndex != std::string::npos && playlistIndex < minIndex){
        INFO_MSG("Clipping playlist index from %zu to %zu to stay within playback timing schedule",
                 playlistIndex, minIndex);
        playlistIndex = minIndex;
      }
      if (maxIndex != std::string::npos && playlistIndex > maxIndex){
        INFO_MSG("Clipping playlist index from %zu to %zu to stay within playback timing schedule",
                 playlistIndex, maxIndex);
        playlistIndex = maxIndex;
      }
      currentSource = playlist.at(playlistIndex);

      std::map<std::string, std::string> overrides;
      overrides["realtime"] = "1";
      overrides["alwaysStart"] = ""; // Just making this value "available" is enough
      std::string srcPath = config->getString("input");
      if ((currentSource.size() && currentSource[0] == '/') || srcPath.rfind('/') == std::string::npos){
        srcPath = currentSource;
      }else{
        srcPath = srcPath.substr(0, srcPath.rfind("/") + 1) + currentSource;
      }
      char *workingDir = getcwd(NULL, 0);
      if (srcPath[0] != '/'){srcPath = std::string(workingDir) + "/" + srcPath;}
      free(workingDir);
      Util::streamVariables(srcPath, streamName, "");

      struct stat statRes;
      if (stat(srcPath.c_str(), &statRes)){
        FAIL_MSG("%s does not exist on the system, skipping it.", srcPath.c_str());
        continue;
      }
      if ((statRes.st_mode & S_IFMT) != S_IFREG){
        FAIL_MSG("%s is not a valid file, skipping it.", srcPath.c_str());
        continue;
      }
      pid_t spawn_pid = 0;
      // manually override stream url to start the correct input
      if (!Util::startInput(streamName, srcPath, true, true, overrides, &spawn_pid)){
        FAIL_MSG("Could not start input for source %s", srcPath.c_str());
        continue;
      }
      seenValidEntry = true;
      while (Util::Procs::isRunning(spawn_pid) && config->is_active){
        if (killSwitch && killSwitch.getStatus() & COMM_STATUS_REQDISCONNECT){
          Util::logExitReason(ER_CLEAN_LIVE_BUFFER_REQ, "buffer requested shutdown");
          config->is_active = false;
          break;
        }
        Util::sleep(1000);
        if (reloadOn != 0xFFFF){
          time_t nowTime = time(0);
          wTime = localtime(&nowTime);
          wallTime = wTime->tm_hour * 60 + wTime->tm_min;
          if (wallTime >= reloadOn){reloadPlaylist();}
          if ((minIndex != std::string::npos && playlistIndex < minIndex) ||
              (maxIndex != std::string::npos && playlistIndex > maxIndex)){
            INFO_MSG("Killing current playback to stay within min/max playlist entry for current "
                     "time of day");
            Util::Procs::Stop(spawn_pid);
          }
        }
      }
      if (!config->is_active && Util::Procs::isRunning(spawn_pid)){Util::Procs::Stop(spawn_pid);}
    }
  }

  void inputPlaylist::reloadPlaylist(){
    minIndex = std::string::npos;
    maxIndex = std::string::npos;
    std::string playlistFile;
    char *origSource = getenv("MIST_ORIGINAL_SOURCE");
    if (origSource){
      playlistFile = origSource;
    }else{
      playlistFile = config->getString("input");
    }
    MEDIUM_MSG("Reloading playlist '%s'", playlistFile.c_str());
    Util::streamVariables(playlistFile, streamName, playlistFile);
    std::ifstream inFile(playlistFile.c_str());
    if (!inFile.good()){
      WARN_MSG("Unable to open playlist '%s', aborting reload!", playlistFile.c_str());
      return;
    }
    std::string line;
    uint16_t plsStartTime = 0xFFFF;
    reloadOn = 0xFFFF;
    playlist.clear();
    playlist_startTime.clear();
    while (inFile.good()){
      std::getline(inFile, line);
      if (inFile.good() && line.size() && line.at(0) != '#'){
        playlist.push_back(line);
        playlist_startTime.push_back(plsStartTime);
        if (plsStartTime != 0xFFFF){
          // If the newest entry has a time under the current time, we know we should never play earlier than this
          if (plsStartTime <= wallTime){minIndex = playlist.size() - 1;}
          // If the newest entry has a time above the current time, we know we should never play it
          if (plsStartTime > wallTime && maxIndex == std::string::npos){
            maxIndex = playlist.size() - 2;
            reloadOn = plsStartTime;
          }
          HIGH_MSG("Start %s on %d (min: %zu, max: %zu)", line.c_str(), plsStartTime, minIndex, maxIndex);
        }
        plsStartTime = 0xFFFF;
      }else{
        if (line.size() > 13 && line.at(0) == '#' && line.substr(0, 13) == "#X-STARTTIME:"){
          int hour, min;
          if (sscanf(line.c_str() + 13, "%d:%d", &hour, &min) == 2){
            plsStartTime = hour * 60 + min;
          }
        }
      }
    }
    inFile.close();
  }

}// namespace Mist
