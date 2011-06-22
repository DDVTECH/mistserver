/// \file RTMP_Parser/main.cpp
/// Debugging tool for RTMP data.
/// Expects RTMP data of one side of the conversion through stdin, outputs human-readable information to stderr.
/// Automatically skips 3073 bytes of handshake data.

#define DEBUG 10 //maximum debugging level
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include "../util/amf.h"
#include "../util/rtmpchunks.h"

/// Debugging tool for RTMP data.
/// Expects RTMP data of one side of the conversion through stdin, outputs human-readable information to stderr.
/// Will output FLV file to stdout, if available
/// Automatically skips 3073 bytes of handshake data.
int main(){

  std::string inbuffer;
  while (std::cin.good()){inbuffer += std::cin.get();}//read all of std::cin to temp
  inbuffer.erase(0, 3073);//strip the handshake part
  RTMPStream::Chunk next;
  AMF::Object amfdata("empty", AMF::AMF0_DDV_CONTAINER);
  AMF::Object3 amf3data("empty", AMF::AMF3_DDV_CONTAINER);
  

  while (next.Parse(inbuffer)){
    switch (next.msg_type_id){
      case 0://does not exist
        fprintf(stderr, "Error chunk - %i, %i, %i, %i, %i\n", next.cs_id, next.timestamp, next.real_len, next.len_left, next.msg_stream_id);
        //return 0;
        break;//happens when connection breaks unexpectedly
      case 1://set chunk size
        RTMPStream::chunk_rec_max = ntohl(*(int*)next.data.c_str());
        fprintf(stderr, "CTRL: Set chunk size: %i\n", RTMPStream::chunk_rec_max);
        break;
      case 2://abort message - we ignore this one
        fprintf(stderr, "CTRL: Abort message: %i\n", ntohl(*(int*)next.data.c_str()));
        //4 bytes of stream id to drop
        break;
      case 3://ack
        RTMPStream::snd_window_at = ntohl(*(int*)next.data.c_str());
        fprintf(stderr, "CTRL: Acknowledgement: %i\n", RTMPStream::snd_window_at);
        break;
      case 4:{
        short int ucmtype = ntohs(*(short int*)next.data.c_str());
        switch (ucmtype){
          case 0:
            fprintf(stderr, "CTRL: User control message: stream begin %i\n", ntohl(*(int*)next.data.c_str()+2));
            break;
          case 1:
            fprintf(stderr, "CTRL: User control message: stream EOF %i\n", ntohl(*(int*)next.data.c_str()+2));
            break;
          case 2:
            fprintf(stderr, "CTRL: User control message: stream dry %i\n", ntohl(*(int*)next.data.c_str()+2));
            break;
          case 3:
            fprintf(stderr, "CTRL: User control message: setbufferlen %i\n", ntohl(*(int*)next.data.c_str()+2));
            break;
          case 4:
            fprintf(stderr, "CTRL: User control message: streamisrecorded %i\n", ntohl(*(int*)next.data.c_str()+2));
            break;
          case 6:
            fprintf(stderr, "CTRL: User control message: pingrequest %i\n", ntohl(*(int*)next.data.c_str()+2));
            break;
          case 7:
            fprintf(stderr, "CTRL: User control message: pingresponse %i\n", ntohl(*(int*)next.data.c_str()+2));
            break;
          default:
            fprintf(stderr, "CTRL: User control message: UNKNOWN %hi - %i\n", ucmtype, ntohl(*(int*)next.data.c_str()+2));
            break;
        }
      } break;
      case 5://window size of other end
        RTMPStream::rec_window_size = ntohl(*(int*)next.data.c_str());
        RTMPStream::rec_window_at = RTMPStream::rec_cnt;
        fprintf(stderr, "CTRL: Window size: %i\n", RTMPStream::rec_window_size);
        break;
      case 6:
        RTMPStream::snd_window_size = ntohl(*(int*)next.data.c_str());
        //4 bytes window size, 1 byte limit type (ignored)
        fprintf(stderr, "CTRL: Set peer bandwidth: %i\n", RTMPStream::snd_window_size);
        break;
      case 8:
        fprintf(stderr, "Received %i bytes audio data\n", next.len);
        break;
      case 9:
        fprintf(stderr, "Received %i bytes video data\n", next.len);
        break;
      case 15:
        fprintf(stderr, "Received AFM3 data message\n");
        break;
      case 16:
        fprintf(stderr, "Received AFM3 shared object\n");
        break;
      case 17:{
        fprintf(stderr, "Received AFM3 command message:\n");
          char soort = next.data[0];
          next.data = next.data.substr(1);
          if (soort == 0){
            amfdata = AMF::parse(next.data);
            amfdata.Print();
          }else{
            amf3data = AMF::parse3(next.data);
            amf3data.Print();
          }
        } break;
      case 18:{
        fprintf(stderr, "Received AFM0 data message (metadata):\n");
        amfdata = AMF::parse(next.data);
        amfdata.Print();
        } break;
      case 19:
        fprintf(stderr, "Received AFM0 shared object\n");
        break;
      case 20:{//AMF0 command message
        fprintf(stderr, "Received AFM0 command message:\n");
        amfdata = AMF::parse(next.data);
        amfdata.Print();
        } break;
      case 22:
        fprintf(stderr, "Received aggregate message\n");
        break;
      default:
        fprintf(stderr, "Unknown chunk received! Probably protocol corruption, stopping parsing of incoming data.\n");
        return 1;
        break;
    }//switch for type of chunk
  }//while chunk parsed
  fprintf(stderr, "No more readable data\n");
  return 0;
}//main