//debugging level 0 = nothing
//debugging level 1 = critical errors
//debugging level 2 = errors
//debugging level 3 = status information
//debugging level 4 = extremely verbose status information
#define DEBUG 3

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
  switch (signum){
    case SIGINT: break;
    case SIGHUP: break;
    case SIGTERM: break;
    default: return; break;
  }
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
  fprintf(stderr, "Made a listening socket on port 1936...");
  if ((argc < 2) || (strcmp(argv[1], "nd") != 0)){
    if (server_socket > 0){
      daemon(1, 0);
      fprintf(stderr, "Going into background mode...");
    }else{
      fprintf(stderr, "Error: could not make listening socket");
      return 1;
    }
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
        fprintf(stderr, "Spawned new process %i for handling socket %i\n", (int)myid, CONN_fd);
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
        default: parseChunk(); break;
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

      retval = epoll_wait(poller, events, 1, 1);
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
            #if DEBUG >= 4
            fprintf(stderr, "Sent a tag to %i\n", CONN_fd);
            #endif
          }
          break;
      }
    }
    //send ACK if we received a whole window
    if (rec_cnt - rec_window_at > rec_window_size){
      rec_window_at = rec_cnt;
      SendCTL(3, rec_cnt);//send ack (msg 3)
    }
  }
  close(CONN_fd);
  #if DEBUG >= 1
  if (All_Hell_Broke_Loose){fprintf(stderr, "All Hell Broke Loose\n");}
  fprintf(stderr, "User %i disconnected.\n", CONN_fd);
  #endif
  return 0;
}//main
