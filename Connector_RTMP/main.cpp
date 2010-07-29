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

bool ready4data = false;//set to true when streaming starts

#include "handshake.cpp" //handshaking
#include "parsechunks.cpp" //chunkstream parsing

int main(){
  fd_set pollset;
  struct timeval timeout;
  //0 timeout - return immediately after select call
  timeout.tv_sec = 1; timeout.tv_usec = 0;
  FD_ZERO(&pollset);//clear the polling set
  FD_SET(0, &pollset);//add stdin to polling set

  doHandshake();
  while (!feof(stdin)){
    select(1, &pollset, 0, 0, &timeout);
    if (FD_ISSET(0, &pollset)){
      //only try to parse a new chunk when one is available :-)
      std::cerr << "Parsing..." << std::endl;
      parseChunk();
    }
    if (ready4data){
      //check for packets, send them if needed
      std::cerr << "Sending crap..." << std::endl;
    }
  }
  return 0;
}//main
