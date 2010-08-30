//#define DEBUG(args...) //debugging disabled
#define DEBUG(args...) fprintf(stderr, args) //debugging enabled

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
bool stopparsing = false;
timeval lastrec;

#include "../util/flv_sock.cpp" //FLV parsing with SocketW

int main(){
  unsigned int ts;
  unsigned int fts = 0;
  unsigned int ftst;
  SWUnixSocket ss;
  fd_set pollset;
  struct timeval timeout;
  //0 timeout - return immediately after select call
  timeout.tv_sec = 1; timeout.tv_usec = 0;
  FD_ZERO(&pollset);//clear the polling set
  FD_SET(0, &pollset);//add stdin to polling set

  //first timestamp set
  firsttime = getNowMS();

  DEBUG("Starting processing...\n");
  while (!feof(stdin)){
    //select(1, &pollset, 0, 0, &timeout);
    //only parse input from stdin if available or not yet init'ed
    //FD_ISSET(0, &pollset) || //NOTE: Polling does not work? WHY?!? WHY DAMN IT?!?
    if (!ready4data && !stopparsing){
      //JARON: verwerk rtsp data
    }
    if (ready4data){
      if (!inited){
        //we are ready, connect the socket!
        if (!ss.connect("/tmp/shared_socket")){
          DEBUG("Could not connect to server!\n");
          return 0;
        }
        FLV_Readheader(ss);//read the header, we don't want it
        DEBUG("Header read, starting to send video data...\n");
        
        //ERIK: Connect de RTP lib hier. De poorten weet je nog niet, dus doe dit later pas, als mijn deel af is.
        
        inited = true;
      }
      if (FLV_GetPacket(ss)){//able to read a full packet?
        //gooi de time uit de flv codec naar de juiste offsets voor deze kijker
        ts = FLVbuffer[7] * 256*256*256;
        ts += FLVbuffer[4] * 256*256;
        ts += FLVbuffer[5] * 256;
        ts += FLVbuffer[6];
        if (ts != 0){
          if (fts == 0){fts = ts;ftst = getNowMS();}
          ts -= fts;
          FLVbuffer[7] = ts / (256*256*256);
          FLVbuffer[4] = ts / (256*256);
          FLVbuffer[5] = ts / 256;
          FLVbuffer[6] = ts % 256;
          ts += ftst;
        }else{
          ftst = getNowMS();
          FLVbuffer[7] = ftst / (256*256*256);
          FLVbuffer[4] = ftst / (256*256);
          FLVbuffer[5] = ftst / 256;
          FLVbuffer[6] = ftst % 256;
        }
        
        //ERIK: verstuur de packet hier!
        //FLV data incl. video tag header staat in FLVbuffer
        //lengte van deze data staat in FLV_len
        
        FLV_Dump();//dump packet and get ready for next
      }
      if ((SWBerr != SWBaseSocket::ok) && (SWBerr != SWBaseSocket::notReady)){
        DEBUG("No more data! :-(  (%s)\n", SWBerr.get_error().c_str());
        return 0;//no more input possible! Fail immediately.
      }
    }
  }
  DEBUG("User disconnected.\n");
  return 0;
}//main
