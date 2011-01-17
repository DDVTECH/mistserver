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

//for connection to server
bool ready4data = false;//set to true when streaming starts
bool inited = false;
bool stopparsing = false;
timeval lastrec;

#define DEFAULT_PORT 1935
#include "../util/server_setup.cpp"

int CONN_fd = 0;
#include "parsechunks.cpp" //chunkstream parsing
#include "handshake.cpp" //handshaking

int mainHandler(int connection){
  CONN_fd = connection;
  unsigned int ts;
  unsigned int fts = 0;
  unsigned int ftst;
  int ss;
  FLV_Pack * tag = 0;

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
  
  while (!socketError && !All_Hell_Broke_Loose){
    //only parse input if available or not yet init'ed
    //rightnow = getNowMS();
    retval = epoll_wait(poller, events, 1, 1);
    if ((retval > 0) || !ready4data){// || (snd_cnt - snd_window_at >= snd_window_size)
      switch (DDV_ready(CONN_fd)){
        case 0:
          socketError = true;
          #if DEBUG >= 1
          fprintf(stderr, "User socket is disconnected.\n");
          #endif
          break;
        case -1: break;//not ready yet
        default:
          parseChunk();
          break;
      }
    }
    if (ready4data){
      if (!inited){
        //we are ready, connect the socket!
        ss = DDV_OpenUnix(streamname);
        if (ss <= 0){
          #if DEBUG >= 1
          fprintf(stderr, "Could not connect to server!\n");
          #endif
          socketError = 1;
          break;
        }
        ev.events = EPOLLIN;
        ev.data.fd = ss;
        epoll_ctl(sspoller, EPOLL_CTL_ADD, ss, &ev);
        #if DEBUG >= 3
        fprintf(stderr, "Everything connected, starting to send video data...\n");
        #endif
        inited = true;
      }

      retval = epoll_wait(sspoller, events, 1, 1);
      switch (DDV_ready(ss)){
        case 0:
          socketError = true;
          #if DEBUG >= 1
          fprintf(stderr, "Source socket is disconnected.\n");
          #endif
          break;
        case -1: break;//not ready yet
        default:
          if (FLV_GetPacket(tag, ss)){//able to read a full packet?
            ts = tag->data[7] * 256*256*256;
            ts += tag->data[4] * 256*256;
            ts += tag->data[5] * 256;
            ts += tag->data[6];
            if (ts != 0){
              if (fts == 0){fts = ts;ftst = getNowMS();}
              ts -= fts;
              tag->data[7] = ts / (256*256*256);
              tag->data[4] = ts / (256*256);
              tag->data[5] = ts / 256;
              tag->data[6] = ts % 256;
              ts += ftst;
            }else{
              ftst = getNowMS();
              tag->data[7] = ftst / (256*256*256);
              tag->data[4] = ftst / (256*256);
              tag->data[5] = ftst / 256;
              tag->data[6] = ftst % 256;
            }
            SendMedia((unsigned char)tag->data[0], (unsigned char *)tag->data+11, tag->len-15, ts);
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
  close(CONN_fd);
  #if DEBUG >= 5
  fclose(tmpfile);
  #endif
  if (inited) close(ss);
  #if DEBUG >= 1
  if (All_Hell_Broke_Loose){fprintf(stderr, "All Hell Broke Loose\n");}
  fprintf(stderr, "User %i disconnected.\n", CONN_fd);
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
}//mainHandler
