/// \file buffer_stream.h
/// Contains definitions for buffer streams.

#pragma once
#include <string>
#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/socket.h>
#include <mist/tinythread.h>

namespace Buffer {

  /// Converts a stats line to up, down, host, connector and conntime values.
  class Stats{
  public:
    unsigned int up;///<The amount of bytes sent upstream.
    unsigned int down;///<The amount of bytes received downstream.
    std::string host;///<The connected host.
    std::string connector;///<The connector the user is connected with.
    unsigned int conntime;///<The amount of time the user is connected.
    Stats(std::string s);
    Stats();
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
      /// Removes a track and all related buffers from the stream.
      void removeTrack(int trackId);
      /// Calls removeTrack on all tracks that were streaming from this socket number.
      void removeSocket(int sockNo);
      /// Thread-safe parsePacket override.
      bool parsePacket(std::string & buffer);
      /// Thread-safe parsePacket override.
      bool parsePacket(Socket::Connection & c);
      /// Logs a message to the controller.
      void Log(std::string type, std::string message);
      DTSC::livePos getNext(DTSC::livePos & pos, std::set<int> & allowedTracks);
  private:
      void deletionCallback(DTSC::livePos deleting);
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
