/// \file Buffer/main.cpp
/// Contains the main code for the Buffer.

#include <fcntl.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sstream>
#include <sys/time.h>
#include "../util/dtsc.h" //DTSC support
#include "../util/socket.h" //Socket lib
#include "../util/json.h"
#include "../util/tinythread.h"

/// Holds all code unique to the Buffer.
namespace Buffer{

  /// Gets the current system time in milliseconds.
  unsigned int getNowMS(){
    timeval t;
    gettimeofday(&t, 0);
    return t.tv_sec + t.tv_usec/1000;
  }//getNowMS


  JSON::Value Storage; ///< Global storage of data.
  
  ///A simple signal handler that ignores all signals.
  void termination_handler (int signum){
    switch (signum){
      case SIGPIPE: return; break;
      default: return; break;
    }
  }

  DTSC::Stream * Strm = 0;
  std::string waiting_ip = ""; ///< IP address for media push.
  Socket::Connection ip_input; ///< Connection used for media push.
  
  /// Converts a stats line to up, down, host, connector and conntime values.
  class Stats{
    public:
      unsigned int up;
      unsigned int down;
      std::string host;
      std::string connector;
      unsigned int conntime;
      Stats(){
        up = 0;
        down = 0;
        conntime = 0;
      }
      Stats(std::string s){
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
  };

  /// Holds connected users.
  /// Keeps track of what buffer users are using and the connection status.
  class user{
    public:
      tthread::thread * Thread; ///< Holds the thread dealing with this user.
      DTSC::Ring * myRing; ///< Ring of the buffer for this user.
      int MyNum; ///< User ID of this user.
      std::string MyStr; ///< User ID of this user as a string.
      int currsend; ///< Current amount of bytes sent.
      Stats lastStats; ///< Holds last known stats for this connection.
      unsigned int curr_up; ///< Holds the current estimated transfer speed up.
      unsigned int curr_down; ///< Holds the current estimated transfer speed down.
      bool gotproperaudio; ///< Whether the user received proper audio yet.
      void * lastpointer; ///< Pointer to data part of current buffer.
      static int UserCount; ///< Global user counter.
      Socket::Connection S; ///< Connection to user
      /// Creates a new user from a newly connected socket.
      /// Also prints "User connected" text to stdout.
      user(Socket::Connection fd){
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
        std::cout << "User " << MyNum << " connected" << std::endl;
      }//constructor
      /// Drops held DTSC::Ring class, if one is held.
      ~user(){
        Strm->dropRing(myRing);
      }//destructor
      /// Disconnects the current user. Doesn't do anything if already disconnected.
      /// Prints "Disconnected user" to stdout if disconnect took place.
      void Disconnect(std::string reason) {
        if (S.connected()){S.close();}
        if (Thread != 0){
          if (Thread->joinable()){Thread->join();}
          Thread = 0;
        }
        Storage["curr"].removeMember(MyStr);
        Storage["log"][MyStr]["connector"] = lastStats.connector;
        Storage["log"][MyStr]["up"] = lastStats.up;
        Storage["log"][MyStr]["down"] = lastStats.down;
        Storage["log"][MyStr]["conntime"] = lastStats.conntime;
        Storage["log"][MyStr]["host"] = lastStats.host;
        Storage["log"][MyStr]["start"] = (unsigned int)time(0) - lastStats.conntime;
        std::cout << "Disconnected user " << MyStr << ": " << reason << ". " << lastStats.connector << " transferred " << lastStats.up << " up and " << lastStats.down << " down in " << lastStats.conntime << " seconds to " << lastStats.host << std::endl;
      }//Disconnect
      /// Tries to send the current buffer, returns true if success, false otherwise.
      /// Has a side effect of dropping the connection if send will never complete.
      bool doSend(const char * ptr, int len){
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
      void Send(){
        if (!myRing){return;}//no ring!
        if (!S.connected()){return;}//cancel if not connected
        if (myRing->waiting){return;}//still waiting for next buffer?

        if (myRing->starved){
          //if corrupt data, warn and get new DTSC::Ring
          std::cout << "Warning: User was send corrupt video data and send to the next keyframe!" << std::endl;
          Strm->dropRing(myRing);
          myRing = Strm->getRing();
        }
        currsend = 0;

        //try to complete a send
        if (doSend(Strm->outPacket(myRing->b).c_str(), Strm->outPacket(myRing->b).length())){
          //switch to next buffer
          if (myRing->b <= 0){myRing->waiting = true; return;}//no next buffer? go in waiting mode.
          myRing->b--;
          currsend = 0;
        }//completed a send
      }//send
  };
  int user::UserCount = 0;

  void handleUser(void * v_usr){
    user * usr = (user*)v_usr;
    std::cerr << "Thread launched for user " << usr->MyStr << ", socket number " << usr->S.getSocket() << std::endl;

    usr->myRing = Strm->getRing();
    if (!usr->S.write(Strm->outHeader())){
      usr->Disconnect("failed to receive the header!");
      return;
    }

    while (usr->S.connected()){
      if (usr->S.canRead()){
        std::string tmp = "";
        char charbuf;
        while ((usr->S.iread(&charbuf, 1) == 1) && charbuf != '\n' ){
          tmp += charbuf;
        }
        if (tmp != ""){
          if (tmp[0] == 'P'){
            std::cout << "Push attempt from IP " << tmp.substr(2) << std::endl;
            if (tmp.substr(2) == waiting_ip){
              if (!ip_input.connected()){
                std::cout << "Push accepted!" << std::endl;
                ip_input = usr->S;
                usr->S = Socket::Connection(-1);
                return;
              }else{
                usr->Disconnect("Push denied - push already in progress!");
              }
            }else{
              usr->Disconnect("Push denied - invalid IP address!");
            }
          }
          if (tmp[0] == 'S'){
            Stats tmpStats = Stats(tmp.substr(2));
            unsigned int secs = tmpStats.conntime - usr->lastStats.conntime;
            if (secs < 1){secs = 1;}
            usr->curr_up = (tmpStats.up - usr->lastStats.up) / secs;
            usr->curr_down = (tmpStats.down - usr->lastStats.down) / secs;
            usr->lastStats = tmpStats;
            Storage["curr"][usr->MyStr]["connector"] = tmpStats.connector;
            Storage["curr"][usr->MyStr]["up"] = tmpStats.up;
            Storage["curr"][usr->MyStr]["down"] = tmpStats.down;
            Storage["curr"][usr->MyStr]["conntime"] = tmpStats.conntime;
            Storage["curr"][usr->MyStr]["host"] = tmpStats.host;
            Storage["curr"][usr->MyStr]["start"] = (unsigned int) time(0) - tmpStats.conntime;
          }
        }
      }
      usr->Send();
    }
    usr->Disconnect("Closed");
  }

  /// Starts a loop, waiting for connections to send data to.
  int Start(int argc, char ** argv) {
    //first make sure no segpipe signals will kill us
    struct sigaction new_action;
    new_action.sa_handler = termination_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction (SIGPIPE, &new_action, NULL);

    //then check and parse the commandline
    if (argc < 2) {
      std::cout << "usage: " << argv[0] << " streamName [awaiting_IP]" << std::endl;
      return 1;
    }
    bool ip_waiting = false;
    if (argc >= 4){
      waiting_ip += argv[2];
      ip_waiting = true;
    }
    std::string shared_socket = "/tmp/shared_socket_";
    shared_socket += argv[1];

    Socket::Server SS(shared_socket, true);
    Strm = new DTSC::Stream(5);
    std::vector<user> users;
    std::vector<user>::iterator usersIt;
    std::string inBuffer;
    char charBuffer[1024*10];
    unsigned int charCount;
    unsigned int stattimer = 0;
    unsigned int lastPacketTime = 0;//time in MS last packet was parsed
    unsigned int currPacketTime = 0;//time of the last parsed packet (current packet)
    unsigned int prevPacketTime = 0;//time of the previously parsed packet (current packet - 1)
    Socket::Connection incoming;
    Socket::Connection std_input(fileno(stdin));
    Socket::Connection StatsSocket = Socket::Connection("/tmp/ddv_statistics", true);

    Storage["log"].null();
    Storage["curr"].null();
    Storage["totals"].null();

    while (!feof(stdin) || ip_waiting){
      usleep(1000); //sleep for 1 ms, to prevent 100% CPU time
      unsigned int now = time(0);
      if (now != stattimer){
        stattimer = now;
        unsigned int tot_up = 0, tot_down = 0, tot_count = 0;
        if (users.size() > 0){
          for (usersIt = users.begin(); usersIt != users.end(); usersIt++){
            tot_down += usersIt->curr_down;
            tot_up += usersIt->curr_up;
            tot_count++;
          }
        }
        Storage["totals"]["down"] = tot_down;
        Storage["totals"]["up"] = tot_up;
        Storage["totals"]["count"] = tot_count;
        Storage["totals"]["now"] = now;
        Storage["totals"]["buffer"] = argv[1];
        if (!StatsSocket.connected()){
          StatsSocket = Socket::Connection("/tmp/ddv_statistics", true);
        }
        if (StatsSocket.connected()){
          StatsSocket.write(Storage.toString()+"\n\n");
          Storage["log"].null();
        }
      }
      //invalidate the current buffer
      if ( (!ip_waiting && std_input.canRead()) || (ip_waiting && ip_input.connected()) ){
        //slow down packet receiving to real-time
        if ((getNowMS() - lastPacketTime > currPacketTime - prevPacketTime) || (currPacketTime <= prevPacketTime)){
          std::cin.read(charBuffer, 1024*10);
          charCount = std::cin.gcount();
          inBuffer.append(charBuffer, charCount);
          if (Strm->parsePacket(inBuffer)){
            lastPacketTime = getNowMS();
            prevPacketTime = currPacketTime;
            currPacketTime = Strm->getTime();
          }
        }
      }

      //check for new connections, accept them if there are any
      //starts a thread for every accepted connection
      incoming = SS.accept(false);
      if (incoming.connected()){
        std::cerr << "New socket: " << incoming.getSocket() << std::endl;
        users.push_back(incoming);
        user * usr_ptr = &(users.back());
        usr_ptr->Thread = new tthread::thread(handleUser, (void *)usr_ptr);
      }

      //erase disconnected users
      if (users.size() > 0){
        for (usersIt = users.begin(); usersIt != users.end(); usersIt++){
          if (!(*usersIt).S.connected()){users.erase(usersIt); break;}
        }
      }

    }//main loop

    // disconnect listener
    /// \todo Deal with EOF more nicely - doesn't send the end of the stream to all users!
    std::cout << "Reached EOF of input" << std::endl;
    SS.close();

    while (users.size() > 0){
      for (usersIt = users.begin(); usersIt != users.end(); usersIt++){
        if ((*usersIt).S.connected()){
          (*usersIt).Disconnect("Terminating...");
        }else{
          users.erase(usersIt);
          break;
        }
      }
    }
    
    delete Strm;
    return 0;
  }

};//Buffer namespace

/// Entry point for Buffer, simply calls Buffer::Start().
int main(int argc, char ** argv){
  return Buffer::Start(argc, argv);
}//main
