/// \file buffer_stream.cpp
/// Contains definitions for buffer streams.

#include "buffer_stream.h"
#include <mist/timing.h>
#include <stdlib.h>

namespace Buffer {
  /// Stores the singleton reference.
  Stream * Stream::ref = 0;

  /// Returns a reference to the singleton instance of this class.
  /// \return A reference to the class.
  Stream * Stream::get(){
    static tthread::mutex creator;
    if (ref == 0){
      //prevent creating two at the same time
      creator.lock();
      if (ref == 0){
        ref = new Stream();
        ref->metadata.live = true;
      }
      creator.unlock();
    }
    return ref;
  }

  /// Creates a new DTSC::Stream object, private function so only one instance can exist.
  Stream::Stream() : DTSC::Stream(5){}

  /// Do cleanup on delete.
  Stream::~Stream(){
    tthread::lock_guard<tthread::recursive_mutex> guard(stats_mutex);
    if (users.size() > 0){
      for (usersIt = users.begin(); usersIt != users.end(); usersIt++){
        if (( * *usersIt).S.connected()){
          ( * *usersIt).S.close();
        }
      }
    }
    moreData.notify_all();
  }

  /// Calculate and return the current statistics.
  /// \return The current statistics in JSON format.
  std::string & Stream::getStats(){
    static std::string ret;
    long long int now = Util::epoch();
    unsigned int tot_up = 0, tot_down = 0, tot_count = 0;
    tthread::lock_guard<tthread::recursive_mutex> guard(stats_mutex);
    if (users.size() > 0){
      for (usersIt = users.begin(); usersIt != users.end(); usersIt++){
        tot_down += ( * *usersIt).curr_down;
        tot_up += ( * *usersIt).curr_up;
        tot_count++;
      }
    }
    Storage["totals"]["down"] = tot_down;
    Storage["totals"]["up"] = tot_up;
    Storage["totals"]["count"] = tot_count;
    Storage["totals"]["now"] = now;
    Storage["buffer"] = name;
    
    Storage["meta"] = metadata.toJSON();
    if (Storage["meta"].isMember("tracks")){
      for (JSON::ObjIter oIt = Storage["meta"]["tracks"].ObjBegin(); oIt != Storage["meta"]["tracks"].ObjEnd(); ++oIt){
        oIt->second.removeMember("fragments");
        oIt->second.removeMember("keys");
        oIt->second.removeMember("parts");
        oIt->second.removeMember("idheader");
        oIt->second.removeMember("commentheader");
      }
    }
    
    ret = Storage.toString();
    Storage["log"].null();
    return ret;
  }

  /// Set the IP address to accept push data from.
  /// \param ip The new IP to accept push data from.
  void Stream::setWaitingIP(std::string ip){
    waiting_ip = ip;
  }

  ///\brief Check if this is the IP address to accept push data from.
  ///\param ip The IP address to check, followed by a space and the password to check.
  ///\return True if it is the correct address or password, false otherwise.
  bool Stream::checkWaitingIP(std::string push_request){
    std::string ip = push_request.substr(0, push_request.find(' '));
    std::string pass = push_request.substr(push_request.find(' ') + 1);
    if (waiting_ip.length() > 0 && waiting_ip[0] == '@'){
      if (pass == waiting_ip.substr(1)){
        return true;
      }else{
        std::cout << "Password '" << pass << "' incorrect" << std::endl;
        return false;
      }
    }else{
      if (ip == waiting_ip || ip == "::ffff:" + waiting_ip){
        return true;
      }else{
        std::cout << ip << " != (::ffff:)" << waiting_ip << std::endl;
        return false;
      }
    }
  }

  /// Stores intermediate statistics.
  /// \param username The name of the user.
  /// \param stats The final statistics to store.
  void Stream::saveStats(std::string username, Stats & stats){
    tthread::lock_guard<tthread::recursive_mutex> guard(stats_mutex);
    Storage["curr"][username]["connector"] = stats.connector;
    Storage["curr"][username]["up"] = stats.up;
    Storage["curr"][username]["down"] = stats.down;
    Storage["curr"][username]["conntime"] = stats.conntime;
    Storage["curr"][username]["host"] = stats.host;
    Storage["curr"][username]["start"] = Util::epoch() - stats.conntime;
  }

  /// Stores final statistics.
  /// \param username The name of the user.
  /// \param stats The final statistics to store.
  /// \param reason The reason for disconnecting.
  void Stream::clearStats(std::string username, Stats & stats, std::string reason){
    tthread::lock_guard<tthread::recursive_mutex> guard(stats_mutex);
    if (Storage["curr"].isMember(username)){
      Storage["curr"].removeMember(username);
  #if DEBUG >= 4
      std::cout << "Disconnected user " << username << ": " << reason << ". " << stats.connector << " transferred " << stats.up << " up and "
          << stats.down << " down in " << stats.conntime << " seconds to " << stats.host << std::endl;
  #endif
    }
    Storage["log"][username]["connector"] = stats.connector;
    Storage["log"][username]["up"] = stats.up;
    Storage["log"][username]["down"] = stats.down;
    Storage["log"][username]["conntime"] = stats.conntime;
    Storage["log"][username]["host"] = stats.host;
    Storage["log"][username]["start"] = Util::epoch() - stats.conntime;
  }

  /// The deletion callback override that will disconnect users
  /// whom are currently receiving a tag that is being deleted.
  void Stream::deletionCallback(DTSC::livePos deleting){
    tthread::lock_guard<tthread::recursive_mutex> guard(stats_mutex);
    for (usersIt = users.begin(); usersIt != users.end(); usersIt++){
      if ((*usersIt)->myRing->b == deleting){
        (*usersIt)->Disconnect("Buffer underrun");
      }
    }
  }

  /// Sets the buffer name.
  /// \param n The new name of the buffer.
  void Stream::setName(std::string n){
    name = n;
  }
  
  /// parsePacket override that will lock the rw_mutex during parsing.
  bool Stream::parsePacket(std::string & buffer){
    rw_mutex.lock();
    bool ret = DTSC::Stream::parsePacket(buffer);
    rw_mutex.unlock();
    if (ret){
      rw_change.notify_all();
      moreData.notify_all();
    }
    return ret;
  }
  
  /// getNext override that will lock the rw_mutex during checking.
  DTSC::livePos Stream::getNext(DTSC::livePos & pos, std::set<int> & allowedTracks){
    tthread::lock_guard<tthread::mutex> guard(rw_mutex);
    return DTSC::Stream::getNext(pos, allowedTracks);
  }
  
  /// parsePacket override that will lock the rw_mutex during parsing.
  bool Stream::parsePacket(Socket::Connection & c){
    bool ret = false;
    if (!c.spool()){
      return ret;
    }
    rw_mutex.lock();
    while (DTSC::Stream::parsePacket(c.Received())){
      ret = true;
    }
    rw_mutex.unlock();
    if (ret){
      rw_change.notify_all();
      moreData.notify_all();
    }
    return ret;
  }
  
  /// Metadata sender that locks the rw_mutex during sending.
  void Stream::sendMeta(Socket::Connection & s){
    if (metadata){
      rw_mutex.lock();
      metadata.send(s);
      rw_mutex.unlock();
    }
  }

  /// Add a user to the userlist.
  /// \param newUser The user to be added.
  void Stream::addUser(user * newUser){
    tthread::lock_guard<tthread::recursive_mutex> guard(stats_mutex);
    users.insert(newUser);
  }

  /// Removes a user from the userlist.
  /// \param newUser The user to be removed.
  void Stream::removeUser(user * oldUser){
    tthread::lock_guard<tthread::recursive_mutex> guard(stats_mutex);
    users.erase(oldUser);
  }

  /// Blocks the thread until new data is available.
  void Stream::waitForData(){
    tthread::lock_guard<tthread::recursive_mutex> guard(stats_mutex);
    moreData.wait(stats_mutex);
  }
  
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
