/// \file buffer_user.cpp
/// Contains code for buffer users.

#include "buffer_user.h"
#include "buffer_stream.h"
#include <sstream>
#include <stdlib.h> //for atoi and friends

int Buffer::user::UserCount = 0;

/// Creates a new user from a newly connected socket.
/// Also prints "User connected" text to stdout.
Buffer::user::user(Socket::Connection fd){
  S = fd;
  MyNum = UserCount++;
  std::stringstream st;
  st << MyNum;
  MyStr = st.str();
  curr_up = 0;
  curr_down = 0;
  currsend = 0;
  myRing = 0;
  Thread = 0;
}//constructor

/// Drops held DTSC::Ring class, if one is held.
Buffer::user::~user(){
  Stream::get()->dropRing(myRing);
}//destructor

/// Disconnects the current user. Doesn't do anything if already disconnected.
/// Prints "Disconnected user" to stdout if disconnect took place.
void Buffer::user::Disconnect(std::string reason) {
  if (S.connected()){S.close();}
  Stream::get()->clearStats(MyStr, lastStats, reason);
}//Disconnect

/// Tries to send the current buffer, returns true if success, false otherwise.
/// Has a side effect of dropping the connection if send will never complete.
bool Buffer::user::doSend(const char * ptr, int len){
  if (!len){return false;}//do not do empty sends
  int r = S.iwrite(ptr+currsend, len-currsend);
  if (r <= 0){
    if (errno == EWOULDBLOCK){return false;}
    Disconnect(S.getError());
    return false;
  }
  currsend += r;
  return (currsend == len);
}//doSend

/// Try to send data to this user. Disconnects if any problems occur.
void Buffer::user::Send(){
  if (!myRing){return;}//no ring!
  if (!S.connected()){return;}//cancel if not connected
  if (myRing->waiting){
    Stream::get()->waitForData();
    return;
  }//still waiting for next buffer?
  if (myRing->starved){
    //if corrupt data, warn and get new DTSC::Ring
    std::cout << "Warning: User " << MyNum << " was send corrupt video data and send to the next keyframe!" << std::endl;
    Stream::get()->dropRing(myRing);
    myRing = Stream::get()->getRing();
    return;
  }
  //try to complete a send
  Stream::get()->getReadLock();
  if (doSend(Stream::get()->getStream()->outPacket(myRing->b).c_str(), Stream::get()->getStream()->outPacket(myRing->b).length())){
    //switch to next buffer
    currsend = 0;
    if (myRing->b <= 0){myRing->waiting = true; return;}//no next buffer? go in waiting mode.
    myRing->b--;
  }//completed a send
  Stream::get()->dropReadLock();
}//send

/// Default constructor - should not be in use.
Buffer::Stats::Stats(){
  up = 0;
  down = 0;
  conntime = 0;
}

/// Reads a stats string and parses it to the internal representation.
Buffer::Stats::Stats(std::string s){
  size_t f = s.find(' ');
  if (f != std::string::npos){
    host = s.substr(0, f);
    s.erase(0, f+1);
  }
  f = s.find(' ');
  if (f != std::string::npos){
    connector = s.substr(0, f);
    s.erase(0, f+1);
  }
  f = s.find(' ');
  if (f != std::string::npos){
    conntime = atoi(s.substr(0, f).c_str());
    s.erase(0, f+1);
  }
  f = s.find(' ');
  if (f != std::string::npos){
    up = atoi(s.substr(0, f).c_str());
    s.erase(0, f+1);
    down = atoi(s.c_str());
  }
}
