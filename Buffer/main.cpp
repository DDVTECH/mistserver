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
#include "stream.h"

/// Holds all code unique to the Buffer.
namespace Buffer{

  volatile bool buffer_running = true; ///< Set to false when shutting down.
  Stream * thisStream = 0;
  Socket::Server SS; ///< The server socket.

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

  void handleStats(void * empty){
    if (empty != 0){return;}
    Socket::Connection StatsSocket = Socket::Connection("/tmp/mist/statistics", true);
    while (buffer_running){
      usleep(1000000); //sleep one second
      if (!StatsSocket.connected()){
        StatsSocket = Socket::Connection("/tmp/mist/statistics", true);
      }
      if (StatsSocket.connected()){
        StatsSocket.write(Stream::get()->getStats()+"\n\n");
      }
    }
  }

  void handleUser(void * v_usr){
    user * usr = (user*)v_usr;
    std::cerr << "Thread launched for user " << usr->MyStr << ", socket number " << usr->S.getSocket() << std::endl;

    usr->myRing = thisStream->getRing();
    if (!usr->S.write(thisStream->getHeader())){
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
            if (thisStream->checkWaitingIP(usr->inbuffer.substr(2))){
              if (thisStream->setInput(usr->S)){
                std::cout << "Push accepted!" << std::endl;
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
            usr->tmpStats = Stats(usr->inbuffer.substr(2));
            unsigned int secs = usr->tmpStats.conntime - usr->lastStats.conntime;
            if (secs < 1){secs = 1;}
            usr->curr_up = (usr->tmpStats.up - usr->lastStats.up) / secs;
            usr->curr_down = (usr->tmpStats.down - usr->lastStats.down) / secs;
            usr->lastStats = usr->tmpStats;
            thisStream->saveStats(usr->MyStr, usr->tmpStats);
          }
        }
      }
      usr->Send();
    }
    thisStream->cleanUsers();
    std::cerr << "User " << usr->MyStr << " disconnected, socket number " << usr->S.getSocket() << std::endl;
  }

  /// Loop reading DTSC data from stdin and processing it at the correct speed.
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
      if ((now - lastPacketTime >= currPacketTime - prevPacketTime) || (currPacketTime <= prevPacketTime)){
        std::cin.read(charBuffer, 1024*10);
        charCount = std::cin.gcount();
        inBuffer.append(charBuffer, charCount);
        thisStream->getWriteLock();
        if (thisStream->getStream()->parsePacket(inBuffer)){
          thisStream->getStream()->outPacket(0);
          lastPacketTime = now;
          prevPacketTime = currPacketTime;
          currPacketTime = thisStream->getStream()->getTime();
          thisStream->dropWriteLock(true);
        }else{
          thisStream->dropWriteLock(false);
        }
      }else{
        if (((currPacketTime - prevPacketTime) - (now - lastPacketTime)) > 999){
          usleep(999000);
        }else{
          usleep(((currPacketTime - prevPacketTime) - (now - lastPacketTime)) * 999);
        }
      }
    }
    buffer_running = false;
    SS.close();
  }

  /// Loop reading DTSC data from an IP push address.
  /// No changes to the speed are made.
  void handlePushin(void * empty){
    if (empty != 0){return;}
    std::string inBuffer;
    while (buffer_running){
      if (thisStream->getIPInput().connected()){
        if (thisStream->getIPInput().iread(inBuffer)){
          thisStream->getWriteLock();
          if (thisStream->getStream()->parsePacket(inBuffer)){
            thisStream->getStream()->outPacket(0);
            thisStream->dropWriteLock(true);
          }else{
            thisStream->dropWriteLock(false);
          }
        }
      }else{
        usleep(1000000);
      }
    }
    SS.close();
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
    std::string name = argv[1];

    SS = Socket::makeStream(name);
    thisStream = Stream::get();
    thisStream->setName(name);
    Socket::Connection incoming;
    Socket::Connection std_input(fileno(stdin));

    tthread::thread StatsThread = tthread::thread(handleStats, 0);
    tthread::thread * StdinThread = 0;
    if (argc < 3){
      StdinThread = new tthread::thread(handleStdin, 0);
    }else{
      thisStream->setWaitingIP(argv[2]);
      StdinThread = new tthread::thread(handlePushin, 0);
    }

    while (buffer_running && SS.connected()){
      //check for new connections, accept them if there are any
      //starts a thread for every accepted connection
      incoming = SS.accept(false);
      if (incoming.connected()){
        user * usr_ptr = new user(incoming);
        thisStream->addUser(usr_ptr);
        usr_ptr->Thread = new tthread::thread(handleUser, (void *)usr_ptr);
      }
    }//main loop

    // disconnect listener
    buffer_running = false;
    std::cout << "End of input file - buffer shutting down" << std::endl;
    SS.close();
    StatsThread.join();
    StdinThread->join();
    delete thisStream;
    return 0;
  }

};//Buffer namespace

/// Entry point for Buffer, simply calls Buffer::Start().
int main(int argc, char ** argv){
  return Buffer::Start(argc, argv);
}//main
