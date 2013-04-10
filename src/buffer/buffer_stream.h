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
  class Stream{
    public:
      /// Get a reference to this Stream object.
      static Stream * get();
      /// Get the current statistics in JSON format.
      std::string & getStats();
      /// Get a new DTSC::Ring object for a user.
      DTSC::Ring * getRing();
      /// Drop a DTSC::Ring object.
      void dropRing(DTSC::Ring * ring);
      /// Get the (constant) header data of this stream.
      std::string & getHeader();
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
      /// Blocks until writing is safe.
      void getWriteLock();
      /// Drops a previously gotten write lock.
      void dropWriteLock(bool newpackets_available);
      /// Blocks until reading is safe.
      void getReadLock();
      /// Drops a previously gotten read lock.
      void dropReadLock();
      /// Retrieves a reference to the DTSC::Stream
      DTSC::Stream * getStream();
      /// Sets the buffer name.
      void setName(std::string n);
      /// Add a user to the userlist.
      void addUser(user * newUser);
      /// Delete a user from the userlist.
      void removeUser(user * oldUser);
      /// Blocks the thread until new data is available.
      void waitForData();
      /// Cleanup function
      ~Stream();
    private:
      volatile int readers; ///< Current count of active readers;
      volatile int writers; ///< Current count of waiting/active writers.
      tthread::mutex rw_mutex; ///< Mutex for read/write locking.
      tthread::condition_variable rw_change; ///< Triggered when reader/writer count changes.
      static Stream * ref;
      Stream();
      JSON::Value Storage; ///< Global storage of data.
      DTSC::Stream * Strm;
      std::string waiting_ip; ///< IP address for media push.
      Socket::Connection ip_input; ///< Connection used for media push.
      tthread::mutex stats_mutex; ///< Mutex for stats/users modifications.
      std::set<user*> users; ///< All connected users.
      std::set<user*>::iterator usersIt; ///< Iterator for all connected users.
      std::string name; ///< Name for this buffer.
      tthread::condition_variable moreData; ///< Triggered when more data becomes available.
  };
}
;
