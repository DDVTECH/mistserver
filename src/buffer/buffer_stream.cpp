/// \file buffer_stream.cpp
/// Contains definitions for buffer streams.

#include "buffer_stream.h"
#include <mist/timing.h>

namespace Buffer {
  ///\brief Stores the singleton reference.
  Stream * Stream::ref = 0;

  ///\brief Returns a reference to the singleton instance of this class.
  ///\return A reference to the class.
  Stream * Stream::get(){
    static tthread::mutex creator;
    if (ref == 0){
      //prevent creating two at the same time
      creator.lock();
      if (ref == 0){
        ref = new Stream();
      }
      creator.unlock();
    }
    return ref;
  }

  ///\brief Creates a new DTSC::Stream object, private function so only one instance can exist.
  Stream::Stream(){
    Strm = new DTSC::Stream(5);
    readers = 0;
    writers = 0;
  }

  ///\brief Do cleanup on delete.
  Stream::~Stream(){
    tthread::lock_guard<tthread::mutex> guard(stats_mutex);
    if (users.size() > 0){
      for (usersIt = users.begin(); usersIt != users.end(); usersIt++){
        if (( * *usersIt).S.connected()){
          ( * *usersIt).S.close();
        }
      }
    }
    moreData.notify_all();
    delete Strm;
  }

  ///\brief Calculate and return the current statistics.
  ///\return The current statistics in JSON format.
  std::string & Stream::getStats(){
    static std::string ret;
    long long int now = Util::epoch();
    unsigned int tot_up = 0, tot_down = 0, tot_count = 0;
    tthread::lock_guard<tthread::mutex> guard(stats_mutex);
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
    Storage["meta"] = Strm->metadata;
    if (Storage["meta"].isMember("audio")){
      Storage["meta"]["audio"].removeMember("init");
    }
    if (Storage["meta"].isMember("video")){
      Storage["meta"]["video"].removeMember("init");
    }
    ret = Storage.toString();
    Storage["log"].null();
    return ret;
  }

  ///\brief Get a new DTSC::Ring object for a user.
  ///\return A new DTSC::Ring object.
  DTSC::Ring * Stream::getRing(){
    return Strm->getRing();
  }

  ///\brief Drop a DTSC::Ring object.
  ///\param ring The DTSC::Ring to be invalidated.
  void Stream::dropRing(DTSC::Ring * ring){
    Strm->dropRing(ring);
  }

  ///\brief Get the (constant) header data of this stream.
  ///\return A reference to the header data of the stream.
  std::string & Stream::getHeader(){
    return Strm->outHeader();
  }

  ///\brief Set the IP address to accept push data from.
  ///\param ip The new IP to accept push data from.
  void Stream::setWaitingIP(std::string ip){
    waiting_ip = ip;
  }

  ///\brief Check if this is the IP address to accept push data from.
  ///\param ip The IP address to check.
  ///\return True if it is the correct address, false otherwise.
  bool Stream::checkWaitingIP(std::string ip){
    if (ip == waiting_ip || ip == "::ffff:" + waiting_ip){
      return true;
    }else{
      std::cout << ip << " != (::ffff:)" << waiting_ip << std::endl;
      return false;
    }
  }

  ///\brief Sets the current socket for push data.
  ///\param S The new socket for accepting push data.
  ///\return True if succesful, false otherwise.
  bool Stream::setInput(Socket::Connection S){
    if (ip_input.connected()){
      return false;
    }else{
      ip_input = S;
      return true;
    }
  }

  ///\brief Gets the current socket for push data.
  ///\return A reference to the push socket.
  Socket::Connection & Stream::getIPInput(){
    return ip_input;
  }

  ///\brief Stores intermediate statistics.
  ///\param username The name of the user.
  ///\param stats The final statistics to store.
  void Stream::saveStats(std::string username, Stats & stats){
    tthread::lock_guard<tthread::mutex> guard(stats_mutex);
    Storage["curr"][username]["connector"] = stats.connector;
    Storage["curr"][username]["up"] = stats.up;
    Storage["curr"][username]["down"] = stats.down;
    Storage["curr"][username]["conntime"] = stats.conntime;
    Storage["curr"][username]["host"] = stats.host;
    Storage["curr"][username]["start"] = Util::epoch() - stats.conntime;
  }

  ///\brief Stores final statistics.
  ///\param username The name of the user.
  ///\param stats The final statistics to store.
  ///\param reason The reason for disconnecting.
  void Stream::clearStats(std::string username, Stats & stats, std::string reason){
    tthread::lock_guard<tthread::mutex> guard(stats_mutex);
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

  ///\brief Ask to obtain a write lock.
  ///
  /// Blocks until writing is safe.
  void Stream::getWriteLock(){
    rw_mutex.lock();
    writers++;
    while (writers != 1 && readers != 0){
      rw_change.wait(rw_mutex);
    }
    rw_mutex.unlock();
  }

  ///\brief Drops a previously obtained write lock.
  ///\param newPacketsAvailable Whether new packets are available to update the index.
  void Stream::dropWriteLock(bool newPacketsAvailable){
    if (newPacketsAvailable){
      if (Strm->getPacket(0).isMember("keyframe")){
        stats_mutex.lock();
        Strm->updateHeaders();
        stats_mutex.unlock();
      }
    }
    rw_mutex.lock();
    writers--;
    rw_mutex.unlock();
    rw_change.notify_all();
    if (newPacketsAvailable){
      moreData.notify_all();
    }
  }

  ///\brief Ask to obtain a read lock.
  ///
  ///Blocks until reading is safe.
  void Stream::getReadLock(){
    rw_mutex.lock();
    while (writers > 0){
      rw_change.wait(rw_mutex);
    }
    readers++;
    rw_mutex.unlock();
  }

  ///\brief Drops a previously obtained read lock.
  void Stream::dropReadLock(){
    rw_mutex.lock();
    readers--;
    rw_mutex.unlock();
    rw_change.notify_all();
  }

  ///\brief Retrieves a reference to the DTSC::Stream
  ///\return A reference to the used DTSC::Stream
  DTSC::Stream * Stream::getStream(){
    return Strm;
  }

  ///\brief Sets the buffer name.
  ///\param n The new name of the buffer.
  void Stream::setName(std::string n){
    name = n;
  }

  ///\brief Add a user to the userlist.
  ///\param newUser The user to be added.
  void Stream::addUser(user * newUser){
    tthread::lock_guard<tthread::mutex> guard(stats_mutex);
    users.insert(newUser);
  }

  ///\brief Add a user to the userlist.
  ///\param newUser The user to be added.
  void Stream::removeUser(user * oldUser){
    tthread::lock_guard<tthread::mutex> guard(stats_mutex);
    users.erase(oldUser);
  }
  
  ///\brief Blocks the thread until new data is available.
  void Stream::waitForData(){
    tthread::lock_guard<tthread::mutex> guard(stats_mutex);
    moreData.wait(stats_mutex);
  }
}
