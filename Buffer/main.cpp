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
#include "../util/flv_tag.h" //FLV format parser
#include "../util/socket.h" //Socket lib

#include <sys/epoll.h>

/// Holds all code unique to the Buffer.
namespace Buffer{

  ///A simple signal handler that ignores all signals.
  void termination_handler (int signum){
    switch (signum){
      case SIGPIPE: return; break;
      default: return; break;
    }
  }

  ///holds FLV::Tag objects and their numbers
  struct buffer{
    int number;
    FLV::Tag FLV;
  };//buffer

  /// Holds connected users.
  /// Keeps track of what buffer users are using and the connection status.
  class user{
    public:
      int MyBuffer; ///< Index of currently used buffer.
      int MyBuffer_num; ///< Number of currently used buffer.
      int MyBuffer_len; ///< Length in bytes of currently used buffer.
      int MyNum; ///< User ID of this user.
      int currsend; ///< Current amount of bytes sent.
      bool gotproperaudio; ///< Whether the user received proper audio yet.
      void * lastpointer; ///< Pointer to data part of current buffer.
      static int UserCount; ///< Global user counter.
      Socket::Connection S; ///< Connection to user
      /// Creates a new user from a newly connected socket.
      /// Also prints "User connected" text to stdout.
      user(Socket::Connection fd){
        S = fd;
        MyNum = UserCount++;
        gotproperaudio = false;
        std::cout << "User " << MyNum << " connected" << std::endl;
      }//constructor
      /// Disconnects the current user. Doesn't do anything if already disconnected.
      /// Prints "Disconnected user" to stdout if disconnect took place.
      void Disconnect(std::string reason) {
        if (S.connected()) {
          S.close();
          std::cout << "Disconnected user " << MyNum << ": " << reason << std::endl;
        }
      }//Disconnect
      /// Tries to send the current buffer, returns true if success, false otherwise.
      /// Has a side effect of dropping the connection if send will never complete.
      bool doSend(){
        int r = S.iwrite((char*)lastpointer+currsend, MyBuffer_len-currsend);
        if (r <= 0){
          if (errno == EWOULDBLOCK){return false;}
          Disconnect(S.getError());
          return false;
        }
        currsend += r;
        return (currsend == MyBuffer_len);
      }//doSend
      /// Try to send data to this user. Disconnects if any problems occur.
      /// \param ringbuf Array of buffers (FLV:Tag with ID attached)
      /// \param buffers Count of elements in ringbuf
      void Send(buffer ** ringbuf, int buffers){
        /// \todo For MP3: gotproperaudio - if false, only send if first byte is 0xFF and set to true
        if (!S.connected()){return;}//cancel if not connected

        //still waiting for next buffer? check it
        if (MyBuffer_num < 0){
          MyBuffer_num = ringbuf[MyBuffer]->number;
          if (MyBuffer_num < 0){
            return; //still waiting? don't crash - wait longer.
          }else{
            MyBuffer_len = ringbuf[MyBuffer]->FLV.len;
            lastpointer = ringbuf[MyBuffer]->FLV.data;
          }
        }

        //do check for buffer resizes
        if (lastpointer != ringbuf[MyBuffer]->FLV.data){
          Disconnect("Buffer resize at wrong time... had to disconnect");
          return;
        }

        //try to complete a send
        if (doSend()){
          //switch to next buffer
          if ((ringbuf[MyBuffer]->number != MyBuffer_num)){
            //if corrupt data, warn and find keyframe
            std::cout << "Warning: User " << MyNum << " was send corrupt video data and send to the next keyframe!" << std::endl;
            int nocrashcount = 0;
            do{
              MyBuffer++;
              nocrashcount++;
              MyBuffer %= buffers;
            }while(!ringbuf[MyBuffer]->FLV.isKeyframe && (nocrashcount < buffers));
            //if keyframe not available, try again later
            if (nocrashcount >= buffers){
              std::cout << "Warning: No keyframe found in buffers! Skipping search for now..." << std::endl;
              return;
            }
          }else{
            MyBuffer++;
            MyBuffer %= buffers;
          }
          MyBuffer_num = -1;
          lastpointer = 0;
          currsend = 0;
        }//completed a send
      }//send
  };
  int user::UserCount = 0;

  /// Starts a loop, waiting for connections to send video data to.
  int Start(int argc, char ** argv) {
    //first make sure no segpipe signals will kill us
    struct sigaction new_action;
    new_action.sa_handler = termination_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction (SIGPIPE, &new_action, NULL);

    //then check and parse the commandline
    if (argc < 3) {
      std::cout << "usage: " << argv[0] << " buffers_count streamname" << std::endl;
      return 1;
    }
    std::string shared_socket = "/tmp/shared_socket_";
    shared_socket += argv[2];

    Socket::Server SS(shared_socket, true);
    FLV::Tag metadata;
    FLV::Tag video_init;
    FLV::Tag audio_init;
    int buffers = atoi(argv[1]);
    buffer ** ringbuf = (buffer**) calloc (buffers,sizeof(buffer*));
    std::vector<user> users;
    std::vector<user>::iterator usersIt;
    for (int i = 0; i < buffers; ++i) ringbuf[i] = new buffer;
    int current_buffer = 0;
    int lastproper = 0;//last properly finished buffer number
    unsigned int loopcount = 0;
    Socket::Connection incoming;

    unsigned char packtype;
    bool gotVideoInfo = false;
    bool gotAudioInfo = false;

    int infile = fileno(stdin);//get file number for stdin

    //add stdin to an epoll
    int poller = epoll_create(1);
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = infile;
    epoll_ctl(poller, EPOLL_CTL_ADD, infile, &ev);
    struct epoll_event events[1];


    while(!feof(stdin) && !FLV::Parse_Error){
      //invalidate the current buffer
      ringbuf[current_buffer]->number = -1;
      if ((epoll_wait(poller, events, 1, 10) > 0) && ringbuf[current_buffer]->FLV.FileLoader(stdin)){
        loopcount++;
        packtype = ringbuf[current_buffer]->FLV.data[0];
        //store metadata, if available
        if (packtype == 0x12){
          metadata = ringbuf[current_buffer]->FLV;
          std::cout << "Received metadata!" << std::endl;
          if (gotVideoInfo && gotAudioInfo){
            FLV::Parse_Error = true;
            std::cout << "... after proper video and audio? Cancelling broadcast!" << std::endl;
          }
          gotVideoInfo = false;
          gotAudioInfo = false;
        }
        //store video init data, if available
        if (!gotVideoInfo && ringbuf[current_buffer]->FLV.isKeyframe){
          if ((ringbuf[current_buffer]->FLV.data[11] & 0x0f) == 7){//avc packet
            if (ringbuf[current_buffer]->FLV.data[12] == 0){
              ringbuf[current_buffer]->FLV.tagTime(0);//timestamp to zero
              video_init = ringbuf[current_buffer]->FLV;
              gotVideoInfo = true;
              std::cout << "Received video configuration!" << std::endl;
            }
          }else{gotVideoInfo = true;}//non-avc = no config...
        }
        //store audio init data, if available
        if (!gotAudioInfo && (packtype == 0x08)){
          if (((ringbuf[current_buffer]->FLV.data[11] & 0xf0) >> 4) == 10){//aac packet
            ringbuf[current_buffer]->FLV.tagTime(0);//timestamp to zero
            audio_init = ringbuf[current_buffer]->FLV;
            gotAudioInfo = true;
            std::cout << "Received audio configuration!" << std::endl;
          }else{gotAudioInfo = true;}//no aac = no config...
        }
        //on keyframe set possible start point
        if (packtype == 0x09){
          if (((ringbuf[current_buffer]->FLV.data[11] & 0xf0) >> 4) == 1){
            lastproper = current_buffer;
          }
        }
        //keep track of buffers
        ringbuf[current_buffer]->number = loopcount;
        current_buffer++;
        current_buffer %= buffers;
      }

      //check for new connections, accept them if there are any
      incoming = SS.accept(true);
      if (incoming.connected()){
        users.push_back(incoming);
        //send the FLV header
        users.back().currsend = 0;
        users.back().MyBuffer = lastproper;
        users.back().MyBuffer_num = -1;
        /// \todo Do this more nicely?
        if (!users.back().S.write(FLV::Header, 13)){
          users.back().Disconnect("failed to receive the header!");
        }else{
          if (!users.back().S.write(metadata.data, metadata.len)){
            users.back().Disconnect("failed to receive metadata!");
          }
          if (!users.back().S.write(audio_init.data, audio_init.len)){
            users.back().Disconnect("failed to receive audio init!");
          }
          if (!users.back().S.write(video_init.data, video_init.len)){
            users.back().Disconnect("failed to receive video init!");
          }
        }
      }

      //send all connections what they need, if and when they need it
      if (users.size() > 0){
        for (usersIt = users.begin(); usersIt != users.end(); usersIt++){
          if (!(*usersIt).S.connected()){
            users.erase(usersIt); break;
          }else{
            (*usersIt).Send(ringbuf, buffers);
          }
        }
      }
    }//main loop

    // disconnect listener
    if (FLV::Parse_Error){
      std::cout << "FLV parse error" << std::endl;
    }else{
      std::cout << "Reached EOF of input" << std::endl;
    }
    SS.close();
    while (users.size() > 0){
      for (usersIt = users.begin(); usersIt != users.end(); usersIt++){
        (*usersIt).Disconnect("Shutting down...");
        if (!(*usersIt).S.connected()){users.erase(usersIt);break;}
      }
    }
    return 0;
  }

};//Buffer namespace

/// Entry point for Buffer, simply calls Buffer::Start().
int main(int argc, char ** argv){
  Buffer::Start(argc, argv);
}//main
