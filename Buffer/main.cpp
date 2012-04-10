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

  class user;//forward declaration
  JSON::Value Storage; ///< Global storage of data.
  DTSC::Stream * Strm = 0;
  std::string waiting_ip = ""; ///< IP address for media push.
  Socket::Connection ip_input; ///< Connection used for media push.
  tthread::mutex stats_mutex; ///< Mutex for stats modifications.
  tthread::mutex transfer_mutex; ///< Mutex for data transfers.
  tthread::mutex socket_mutex; ///< Mutex for user deletion/work.
  bool buffer_running = true; ///< Set to false when shutting down.
  std::vector<user> users; ///< All connected users.
  std::vector<user>::iterator usersIt; ///< Iterator for all connected users.
  std::string name; ///< Name for this buffer.
  tthread::condition_variable moreData; ///< Triggered when more data becomes available.

  /// Gets the current system time in milliseconds.
  unsigned int getNowMS(){
    timeval t;
    gettimeofday(&t, 0);
    return t.tv_sec + t.tv_usec/1000;
  }//getNowMS


  ///A simple signal handler that ignores all signals.
  void termination_handler (int signum){
    switch (signum){
      case SIGKILL: buffer_running = false; break;
      case SIGPIPE: return; break;
      default: return; break;
    }
  }
}

#include "stats.cpp"
#include "user.cpp"

namespace Buffer{
  void handleStats(void * empty){
    if (empty != 0){return;}
    Socket::Connection StatsSocket = Socket::Connection("/tmp/ddv_statistics", true);
    while (buffer_running){
      usleep(1000000); //sleep one second
      unsigned int now = time(0);
      unsigned int tot_up = 0, tot_down = 0, tot_count = 0;
      stats_mutex.lock();
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
      Storage["totals"]["buffer"] = name;
      if (!StatsSocket.connected()){
        StatsSocket = Socket::Connection("/tmp/ddv_statistics", true);
      }
      if (StatsSocket.connected()){
        StatsSocket.write(Storage.toString()+"\n\n");
        Storage["log"].null();
      }
      stats_mutex.unlock();
    }
  }

  void handleUser(void * v_usr){
    user * usr = (user*)v_usr;
    std::cerr << "Thread launched for user " << usr->MyStr << ", socket number " << usr->S.getSocket() << std::endl;

    usr->myRing = Strm->getRing();
    if (!usr->S.write(Strm->outHeader())){
      usr->Disconnect("failed to receive the header!");
      return;
    }

    while (usr->S.connected()){
      usleep(5000); //sleep 5ms
      if (usr->S.canRead()){
        usr->inbuffer.clear();
        char charbuf;
        while ((usr->S.iread(&charbuf, 1) == 1) && charbuf != '\n' ){
          usr->inbuffer += charbuf;
        }
        if (usr->inbuffer != ""){
          if (usr->inbuffer[0] == 'P'){
            std::cout << "Push attempt from IP " << usr->inbuffer.substr(2) << std::endl;
            if (usr->inbuffer.substr(2) == waiting_ip){
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
          if (usr->inbuffer[0] == 'S'){
            stats_mutex.lock();
            usr->tmpStats = Stats(usr->inbuffer.substr(2));
            unsigned int secs = usr->tmpStats.conntime - usr->lastStats.conntime;
            if (secs < 1){secs = 1;}
            usr->curr_up = (usr->tmpStats.up - usr->lastStats.up) / secs;
            usr->curr_down = (usr->tmpStats.down - usr->lastStats.down) / secs;
            usr->lastStats = usr->tmpStats;
            Storage["curr"][usr->MyStr]["connector"] = usr->tmpStats.connector;
            Storage["curr"][usr->MyStr]["up"] = usr->tmpStats.up;
            Storage["curr"][usr->MyStr]["down"] = usr->tmpStats.down;
            Storage["curr"][usr->MyStr]["conntime"] = usr->tmpStats.conntime;
            Storage["curr"][usr->MyStr]["host"] = usr->tmpStats.host;
            Storage["curr"][usr->MyStr]["start"] = (unsigned int) time(0) - usr->tmpStats.conntime;
            stats_mutex.unlock();
          }
        }
      }
      usr->Send();
    }
    stats_mutex.lock();
    if (users.size() > 0){
      for (usersIt = users.begin(); usersIt != users.end(); usersIt++){
        if (!(*usersIt).S.connected()){
          users.erase(usersIt);
          break;
        }
      }
    }
    stats_mutex.unlock();
    std::cerr << "User " << usr->MyStr << " disconnected, socket number " << usr->S.getSocket() << std::endl;
  }

  void handleStdin(void * empty){
    if (empty != 0){return;}
    unsigned int lastPacketTime = 0;//time in MS last packet was parsed
    unsigned int currPacketTime = 0;//time of the last parsed packet (current packet)
    unsigned int prevPacketTime = 0;//time of the previously parsed packet (current packet - 1)
    std::string inBuffer;
    char charBuffer[1024*10];
    unsigned int charCount;
    unsigned int now;

    while (std::cin.good() && buffer_running){
      //slow down packet receiving to real-time
      now = getNowMS();
      if ((now - lastPacketTime > currPacketTime - prevPacketTime) || (currPacketTime <= prevPacketTime)){
        std::cin.read(charBuffer, 1024*10);
        charCount = std::cin.gcount();
        inBuffer.append(charBuffer, charCount);
        transfer_mutex.lock();
        if (Strm->parsePacket(inBuffer)){
          Strm->outPacket(0);
          lastPacketTime = now;
          prevPacketTime = currPacketTime;
          currPacketTime = Strm->getTime();
          moreData.notify_all();
        }
        transfer_mutex.unlock();
      }else{
        if (((currPacketTime - prevPacketTime) - (now - lastPacketTime)) > 1000){
          usleep(1000000);
        }else{
          usleep(((currPacketTime - prevPacketTime) - (now - lastPacketTime)) * 1000);
        }
      }
    }
    buffer_running = false;
  }

  /// Starts a loop, waiting for connections to send data to.
  int Start(int argc, char ** argv) {
    //first make sure no segpipe signals will kill us
    struct sigaction new_action;
    new_action.sa_handler = termination_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction (SIGPIPE, &new_action, NULL);
    sigaction (SIGKILL, &new_action, NULL);

    //then check and parse the commandline
    if (argc < 2) {
      std::cout << "usage: " << argv[0] << " streamName [awaiting_IP]" << std::endl;
      return 1;
    }
    name = argv[1];
    bool ip_waiting = false;
    if (argc >= 4){
      waiting_ip += argv[2];
      ip_waiting = true;
    }
    std::string shared_socket = "/tmp/shared_socket_";
    shared_socket += name;

    Socket::Server SS(shared_socket, false);
    Strm = new DTSC::Stream(5);
    Socket::Connection incoming;
    Socket::Connection std_input(fileno(stdin));

    Storage["log"].null();
    Storage["curr"].null();
    Storage["totals"].null();

    //tthread::thread StatsThread = tthread::thread(handleStats, 0);
    tthread::thread * StdinThread = 0;
    if (!ip_waiting){
      StdinThread = new tthread::thread(handleStdin, 0);
    }

    while (buffer_running){
      //check for new connections, accept them if there are any
      //starts a thread for every accepted connection
      incoming = SS.accept(false);
      if (incoming.connected()){
        stats_mutex.lock();
        users.push_back(incoming);
        user * usr_ptr = &(users.back());
        stats_mutex.unlock();
        usr_ptr->Thread = new tthread::thread(handleUser, (void *)usr_ptr);
      }
    }//main loop

    // disconnect listener
    /// \todo Deal with EOF more nicely - doesn't send the end of the stream to all users!
    buffer_running = false;
    std::cout << "Buffer shutting down" << std::endl;
    SS.close();
    //StatsThread.join();
    if (StdinThread){StdinThread->join();}

    if (users.size() > 0){
      stats_mutex.lock();
      for (usersIt = users.begin(); usersIt != users.end(); usersIt++){
        if ((*usersIt).S.connected()){
          (*usersIt).Disconnect("Terminating...");
        }
      }
      stats_mutex.unlock();
    }
    
    delete Strm;
    return 0;
  }

};//Buffer namespace

/// Entry point for Buffer, simply calls Buffer::Start().
int main(int argc, char ** argv){
  return Buffer::Start(argc, argv);
}//main
