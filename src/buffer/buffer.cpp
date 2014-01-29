/// \file buffer.cpp
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
#include <mist/config.h>
#include <mist/timing.h>
#include "buffer_stream.h"
#include <mist/stream.h>
#include <mist/defines.h>

/// Holds all code unique to the Buffer.
namespace Buffer {

  volatile bool buffer_running = true; ///< Set to false when shutting down.
  Stream * thisStream = 0;
  Socket::Server SS; ///< The server socket.

  ///\brief A function running in a thread to send all statistics.
  ///\param empty A null pointer.
  void handleStats(void * empty){
#if defined(_TTHREAD_POSIX_) && defined(WITH_THREADNAMES) && !(defined(__FreeBSD__) || defined(__APPLE__) || defined(__MACH__) || defined(_WIN32) || defined(__CYGWIN__))
    pthread_setname_np(pthread_self(), "StatsHandler");
#endif
    if (empty != 0){
      return;
    }
    std::string double_newline = "\n\n";
    Socket::Connection StatsSocket = Socket::Connection(Util::getTmpFolder() + "statistics", true);
    while (buffer_running){
      Util::sleep(1000); //sleep one second
      if ( !StatsSocket.connected()){
        StatsSocket = Socket::Connection(Util::getTmpFolder() + "statistics", true);
      }
      if (StatsSocket.connected()){
        StatsSocket.SendNow(Stream::get()->getStats());
        StatsSocket.SendNow(double_newline);
        if (StatsSocket.spool()){
          //Got a response.
          buffer_running = false;
        }
      }
    }
    StatsSocket.close();
  }

  ///\brief A function to handle input data.
  ///\param conn A socket reference.
  void handlePushIn(Socket::Connection & conn){
    #if defined(_TTHREAD_POSIX_) && defined(WITH_THREADNAMES) && !(defined(__FreeBSD__) || defined(__APPLE__) || defined(__MACH__) || defined(_WIN32) || defined(__CYGWIN__))
    pthread_setname_np(pthread_self(), "Push Input");
    #endif
    conn.setBlocking(true);
    int sockNo = 0;
    while (buffer_running && conn.connected()){
      while (thisStream->parsePacket(conn)){
        //do nothing while parsing
      }
      Util::sleep(10);//sleep to prevent high CPU usage
    }
    conn.close();
    if (buffer_running){
      thisStream->endStream();
    }
    long long int wait_time = Util::getMS();
    while (Util::getMS() - wait_time < thisStream->metadata.bufferWindow){
      Util::sleep(thisStream->metadata.bufferWindow - (Util::getMS() - wait_time));
    }
    thisStream->removeSocket(sockNo);
  }
  
  ///\brief A function running a thread to handle input data through stdin.
  ///Automatically slows down to realtime playback.
  ///\param empty A null pointer.
  void handleStdin(void * empty){
    if (empty != 0){
      return;
    }
    #if defined(_TTHREAD_POSIX_) && defined(WITH_THREADNAMES) && !(defined(__FreeBSD__) || defined(__APPLE__) || defined(__MACH__) || defined(_WIN32) || defined(__CYGWIN__))
    pthread_setname_np(pthread_self(), "Standard Input");
    #endif
    long long int timeDiff = 0; //difference between local time and stream time
    unsigned int lastPacket = 0; //last parsed packet timestamp
    std::string inBuffer;
    char charBuffer[1024 * 10];
    unsigned int charCount;
    long long int now;
    
    while (std::cin.good() && buffer_running){
      //slow down packet receiving to real-time
      now = Util::getMS();
      if (((now - timeDiff) >= lastPacket) || (lastPacket - (now - timeDiff) > 15000)){
        if (thisStream->parsePacket(inBuffer)){
          lastPacket = thisStream->getTime();
          if ((now - timeDiff - lastPacket) > 15000 || (now - timeDiff - lastPacket < -15000)){
            timeDiff = now - lastPacket;
          }
        }else{
          std::cin.read(charBuffer, 1024 * 10);
          charCount = std::cin.gcount();
          inBuffer.append(charBuffer, charCount);
        }
      }else{
        Util::sleep(std::min(15LL, lastPacket - (now - timeDiff)));
      }
    }
    buffer_running = false;
  }

  ///\brief A function running in a thread to handle a new user connection.
  ///\param v_usr The user that is connected.
  void handleUser(void * v_usr){
    std::set<int> allowedTracks;
    user * usr = (user*)v_usr;
    thisStream->addUser(usr);
#if DEBUG >= 5
    std::cerr << "Thread launched for user " << usr->sID << ", socket number " << usr->S.getSocket() << std::endl;
#endif
#if defined(_TTHREAD_POSIX_) && defined(WITH_THREADNAMES) && !(defined(__FreeBSD__) || defined(__APPLE__) || defined(__MACH__) || defined(_WIN32) || defined(__CYGWIN__))
    pthread_setname_np(pthread_self(), usr->sID.c_str());
#endif
    usr->myRing = thisStream->getRing();
    thisStream->sendMeta(usr->S);

    while (usr->S.connected()){
      if (usr->myRing->playCount){
        if (usr->myRing->waiting){
          Stream::get()->waitForData();
          if ( !Stream::get()->isNewest(usr->myRing->b, allowedTracks)){
            usr->myRing->waiting = false;
            usr->myRing->b = Stream::get()->getNext(usr->myRing->b, allowedTracks);
            if ((Stream::get()->getPacket(usr->myRing->b).isMember("keyframe") && (usr->myRing->playCount > 0)) || (usr->playUntil && usr->playUntil <= Stream::get()->getPacket(usr->myRing->b)["time"].asInt())){
              usr->myRing->playCount--;
              if (usr->myRing->playCount < 1 || usr->playUntil <= Stream::get()->getPacket(usr->myRing->b)["time"].asInt()){
                usr->myRing->playCount = 0;
                JSON::Value pausemark;
                pausemark["trackid"] = 0ll;
                pausemark["mark"] = "pause";
                pausemark["time"] = Stream::get()->getPacket(usr->myRing->b)["time"].asInt();
                pausemark.sendTo(usr->S);
              }
            }
          }
        }else{
          //complete a send
          Stream::get()->getPacket(usr->myRing->b).sendTo(usr->S);
          if ( !usr->S.connected()){break;}
          //switch to next buffer
          if (Stream::get()->isNewest(usr->myRing->b, allowedTracks)){
            //no next buffer? go in waiting mode.
            usr->myRing->waiting = true;
          }else{
            usr->myRing->b = Stream::get()->getNext(usr->myRing->b, allowedTracks);
            if ((Stream::get()->getPacket(usr->myRing->b).isMember("keyframe") && (usr->myRing->playCount > 0)) || (usr->playUntil && usr->playUntil <= Stream::get()->getPacket(usr->myRing->b)["time"].asInt())){
              usr->myRing->playCount--;
              if (usr->myRing->playCount < 1 || usr->playUntil <= Stream::get()->getPacket(usr->myRing->b)["time"].asInt()){
                usr->myRing->playCount = 0;
                JSON::Value pausemark;
                pausemark["trackid"] = 0ll;
                pausemark["mark"] = "pause";
                pausemark["time"] = Stream::get()->getPacket(usr->myRing->b)["time"].asInt();
                pausemark.sendTo(usr->S);
              }
            }
          }
        }
      }
      if (usr->S.spool()){
        while (usr->S.Received().size()){
          //delete anything that doesn't end with a newline
          if ( !usr->S.Received().get().empty() && *(usr->S.Received().get().rbegin()) != '\n'){
            usr->S.Received().get().clear();
            continue;
          }
          usr->S.Received().get().resize(usr->S.Received().get().size() - 1);
          if ( !usr->S.Received().get().empty()){
            switch (usr->S.Received().get()[0]){
              case 'P': { //Push
                if (thisStream->checkWaitingIP(usr->S.Received().get().substr(2))){
                  usr->S.Received().get().clear();
                  Socket::Connection tmp = usr->S;
                  usr->S = Socket::Connection( -1);
                  thisStream->removeUser(usr);
                  thisStream->dropRing(usr->myRing);
                  delete usr;
                  return handlePushIn(tmp);
                }else{
                  usr->Disconnect("Push denied - invalid IP address!");
                }
                break;
              }
              case 'S': { //Stats
                usr->tmpStats = Stats(usr->S.Received().get().substr(2));
                unsigned int secs = usr->tmpStats.conntime - usr->lastStats.conntime;
                if (secs < 1){
                  secs = 1;
                }
                usr->curr_up = (usr->tmpStats.up - usr->lastStats.up) / secs;
                usr->curr_down = (usr->tmpStats.down - usr->lastStats.down) / secs;
                usr->lastStats = usr->tmpStats;
                thisStream->saveStats(usr->sID, usr->tmpStats);
                thisStream->sendMeta(usr->S);
                break;
              }
              case 't': {
                if (usr->S.Received().get().size() >= 3){
                  allowedTracks.clear();
                  std::string tmp = usr->S.Received().get().substr(2);
                  while (tmp != ""){
                    allowedTracks.insert(atoi(tmp.substr(0,tmp.find(' ')).c_str()));
                    if (tmp.find(' ') != std::string::npos){
                      tmp.erase(0,tmp.find(' ')+1);
                    }else{
                      tmp = "";
                    }
                  }
                }
                break;
              }
              case 's': { //second-seek
                unsigned int ms = JSON::Value(usr->S.Received().get().substr(2)).asInt();
                usr->myRing->waiting = false;
                usr->myRing->starved = false;
                usr->myRing->b = thisStream->msSeek(ms, allowedTracks);
                if (usr->myRing->playCount > 0){
                  usr->myRing->playCount = 0;
                }
                break;
              }
              case 'p': { //play
                usr->myRing->playCount = -1;
                if (usr->S.Received().get().size() >= 2){
                  usr->playUntil = atoi(usr->S.Received().get().substr(2).c_str());
                }else{
                  usr->playUntil = 0;
                }
                break;
              }
              case 'o': { //once-play
                if (usr->myRing->playCount >= 0){
                  usr->myRing->playCount++;
                }
                break;
              }
              case 'q': { //quit-playing
                usr->myRing->playCount = 0;
                break;
              }
            }
            usr->S.Received().get().clear();
          }
        }
      }
      if (usr->myRing->waiting || !usr->myRing->playCount){
        Util::sleep(300); //sleep 300ms
      }
    }
    usr->Disconnect("Socket closed.");
    thisStream->dropRing(usr->myRing);
    thisStream->removeUser(usr);
    delete usr;
  }

  ///\brief Starts a loop, waiting for connections to send data to.
  ///\param argc The number of arguments to the program.
  ///\param argv The arguments to the program.
  ///\return The return code of the buffer.
  int Start(int argc, char ** argv){
    Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
    conf.addOption("stream_name",
        JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"Name of the stream this buffer will be providing.\"}"));
    conf.addOption("awaiting_ip",
        JSON::fromString(
            "{\"arg_num\":2, \"arg\":\"string\", \"default\":\"\", \"help\":\"IP address to expect incoming data from. This will completely disable reading from standard input if used.\"}"));
    conf.addOption("reportstats",
        JSON::fromString("{\"default\":0, \"help\":\"Report stats to a controller process.\", \"short\":\"s\", \"long\":\"reportstats\"}"));
    conf.addOption("time",
        JSON::fromString(
            "{\"default\":20000, \"arg\": \"integer\", \"help\":\"Buffer a specied amount of time in ms.\", \"short\":\"t\", \"long\":\"time\"}"));
    conf.parseArgs(argc, argv);

    std::string name = conf.getString("stream_name");

    SS = Util::Stream::makeLive(name);
    if ( !SS.connected()){
      perror("Could not create stream socket");
      return 1;
    }
    SS.setBlocking(false);
    conf.activate();
    #if defined(_TTHREAD_POSIX_) && defined(WITH_THREADNAMES) && !(defined(__FreeBSD__) || defined(__APPLE__) || defined(__MACH__) || defined(_WIN32) || defined(__CYGWIN__))
    pthread_setname_np(pthread_self(), "Main accepter");
    #endif
    thisStream = Stream::get();
    thisStream->setName(name);
    thisStream->setBufferTime(conf.getInteger("time"));
    Socket::Connection incoming;
    Socket::Connection std_input(fileno(stdin));

    if (conf.getBool("reportstats")){
      tthread::thread StatsThread(handleStats, 0);
      StatsThread.detach();
    }
    std::string await_ip = conf.getString("awaiting_ip");
    if (await_ip == ""){
      tthread::thread StdinThread(handleStdin, 0);
      StdinThread.detach();
    }else{
      thisStream->setWaitingIP(await_ip);
    }

    unsigned int userId = 0;
    SS.setBlocking(true);
    while (buffer_running && SS.connected() && conf.is_active){
      //check for new connections, accept them if there are any
      //starts a thread for every accepted connection
      incoming = SS.accept(true);
      if (incoming.connected()){
        tthread::thread thisUser(handleUser, (void *)new user(incoming, ++userId));
        thisUser.detach();
      }
    } //main loop

    // disconnect listener
    buffer_running = false;
    SS.close();
    delete thisStream;
    return 0;
  }

} //Buffer namespace

///\brief Entry point for Buffer, simply calls Buffer::Start().
int main(int argc, char ** argv){
  return Buffer::Start(argc, argv);
} //main
