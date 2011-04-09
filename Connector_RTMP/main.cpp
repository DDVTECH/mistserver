//debugging level 0 = nothing
//debugging level 1 = critical errors
//debugging level 2 = errors
//debugging level 3 = status information
//debugging level 4 = extremely verbose status information
//debugging level 5 = save all streams to FLV files
#define DEBUG 4

#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <getopt.h>
#include "../util/ddv_socket.h"
#include "../util/flv_tag.h"

#include "parsechunks.cpp" //chunkstream parsing
#include "handshake.cpp" //handshaking

/// Holds all functions and data unique to the RTMP Connector
namespace Connector_RTMP{

  //for connection to server
  bool ready4data = false; ///< Set to true when streaming starts.
  bool inited = false; ///< Set to true when ready to connect to Buffer.
  bool stopparsing = false; ///< Set to true when all parsing needs to be cancelled.
  timeval lastrec; ///< Timestamp of last received data.

  DDV::Socket Socket; ///< Socket connected to user

  /// Main Connector_RTMP function
  int Connector_RTMP(DDV::Socket conn){
    Socket = conn;
    unsigned int ts;
    unsigned int fts = 0;
    unsigned int ftst;
    DDV::Socket SS;
    FLV::Tag tag = 0;

    //first timestamp set
    firsttime = getNowMS();

    if (doHandshake()){
      #if DEBUG >= 4
      fprintf(stderr, "Handshake succcess!\n");
      #endif
    }else{
      #if DEBUG >= 1
      fprintf(stderr, "Handshake fail!\n");
      #endif
      return 0;
    }

    int retval;
    int poller = epoll_create(1);
    int sspoller = epoll_create(1);
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = CONN_fd;
    epoll_ctl(poller, EPOLL_CTL_ADD, CONN_fd, &ev);
    struct epoll_event events[1];
    #if DEBUG >= 5
    //for writing whole stream to a file
    FILE * tmpfile = 0;
    char tmpstr[200];
    #endif
  
    while (Socket.connected() && !FLV::Parse_Error){
      //only parse input if available or not yet init'ed
      //rightnow = getNowMS();
      retval = epoll_wait(poller, events, 1, 1);
      if ((retval > 0) || !ready4data){// || (snd_cnt - snd_window_at >= snd_window_size)
        switch (Socket.ready()){
          case -1: break; //disconnected
          case 0: break; //not ready yet
          default: parseChunk(); break; //new data is waiting
        }
      }
      if (ready4data){
        if (!inited){
          //we are ready, connect the socket!
          SS = DDV::Socket(streamname);
          if (!SS.connected()){
            #if DEBUG >= 1
            fprintf(stderr, "Could not connect to server!\n");
            #endif
            Socket.close();//disconnect user
            break;
          }
          ev.events = EPOLLIN;
          ev.data.fd = SS.getSocket();
          epoll_ctl(sspoller, EPOLL_CTL_ADD, SS.getSocket(), &ev);
          #if DEBUG >= 3
          fprintf(stderr, "Everything connected, starting to send video data...\n");
          #endif
          inited = true;
        }

        retval = epoll_wait(sspoller, events, 1, 1);
        switch (SS.ready()){
          case -1:
            #if DEBUG >= 1
            fprintf(stderr, "Source socket is disconnected.\n");
            #endif
            Socket.close();//disconnect user
            break;
          case 0: break;//not ready yet
          default:
            if (tag.SockLoader(SS)){//able to read a full packet?
              ts = tag.tagTime();
              if (ts != 0){
                if (fts == 0){fts = ts;ftst = getNowMS();}
                ts -= fts;
                tag.tagTime(ts);
                ts += ftst;
              }else{
                ftst = getNowMS();
                tag.tagTime(ftst);
              }
              SendMedia((unsigned char)tag.data[0], (unsigned char *)tag.data+11, tag.len-15, ts);
              #if DEBUG >= 5
              //write whole stream to a file
              if (tmpfile == 0){
                sprintf(tmpstr, "./tmpfile_socket_%i.flv", CONN_fd);
                tmpfile = fopen(tmpstr, "w");
                fwrite(FLVHeader, 13, 1, tmpfile);
              }
              fwrite(tag->data, tag->len, 1, tmpfile);
              #endif
              #if DEBUG >= 4
              fprintf(stderr, "Sent a tag to %i\n", CONN_fd);
              #endif
            }
            break;
        }
      }
      //send ACK if we received a whole window
      if ((rec_cnt - rec_window_at > rec_window_size)){
        rec_window_at = rec_cnt;
        SendCTL(3, rec_cnt);//send ack (msg 3)
      }
    }
    SS.close();
    Socket.close();
    #if DEBUG >= 5
    fclose(tmpfile);
    #endif
    #if DEBUG >= 1
    if (FLV::Parse_Error){fprintf(stderr, "FLV Parse Error\n");}
    fprintf(stderr, "User %i disconnected.\n", conn.getSocket());
    if (inited){
      fprintf(stderr, "Status was: inited\n");
    }else{
      if (ready4data){
        fprintf(stderr, "Status was: ready4data\n");
      }else{
        fprintf(stderr, "Status was: connected\n");
      }
    }
    #endif
    return 0;
  }//Connector_RTMP

};//Connector_RTMP namespace

// Load main server setup file, default port 1935, handler is Connector_RTMP::Connector_RTMP
#define DEFAULT_PORT 1935
#define MAINHANDLER Connector_RTMP::Connector_RTMP
#include "../util/server_setup.cpp"
