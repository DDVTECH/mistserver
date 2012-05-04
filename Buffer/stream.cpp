#include "stream.h"

/// Stores the globally equal reference.
Buffer::Stream * Buffer::Stream::ref = 0;

/// Returns a globally equal reference to this class.
Buffer::Stream * Buffer::Stream::get(){
  static tthread::mutex creator;
  if (ref == 0){
    //prevent creating two at the same time
    creator.lock();
    if (ref == 0){ref = new Stream();}
    creator.unlock();
  }
  return ref;
}

/// Creates a new DTSC::Stream object, private function so only one instance can exist.
Buffer::Stream::Stream(){
  Strm = new DTSC::Stream(5);
}

/// Do cleanup on delete.
Buffer::Stream::~Stream(){
  delete Strm;
  while (users.size() > 0){
    stats_mutex.lock();
    for (usersIt = users.begin(); usersIt != users.end(); usersIt++){
      if ((**usersIt).S.connected()){
        if ((**usersIt).myRing->waiting){
          (**usersIt).S.close();
          printf("Closing user %s\n", (**usersIt).MyStr.c_str());
        }
      }
    }
    stats_mutex.unlock();
    moreData.notify_all();
    cleanUsers();
  }
}

/// Calculate and return the current statistics in JSON format.
std::string Buffer::Stream::getStats(){
  unsigned int now = time(0);
  unsigned int tot_up = 0, tot_down = 0, tot_count = 0;
  stats_mutex.lock();
  if (users.size() > 0){
    for (usersIt = users.begin(); usersIt != users.end(); usersIt++){
      tot_down += (**usersIt).curr_down;
      tot_up += (**usersIt).curr_up;
      tot_count++;
    }
  }
  Storage["totals"]["down"] = tot_down;
  Storage["totals"]["up"] = tot_up;
  Storage["totals"]["count"] = tot_count;
  Storage["totals"]["now"] = now;
  Storage["totals"]["buffer"] = name;
  std::string ret = Storage.toString();
  Storage["log"].null();
  stats_mutex.unlock();
  return ret;
}

/// Get a new DTSC::Ring object for a user.
DTSC::Ring * Buffer::Stream::getRing(){
  return Strm->getRing();
}

/// Drop a DTSC::Ring object.
void Buffer::Stream::dropRing(DTSC::Ring * ring){
  Strm->dropRing(ring);
}

/// Get the (constant) header data of this stream.
std::string & Buffer::Stream::getHeader(){
  return Strm->outHeader();
}

/// Set the IP address to accept push data from.
void Buffer::Stream::setWaitingIP(std::string ip){
  waiting_ip = ip;
}

/// Check if this is the IP address to accept push data from.
bool Buffer::Stream::checkWaitingIP(std::string ip){
  if (ip == waiting_ip || ip == "::ffff:"+waiting_ip){
    return true;
  }else{
    std::cout << ip << " != " << waiting_ip << std::endl;
    return false;
  }
}

/// Sets the current socket for push data.
bool Buffer::Stream::setInput(Socket::Connection S){
  if (ip_input.connected()){
    return false;
  }else{
    ip_input = S;
    return true;
  }
}

/// Gets the current socket for push data.
Socket::Connection & Buffer::Stream::getIPInput(){
  return ip_input;
}


/// Stores intermediate statistics.
void Buffer::Stream::saveStats(std::string username, Stats & stats){
  stats_mutex.lock();
  Storage["curr"][username]["connector"] = stats.connector;
  Storage["curr"][username]["up"] = stats.up;
  Storage["curr"][username]["down"] = stats.down;
  Storage["curr"][username]["conntime"] = stats.conntime;
  Storage["curr"][username]["host"] = stats.host;
  Storage["curr"][username]["start"] = (unsigned int) time(0) - stats.conntime;
  stats_mutex.unlock();
}

/// Stores final statistics.
void Buffer::Stream::clearStats(std::string username, Stats & stats, std::string reason){
  stats_mutex.lock();
  Storage["curr"].removeMember(username);
  Storage["log"][username]["connector"] = stats.connector;
  Storage["log"][username]["up"] = stats.up;
  Storage["log"][username]["down"] = stats.down;
  Storage["log"][username]["conntime"] = stats.conntime;
  Storage["log"][username]["host"] = stats.host;
  Storage["log"][username]["start"] = (unsigned int)time(0) - stats.conntime;
  std::cout << "Disconnected user " << username << ": " << reason << ". " << stats.connector << " transferred " << stats.up << " up and " << stats.down << " down in " << stats.conntime << " seconds to " << stats.host << std::endl;
  stats_mutex.unlock();
  cleanUsers();
}

/// Cleans up broken connections
void Buffer::Stream::cleanUsers(){
  bool repeat = false;
  stats_mutex.lock();
  do{
    repeat = false;
    if (users.size() > 0){
      for (usersIt = users.begin(); usersIt != users.end(); usersIt++){
        if ((**usersIt).Thread == 0 && !(**usersIt).S.connected()){
          delete *usersIt;
          users.erase(usersIt);
          repeat = true;
          break;
        }
      }
    }
  }while(repeat);
  stats_mutex.unlock();
}

/// Blocks until writing is safe.
void Buffer::Stream::getWriteLock(){
  rw_mutex.lock();
  writers++;
  while (writers != 1 && readers != 0){
    rw_change.wait(rw_mutex);
  }
  rw_mutex.unlock();
}

/// Drops a previously gotten write lock.
void Buffer::Stream::dropWriteLock(bool newpackets_available){
  rw_mutex.lock();
  writers--;
  rw_mutex.unlock();
  rw_change.notify_all();
  if (newpackets_available){moreData.notify_all();}
}

/// Blocks until reading is safe.
void Buffer::Stream::getReadLock(){
  rw_mutex.lock();
  while (writers > 0){
    rw_change.wait(rw_mutex);
  }
  readers++;
  rw_mutex.unlock();
}

/// Drops a previously gotten read lock.
void Buffer::Stream::dropReadLock(){
  rw_mutex.lock();
  readers--;
  rw_mutex.unlock();
  rw_change.notify_all();
}

/// Retrieves a reference to the DTSC::Stream
DTSC::Stream * Buffer::Stream::getStream(){
  return Strm;
}

/// Sets the buffer name.
void Buffer::Stream::setName(std::string n){
  name = n;
}

/// Add a user to the userlist.
void Buffer::Stream::addUser(user * new_user){
  stats_mutex.lock();
  users.push_back(new_user);
  stats_mutex.unlock();
}

/// Blocks the thread until new data is available.
void Buffer::Stream::waitForData(){
  stats_mutex.lock();
  moreData.wait(stats_mutex);
  stats_mutex.unlock();
}
