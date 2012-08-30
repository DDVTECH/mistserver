/// \file stream.cpp
/// Utilities for handling streams.

#include "stream.h"
#include "procs.h"
#include "socket.h"

/// Filters the streamname, removing invalid characters and converting all
/// letters to lowercase. If a '?' character is found, everything following
/// that character is deleted. The original string is modified.
void Util::Stream::sanitizeName(std::string & streamname){
  //strip anything that isn't numbers, digits or underscores
  for (std::string::iterator i=streamname.end()-1; i>=streamname.begin(); --i){
    if (*i == '?'){streamname.erase(i, streamname.end()); break;}
    if (!isalpha(*i) && !isdigit(*i) && *i != '_'){
      streamname.erase(i);
    }else{
      *i=tolower(*i);
    }
  }
}

Socket::Connection Util::Stream::getLive(std::string streamname){
  sanitizeName(streamname);
  return Socket::Connection("/tmp/mist/stream_"+streamname);
}

/// Starts a process for the VoD stream.
Socket::Connection Util::Stream::getVod(std::string streamname){
  sanitizeName(streamname);
  std::string filename = "/tmp/mist/vod_" + streamname;
  /// \todo Is the name unique enough?
  std::string name = "MistPlayer " + filename;
  const char *argv[] = { "MistPlayer", filename.c_str(), NULL };
  int fdin = -1, fdout = -1;
  Util::Procs::StartPiped(name, (char **)argv, &fdin, &fdout, 0);
  // if StartPiped fails then fdin and fdout will be unmodified (-1)
  return Socket::Connection(fdin, fdout);
}

/// Probe for available streams. Currently first VoD, then Live.
Socket::Connection Util::Stream::getStream(std::string streamname){
  Socket::Connection vod = getVod(streamname);
  if (vod.connected()){
    return vod;
  }
  return getLive(streamname);
}
/// Create a stream on the system.
/// Filters the streamname, removing invalid characters and
/// converting all letters to lowercase.
/// If a '?' character is found, everything following that character is deleted.
/// If the /tmp/mist directory doesn't exist yet, this will create it.
Socket::Server Util::Stream::makeLive(std::string streamname){
  sanitizeName(streamname);
  std::string loc = "/tmp/mist/stream_"+streamname;
  //attempt to create the /tmp/mist directory if it doesn't exist already.
  //ignore errors - we catch all problems in the Socket::Server creation already
  mkdir("/tmp/mist", S_IRWXU | S_IRWXG | S_IRWXO);
  //create and return the Socket::Server
  return Socket::Server(loc);
}
