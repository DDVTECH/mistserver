/// \file buffer_stream.h
/// Contains definitions for buffer streams.

#pragma once
#include <string>
#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/socket.h>
#include "tinythread.h"
#include "buffer_user.h"

namespace Buffer {
  /// Keeps track of a single streams inputs and outputs, taking care of thread safety and all other related issues.
  class Stream : public DTSC::Stream{
    public:
      /// Get a reference to this Stream object.
      static Stream * get();
      /// Get the current statistics in JSON format.
      std::string & getStats();
      /// Set the IP address to accept push data from.
      void setWaitingIP(std::string ip);
      /// Check if this is the IP address to accept push data from.
      bool checkWaitingIP(std::string ip);
      /// Sets the current socket for push data.
      bool setInput(Socket::Connection S);
      /// Gets the current socket for push data.
      Socket::Connection & getIPInput();
      /// Stores intermediate statistics.
      void saveStats(std::string username, Stats & stats);
      /// Stores final statistics.
      void clearStats(std::string username, Stats & stats, std::string reason);
      /// Sets the buffer name.
      void setName(std::string n);
      /// Add a user to the userlist.
      void addUser(user * newUser);
      /// Delete a user from the userlist.
      void removeUser(user * oldUser);
      /// Blocks the thread until new data is available.
      void waitForData();
      /// Sends the metadata to a specific socket
      void sendMeta(Socket::Connection & s);
      /// Cleanup function
      ~Stream();
      /// TODO: WRITEME
      bool parsePacket(std::string & buffer);
      bool parsePacket(Socket::Buffer & buffer);
      DTSC::livePos getNext(DTSC::livePos & pos, std::set<int> & allowedTracks);
      void cutBefore(int whereToCut);
  private:
      void deletionCallback(DTSC::livePos deleting);
      volatile int readers; ///< Current count of active readers;
      volatile int writers; ///< Current count of waiting/active writers.
      tthread::mutex rw_mutex; ///< Mutex for read/write locking.
      tthread::condition_variable rw_change; ///< Triggered when reader/writer count changes.
      static Stream * ref;
      Stream();
      JSON::Value Storage; ///< Global storage of data.
      std::string waiting_ip; ///< IP address for media push.
      Socket::Connection ip_input; ///< Connection used for media push.
      tthread::recursive_mutex stats_mutex; ///< Mutex for stats/users modifications.
      std::set<user*> users; ///< All connected users.
      std::set<user*>::iterator usersIt; ///< Iterator for all connected users.
      std::string name; ///< Name for this buffer.
      tthread::condition_variable moreData; ///< Triggered when more data becomes available.
  };
}
;
