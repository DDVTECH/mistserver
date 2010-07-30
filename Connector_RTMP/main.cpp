#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cmath>

//needed for select
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

//for connection to server
#include "../sockets/SocketW.h"
bool ready4data = false;//set to true when streaming starts
bool inited = false;
#include "../util/flv.cpp" //FLV format parser
#include "handshake.cpp" //handshaking
#include "parsechunks.cpp" //chunkstream parsing

int main(){
  SWUnixSocket ss;
  FLV_Pack * FLV = 0;
  int ssfd = 0;
  fd_set pollset;
  struct timeval timeout;
  //0 timeout - return immediately after select call
  timeout.tv_sec = 1; timeout.tv_usec = 0;
  FD_ZERO(&pollset);//clear the polling set
  FD_SET(0, &pollset);//add stdin to polling set

  doHandshake();
  while (!feof(stdin)){
    select(1, &pollset, 0, 0, &timeout);
    //only parse input from stdin if available
    if (FD_ISSET(0, &pollset)){parseChunk();}
    if (ready4data){
      if (!inited){
        //we are ready, connect the socket!
        ss.connect("../shared_socket");
        ssfd = ss.get_fd(0);
        if (ssfd > 0){FD_SET(ssfd, &pollset);}else{return 1;}
        FLV_Readheader(ssfd);//read the header, we don't want it
        fprintf(stderr, "Header read\n");
        inited = true;
      }
      //only deal with FLV packets if we have any to receive
      if (FD_ISSET(ssfd, &pollset)){
        fprintf(stderr, "Getting packet...\n");
        FLV_GetPacket(FLV, ssfd);//read a full packet
        fprintf(stderr, "Sending a type %hhx packet...\n", (unsigned char)FLV->data[0]);
        SendMedia((unsigned char)FLV->data[0], (unsigned char *)FLV->data+11, FLV->len-15);
        fprintf(stderr, "Packet sent.\n");
      }
    }
  }
  return 0;
}//main
