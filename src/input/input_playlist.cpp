#include "input_playlist.h"
#include <algorithm>
#include <mist/stream.h>
#include <mist/procs.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

namespace Mist {
  inputPlaylist::inputPlaylist(Util::Config * cfg) : Input(cfg) {
    capa["name"] = "Playlist";
    capa["desc"] = "Enables Playlist Input";
    capa["source_match"] = "*.pls";
    capa["priority"] = 9;

    capa["hardcoded"]["resume"] = 1;
    capa["hardcoded"]["always_on"] = 1;


    playlistIndex = 0xFFFFFFFEull;//Not FFFFFFFF on purpose!
    seenValidEntry = true;
  }

  bool inputPlaylist::checkArguments(){
    if (config->getString("input") == "-") {
      std::cerr << "Input from stdin not supported" << std::endl;
      return false;
    }
    if (!config->getString("streamname").size()){
      if (config->getString("output") == "-") {
        std::cerr << "Output to stdout not supported" << std::endl;
        return false;
      }
    }else{
      if (config->getString("output") != "-") {
        std::cerr << "File output not supported" << std::endl;
        return false;
      }
    }
    return true;
  }

  void inputPlaylist::stream(){
    IPC::semaphore playlistLock;
    playlistLock.open(std::string("/MstPlaylist_" + streamName).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
    if (!playlistLock){
      FAIL_MSG("Could not open pull lock for stream '%s' - aborting!", streamName.c_str());
      return;
    }
    if (!playlistLock.tryWait()){
      WARN_MSG("A pull process for stream %s is already running", streamName.c_str());
      playlistLock.close();
      return;
    }

    std::map<std::string, std::string> overrides;
    overrides["resume"] = "1";
    if (!Util::startInput(streamName, "push://INTERNAL_ONLY:"+config->getString("input"), true, true, overrides)) {//manually override stream url to start the buffer
      playlistLock.post();
      playlistLock.close();
      playlistLock.unlink();
      WARN_MSG("Could not start buffer, cancelling");
      return;
    }

    char userPageName[NAME_BUFFER_SIZE];
    snprintf(userPageName, NAME_BUFFER_SIZE, SHM_USERS, streamName.c_str());
    nProxy.userClient = IPC::sharedClient(userPageName, PLAY_EX_SIZE, true);
    nProxy.userClient.countAsViewer = false;

    uint64_t startTime = Util::bootMS();
  
    while (config->is_active && nProxy.userClient.isAlive()){
      nProxy.userClient.keepAlive();
      reloadPlaylist();
      if (!playlist.size()){
        playlistLock.post();
        playlistLock.close();
        playlistLock.unlink();
        WARN_MSG("No entries in playlist, exiting");
        break;
      }
      ++playlistIndex;
      if (playlistIndex >= playlist.size()){
        if (!seenValidEntry){
          HIGH_MSG("Parsed entire playlist without seeing a valid entry, wait a second for any entry to become available");
          Util::sleep(1000);
        }
        playlistIndex = 0;
        seenValidEntry = false;
      }
      currentSource = playlist.at(playlistIndex);

      std::map<std::string, std::string> overrides;
      overrides["realtime"] = "1";
      overrides["alwaysStart"] = "";//Just making this value "available" is enough
      overrides["simulated-starttime"] = JSON::Value(startTime).asString();
      std::string srcPath = config->getString("input");
      if ((currentSource.size() && currentSource[0] == '/') || srcPath.rfind('/') == std::string::npos){
        srcPath = currentSource;
      } else {
        srcPath = srcPath.substr(0, srcPath.rfind("/") + 1) + currentSource;
      }
      char * workingDir = getcwd(NULL, 0);
      if (srcPath[0] != '/'){
        srcPath = std::string(workingDir) + "/" + srcPath;
      }
      free(workingDir);

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
      if (!Util::startInput(streamName, srcPath, true, true, overrides, &spawn_pid)) {//manually override stream url to start the correct input
        FAIL_MSG("Could not start input for source %s", srcPath.c_str());
        continue;
      }
      seenValidEntry = true;
      while (Util::Procs::isRunning(spawn_pid) && nProxy.userClient.isAlive() && config->is_active){
        Util::sleep(1000);
        nProxy.userClient.keepAlive();
      }
      if (!config->is_active && Util::Procs::isRunning(spawn_pid)){
        Util::Procs::Stop(spawn_pid);
      }
    }
    playlistLock.post();
    playlistLock.close();
    playlistLock.unlink();

    nProxy.userClient.finish();
  }
  
  void inputPlaylist::reloadPlaylist(){
    std::string playlistFile = config->getString("input");
    std::ifstream inFile(playlistFile.c_str());
    if (!inFile.good()){
      WARN_MSG("Unable to open playlist, aborting reload!");
      return;
    }
    std::string line;
    playlist.clear();
    while (inFile.good()){
      std::getline(inFile, line);
      if (inFile.good() && line.size() && line.at(0) != '#'){
        playlist.push_back(line);
      }
    }
    inFile.close();
  }

}

