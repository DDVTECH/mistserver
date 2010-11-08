#define DEBUG
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/epoll.h>

//for connection to server
bool ready4data = false;//set to true when streaming starts
bool inited = false;
bool stopparsing = false;
timeval lastrec;

int CONN_fd = 0;
#include "../util/ddv_socket.cpp" //DDVTech Socket wrapper
#include "../util/flv_sock.cpp" //FLV parsing with SocketW
#include "parsechunks.cpp" //chunkstream parsing
#include "handshake.cpp" //handshaking



int server_socket = 0;

void termination_handler (int signum){
  if (server_socket == 0) return;
  close(server_socket);
  server_socket = 0;
}

int main(int argc, char ** argv){
  //setup signal handler
  struct sigaction new_action;
  new_action.sa_handler = termination_handler;
  sigemptyset (&new_action.sa_mask);
  new_action.sa_flags = 0;
  sigaction (SIGINT, &new_action, NULL);
  sigaction (SIGHUP, &new_action, NULL);
  sigaction (SIGTERM, &new_action, NULL);
  
  server_socket = DDV_Listen(1935);
  if ((argc < 2) || (argv[1] == "nd")){
    if (server_socket > 0){daemon(1, 0);}else{return 1;}
  }
  int status;
  while (server_socket > 0){
    waitpid((pid_t)-1, &status, WNOHANG);
    CONN_fd = DDV_Accept(server_socket);
    if (CONN_fd > 0){
      pid_t myid = fork();
      if (myid == 0){
        break;
      }else{
        printf("Spawned new process %i for handling socket %i\n", (int)myid, CONN_fd);
      }
    }
  }
  if (server_socket <= 0){
    return 0;
  }

  unsigned int ts;
  unsigned int fts = 0;
  unsigned int ftst;
  int ss;
  FLV_Pack * tag;

  //first timestamp set
  firsttime = getNowMS();

  #ifdef DEBUG
  fprintf(stderr, "Doing handshake...\n");
  #endif
  if (doHandshake()){
    #ifdef DEBUG
    fprintf(stderr, "Handshake succcess!\n");
    #endif
  }else{
    #ifdef DEBUG
    fprintf(stderr, "Handshake fail!\n");
    #endif
    return 0;
  }
  #ifdef DEBUG
  fprintf(stderr, "Starting processing...\n");
  #endif


  int retval;
  int poller = epoll_create(1);
  struct epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.fd = CONN_fd;
  epoll_ctl(poller, EPOLL_CTL_ADD, CONN_fd, &ev);
  struct epoll_event events[1];

  
    
  
  while (!socketError && !All_Hell_Broke_Loose){
    //only parse input if available or not yet init'ed
    //rightnow = getNowMS();
    retval = epoll_wait(poller, events, 1, 0);
    if (!ready4data || (snd_cnt - snd_window_at >= snd_window_size)){
      if (DDV_ready(CONN_fd)){
        parseChunk();
      }
    }
    if (ready4data){
      if (!inited){
        //we are ready, connect the socket!
        ss = DDV_OpenUnix(streamname.c_str());
        if (ss <= 0){
          #ifdef DEBUG
          fprintf(stderr, "Could not connect to server!\n");
          #endif
          return 0;
        }
        #ifdef DEBUG
        fprintf(stderr, "Everything connected, starting to send video data...\n");
        #endif
        inited = true;
      }
      //only send data if previous data has been ACK'ed...
      //if (snd_cnt - snd_window_at < snd_window_size){
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
          #ifdef DEBUG
          fprintf(stderr, "Sent a tag to %i\n", CONN_fd);
          #endif
        }
      //}
    }
    //send ACK if we received a whole window
    if (rec_cnt - rec_window_at > rec_window_size){
      rec_window_at = rec_cnt;
      SendCTL(3, rec_cnt);//send ack (msg 3)
    }
  }
  //#ifdef DEBUG
  fprintf(stderr, "User %i disconnected.\n", CONN_fd);
  //#endif
  return 0;
}//main
