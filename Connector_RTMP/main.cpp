#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include "handshake.cpp" //handshaking
#include "chunkstream.cpp" //chunkstream decoding
#include "amf.cpp" //simple AMF0 parsing

int main(){
  doHandshake();

  chunkpack next;
  std::vector<AMFType> * amfdata = 0;
  while (!feof(stdin)){
    next = getWholeChunk();
    if (next.cs_id == 2 && next.msg_stream_id == 0){
      fprintf(stderr, "Received protocol message. (cs_id 2, stream id 0)\nContents:\n");
      fwrite(next.data, 1, next.real_len, stderr);
      fflush(stderr);
    }
    switch (next.msg_type_id){
      case 1:
        fprintf(stderr, "CTRL: Set chunk size\n");
        break;
      case 2:
        fprintf(stderr, "CTRL: Abort message\n");
        break;
      case 3:
        fprintf(stderr, "CTRL: Acknowledgement\n");
        break;
      case 4:
        fprintf(stderr, "CTRL: User control message\n");
        break;
      case 5:
        fprintf(stderr, "CTRL: Window size\n");
        break;
      case 6:
        fprintf(stderr, "CTRL: Set peer bandwidth\n");
        break;
      case 8:
        fprintf(stderr, "Received audio data\n");
        break;
      case 9:
        fprintf(stderr, "Received video data\n");
        break;
      case 15:
        fprintf(stderr, "Received AFM3 data message\n");
        break;
      case 16:
        fprintf(stderr, "Received AFM3 shared object\n");
        break;
      case 17:
        fprintf(stderr, "Received AFM3 command message\n");
        break;
      case 18:
        fprintf(stderr, "Received AFM0 data message\n");
        break;
      case 19:
        fprintf(stderr, "Received AFM0 shared object\n");
        break;
      case 20:
        fprintf(stderr, "Received AFM0 command message\n");
        if (amfdata != 0){delete amfdata;}
        amfdata = parseAMF(next.data, next.real_len);
        
        break;
      case 22:
        fprintf(stderr, "Received aggregate message\n");
        break;
      default:
        fprintf(stderr, "Unknown chunk received!\n");
        break;
    }
  }

  
  return 0;
}//main
