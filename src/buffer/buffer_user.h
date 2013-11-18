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
      unsigned int playUntil; ///< Time until where is being played or zero if undefined.
      Stats lastStats; ///< Holds last known stats for this connection.
      Stats tmpStats; ///< Holds temporary stats for this connection.
      std::string sID; ///< Holds the connection ID.
      unsigned int curr_up; ///< Holds the current estimated transfer speed up.
      unsigned int curr_down; ///< Holds the current estimated transfer speed down.
      Socket::Connection S; ///< Connection to user
      /// Creates a new user from a newly connected socket.
      user(Socket::Connection fd, long long int ID);
      /// Disconnects the current user. Doesn't do anything if already disconnected.
      void Disconnect(std::string reason);
  };
}
