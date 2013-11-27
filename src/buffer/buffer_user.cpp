/// \file buffer_user.cpp
/// Contains code for buffer users.
#include "buffer_user.h"
#include "buffer_stream.h"

#include <sstream>
#include <stdlib.h>

namespace Buffer {
  ///Creates a new user from a newly connected socket.
  ///Also prints "User connected" text to stdout.
  ///\param fd A connection to the user.
  user::user(Socket::Connection fd, long long ID){
    sID = JSON::Value(ID).asString();
    S = fd;
    curr_up = 0;
    curr_down = 0;
    myRing = 0;
  } //constructor

  ///Disconnects the current user. Doesn't do anything if already disconnected.
  ///Prints "Disconnected user" to stdout if disconnect took place.
  ///\param reason The reason for disconnecting the user.
  void user::Disconnect(std::string reason){
    S.close();
    Stream::get()->clearStats(sID, lastStats, reason);
  } //Disconnect

  ///Default stats constructor.
  ///Should not be used.
  Stats::Stats(){
    up = 0;
    down = 0;
    conntime = 0;
  }

  ///Stats constructor reading a string.
  ///Reads a stats string and parses it to the internal representation.
  ///\param s The string of stats.
  Stats::Stats(std::string s){
    size_t f = s.find(' ');
    if (f != std::string::npos){
      host = s.substr(0, f);
      s.erase(0, f + 1);
    }
    f = s.find(' ');
    if (f != std::string::npos){
      connector = s.substr(0, f);
      s.erase(0, f + 1);
    }
    f = s.find(' ');
    if (f != std::string::npos){
      conntime = atoi(s.substr(0, f).c_str());
      s.erase(0, f + 1);
    }
    f = s.find(' ');
    if (f != std::string::npos){
      up = atoi(s.substr(0, f).c_str());
      s.erase(0, f + 1);
      down = atoi(s.c_str());
    }
  }
}
