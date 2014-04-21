/// \file stream.cpp
/// Utilities for handling streams.

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <semaphore.h>
#include "json.h"
#include "stream.h"
#include "procs.h"
#include "config.h"
#include "socket.h"
#include "defines.h"

std::string Util::getTmpFolder(){
  std::string dir;
  char * tmp_char = 0;
  if ( !tmp_char){
    tmp_char = getenv("TMP");
  }
  if ( !tmp_char){
    tmp_char = getenv("TEMP");
  }
  if ( !tmp_char){
    tmp_char = getenv("TMPDIR");
  }
  if (tmp_char){
    dir = tmp_char;
    dir += "/mist";
  }else{
#if defined(_WIN32) || defined(_CYGWIN_)
    dir = "C:/tmp/mist";
#else
    dir = "/tmp/mist";
#endif
  }
  if (access(dir.c_str(), 0) != 0){
    mkdir(dir.c_str(), S_IRWXU | S_IRWXG | S_IRWXO); //attempt to create mist folder - ignore failures
  }
  return dir + "/";
}


/// Filters the streamname, removing invalid characters and converting all
/// letters to lowercase. If a '?' character is found, everything following
/// that character is deleted. The original string is modified.
void Util::Stream::sanitizeName(std::string & streamname){
  //strip anything that isn't numbers, digits or underscores
  for (std::string::iterator i = streamname.end() - 1; i >= streamname.begin(); --i){
    if ( *i == '?'){
      streamname.erase(i, streamname.end());
      break;
    }
    if ( !isalpha( *i) && !isdigit( *i) && *i != '_'){
      streamname.erase(i);
    }else{
      *i = tolower( *i);
    }
  }
}

bool Util::Stream::getLive(std::string streamname){
  JSON::Value ServConf = JSON::fromFile(getTmpFolder() + "streamlist");
  static unsigned long long counter = 0;
  std::stringstream name;
  name << "MistInBuffer " << (counter++);
  std::string player_bin = Util::getMyPath() + "MistInBuffer";
  DEBUG_MSG(DLVL_WARN, "Starting %s -p -s %s", player_bin.c_str(), streamname.c_str());
  char* argv[15] = {(char*)player_bin.c_str(), (char*)"-p", (char*)"-s", (char*)streamname.c_str(), (char*)0};
  int argNum = 4;
  if (ServConf["streams"][streamname].isMember("DVR")){
    std::string bufferTime = ServConf["streams"][streamname]["DVR"].asString();
    argv[argNum++] = (char*)"-b";
    argv[argNum++] = (char*)bufferTime.c_str();
    argv[argNum++] = (char*)0;
  }

  int pid = fork();
  if (pid){
    execvp(argv[0], argv);
    _exit(42);
  }else if(pid == -1){
    perror("Could not start vod");
  }
  return true;
}

/// Starts a process for a VoD stream.
bool Util::Stream::getVod(std::string filename, std::string streamname){
  static unsigned long long counter = 0;
  std::stringstream name;
  name << "MistInDTSC " << (counter++);
  std::string player_bin = Util::getMyPath() + "MistInDTSC";
  if (filename.substr(filename.size()-5) == ".ismv"){
    name.str("MistInISMV " + filename);
    player_bin = Util::getMyPath() + "MistInISMV";
  }
  if (filename.substr(filename.size()-4) == ".flv"){
    name.str("MistInFLV " + filename);
    player_bin = Util::getMyPath() + "MistInFLV";
  }
  DEBUG_MSG(DLVL_WARN, "Starting %s -p -s %s %s", player_bin.c_str(), streamname.c_str(), filename.c_str());
  char* const argv[] = {(char*)player_bin.c_str(), (char*)"-p", (char*)"-s", (char*)streamname.c_str(), (char*)filename.c_str(), (char*)0};

  int pid = fork();
  if (pid){
    execvp(argv[0], argv);
    _exit(42);
  }else if(pid == -1){
    perror("Could not start vod");
  }
  return true;
}

/// Probe for available streams. Currently first VoD, then Live.
bool Util::Stream::getStream(std::string streamname){
  sanitizeName(streamname);
  JSON::Value ServConf = JSON::fromFile(getTmpFolder() + "streamlist");
  /*LTS-START*/
  if (ServConf["config"].isMember("hardlimit_active")){
    return false;
  }
  /*LTS-END*/
  if (ServConf["streams"].isMember(streamname)){
    /*LTS-START*/
    if (ServConf["streams"][streamname].isMember("hardlimit_active")){
      return false;
    }
    /*LTS-END*/
    //check if the stream is already active, if yes, don't re-activate
    sem_t * playerLock = sem_open(std::string("/lock_" + streamname).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
    if (sem_trywait(playerLock) == -1){
      sem_close(playerLock);
      DEBUG_MSG(DLVL_MEDIUM, "Playerlock for %s already active - not re-activating stream", streamname.c_str());
      return true;
    }
    sem_post(playerLock);
    sem_close(playerLock);
    if (ServConf["streams"][streamname]["source"].asString()[0] == '/'){
      DEBUG_MSG(DLVL_MEDIUM, "Activating VoD stream %s", streamname.c_str());
      return getVod(ServConf["streams"][streamname]["source"].asString(), streamname);
    }else{
      DEBUG_MSG(DLVL_MEDIUM, "Activating live stream %s", streamname.c_str());
      return getLive(streamname);
    }
  }
  DEBUG_MSG(DLVL_ERROR, "Stream not found: %s", streamname.c_str());
  return false;
}

/// Create a stream on the system.
/// Filters the streamname, removing invalid characters and
/// converting all letters to lowercase.
/// If a '?' character is found, everything following that character is deleted.
Socket::Server Util::Stream::makeLive(std::string streamname){
  sanitizeName(streamname);
  std::string loc = getTmpFolder() + "stream_" + streamname;
  //create and return the Socket::Server
  return Socket::Server(loc);
}
