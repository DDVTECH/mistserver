/// \file stream.cpp
/// Utilities for handling streams.

#if DEBUG >= 4
#include <iostream>
#endif

#include <sys/stat.h>
#include <sys/types.h>
#include "json.h"
#include "stream.h"
#include "procs.h"
#include "config.h"
#include "socket.h"

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

Socket::Connection Util::Stream::getLive(std::string streamname){
  return Socket::Connection("/tmp/mist/stream_" + streamname);
}

/// Starts a process for a VoD stream.
Socket::Connection Util::Stream::getVod(std::string filename){
  std::string name = "MistPlayer " + filename;
  std::string player_bin = Util::getMyPath() + "MistPlayer";
  char* const argv[] = {(char*)player_bin.c_str(), (char*)filename.c_str(), NULL};
  int fdin = -1, fdout = -1, fderr = fileno(stderr);
  Util::Procs::StartPiped(name, argv, &fdin, &fdout, &fderr);
  // if StartPiped fails then fdin and fdout will be unmodified (-1)
  return Socket::Connection(fdin, fdout);
}

/// Probe for available streams. Currently first VoD, then Live.
Socket::Connection Util::Stream::getStream(std::string streamname){
  sanitizeName(streamname);
  JSON::Value ServConf = JSON::fromFile("/tmp/mist/streamlist");
  if (ServConf["streams"].isMember(streamname)){
    if (ServConf["streams"][streamname]["source"].asString()[0] == '/'){
#if DEBUG >= 5
      std::cerr << "Opening VoD stream from file " << ServConf["streams"][streamname]["source"].asString() << std::endl;
#endif
      return getVod(ServConf["streams"][streamname]["source"].asString());
    }else{
#if DEBUG >= 5
      std::cerr << "Opening live stream " << streamname << std::endl;
#endif
      return Socket::Connection("/tmp/mist/stream_" + streamname);
    }
  }
#if DEBUG >= 5
  std::cerr << "Could not open stream " << streamname << " - stream not found" << std::endl;
#endif
  return Socket::Connection();
}

/// Create a stream on the system.
/// Filters the streamname, removing invalid characters and
/// converting all letters to lowercase.
/// If a '?' character is found, everything following that character is deleted.
/// If the /tmp/mist directory doesn't exist yet, this will create it.
Socket::Server Util::Stream::makeLive(std::string streamname){
  sanitizeName(streamname);
  std::string loc = "/tmp/mist/stream_" + streamname;
  //attempt to create the /tmp/mist directory if it doesn't exist already.
  //ignore errors - we catch all problems in the Socket::Server creation already
  mkdir("/tmp/mist", S_IRWXU | S_IRWXG | S_IRWXO);
  //create and return the Socket::Server
  return Socket::Server(loc);
}
