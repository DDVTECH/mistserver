/// \file buffer_user.h
/// Contains definitions for buffer users.

#pragma once
#include <string>
#include <mist/dtsc.h>
#include <mist/socket.h>
#include "tinythread.h"

namespace Buffer {
  /// Converts a stats line to up, down, host, connector and conntime values.
  class Stats{
    public:
      unsigned int up;///<The amount of bytes sent upstream.
      unsigned int down;///<The amount of bytes received downstream.
      std::string host;///<The connected host.
      std::string connector;///<The connector the user is connected with.
      unsigned int conntime;///<The amount of time the user is connected.
      Stats();
      Stats(std::string s);
  };

  ///\brief Keeps track of connected users.
  ///
  ///Keeps track of which buffer the user currently uses,
  ///and its connection status.
  class user{
    public:
      DTSC::Ring * myRing; ///< Ring of the buffer for this user.
      int MyNum; ///< User ID of this user.
      std::string MyStr; ///< User ID of this user as a string.
      std::string inbuffer; ///< Used to buffer input data.
      int currsend; ///< Current amount of bytes sent.
      Stats lastStats; ///< Holds last known stats for this connection.
      Stats tmpStats; ///< Holds temporary stats for this connection.
      unsigned int curr_up; ///< Holds the current estimated transfer speed up.
      unsigned int curr_down; ///< Holds the current estimated transfer speed down.
      bool gotproperaudio; ///< Whether the user received proper audio yet.
      void * lastpointer; ///< Pointer to data part of current buffer.
      static int UserCount; ///< Global user counter.
      Socket::Connection S; ///< Connection to user
      /// Creates a new user from a newly connected socket.
      /// Also prints "User connected" text to stdout.
      user(Socket::Connection fd);
      /// Drops held DTSC::Ring class, if one is held.
      ~user();
      /// Disconnects the current user. Doesn't do anything if already disconnected.
      /// Prints "Disconnected user" to stdout if disconnect took place.
      void Disconnect(std::string reason);
      /// Tries to send the current buffer, returns true if success, false otherwise.
      /// Has a side effect of dropping the connection if send will never complete.
      bool doSend(const char * ptr, int len);
      /// Try to send data to this user. Disconnects if any problems occur.
      bool Send();
  };
}
