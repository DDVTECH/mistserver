/// \file buffer_user.cpp
/// Contains code for buffer users.
#include "buffer_user.h"
#include "buffer_stream.h"

#include <sstream>
#include <stdlib.h>

namespace Buffer {
  int user::UserCount = 0;

  ///\brief Creates a new user from a newly connected socket.
  ///
  ///Also prints "User connected" text to stdout.
  ///\param fd A connection to the user.
  user::user(Socket::Connection fd){
    S = fd;
    MyNum = UserCount++;
    std::stringstream st;
    st << MyNum;
    MyStr = st.str();
    curr_up = 0;
    curr_down = 0;
    currsend = 0;
    myRing = 0;
    gotproperaudio = false;
    lastpointer = 0;
  } //constructor

  ///\brief Drops held DTSC::Ring class, if one is held.
  user::~user(){
    Stream::get()->dropRing(myRing);
  } //destructor

  ///\brief Disconnects the current user. Doesn't do anything if already disconnected.
  ///
  ///Prints "Disconnected user" to stdout if disconnect took place.
  ///\param reason The reason for disconnecting the user.
  void user::Disconnect(std::string reason){
    if (S.connected()){
      S.close();
    }
    Stream::get()->clearStats(MyStr, lastStats, reason);
  } //Disconnect

  ///\brief Tries to send data to the user.
  ///
  ///Has a side effect of dropping the connection if send will never complete.
  ///\param ptr A pointer to the data that is to be sent.
  ///\param len The amount of bytes to be sent from this pointer.
  ///\return True if len bytes are sent, false otherwise.
  bool user::doSend(const char * ptr, int len){
    if ( !len){
      return true;
    } //do not do empty sends
    int r = S.iwrite(ptr + currsend, len - currsend);
    if (r <= 0){
      if (errno == EWOULDBLOCK){
        return false;
      }
      Disconnect(S.getError());
      return false;
    }
    currsend += r;
    return (currsend == len);
  } //doSend

  ///\brief Try to send the current buffer.
  ///
  ///\return True if the send was succesful, false otherwise.
  bool user::Send(){
    if ( !myRing){
      return false;
    } //no ring!
    if ( !S.connected()){
      return false;
    } //cancel if not connected
    if (myRing->waiting){
      Stream::get()->waitForData();
      if ( !myRing->waiting){
        Stream::get()->getReadLock();
        if (Stream::get()->getStream()->getPacket(myRing->b).isMember("keyframe") && myRing->playCount > 0){
          myRing->playCount--;
          if ( !myRing->playCount){
            JSON::Value pausemark;
            pausemark["datatype"] = "pause_marker";
            pausemark["time"] = Stream::get()->getStream()->getPacket(myRing->b)["time"].asInt();
            pausemark.toPacked();
            S.SendNow(pausemark.toNetPacked());
          }
        }
        Stream::get()->dropReadLock();
      }
      return false;
    } //still waiting for next buffer?
    if (myRing->starved){
      //if corrupt data, warn and get new DTSC::Ring
      std::cout << "Warning: User " << MyNum << " was send corrupt video data and send to the next keyframe!" << std::endl;
      Stream::get()->dropRing(myRing);
      myRing = Stream::get()->getRing();
      return false;
    }
    //try to complete a send
    Stream::get()->getReadLock();
    if (doSend(Stream::get()->getStream()->outPacket(myRing->b).c_str(), Stream::get()->getStream()->outPacket(myRing->b).length())){
      //switch to next buffer
      currsend = 0;
      if (myRing->b <= 0){
        myRing->waiting = true;
        return false;
      } //no next buffer? go in waiting mode.
      myRing->b--;
      if (Stream::get()->getStream()->getPacket(myRing->b).isMember("keyframe") && myRing->playCount > 0){
        myRing->playCount--;
        if ( !myRing->playCount){
          JSON::Value pausemark;
          pausemark["datatype"] = "pause_marker";
          pausemark["time"] = Stream::get()->getStream()->getPacket(myRing->b)["time"].asInt();
          pausemark.toPacked();
          S.SendNow(pausemark.toNetPacked());
        }
      }
      Stream::get()->dropReadLock();
      return false;
    } //completed a send
    Stream::get()->dropReadLock();
    return true;
  } //send

  ///\brief Default stats constructor.
  ///
  ///Should not be used.
  Stats::Stats(){
    up = 0;
    down = 0;
    conntime = 0;
  }

  ///\brief Stats constructor reading a string.
  ///
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
