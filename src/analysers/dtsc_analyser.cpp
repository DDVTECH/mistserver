/// \file dtsc_analyser.cpp
/// Contains the code for the DTSC Analysing tool.

#include <fcntl.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "../../lib/dtsc.h" //DTSC support

/// Reads DTSC from stdin and outputs human-readable information to stderr.
int main() {
  DTSC::Stream Strm;

  std::string inBuffer;
  char charBuffer[1024*10];
  unsigned int charCount;
  bool doneheader = false;

  long long unsigned int firstpack = 0;
  long long unsigned int nowpack = 0;
  long long unsigned int lastaudio = 0;
  long long unsigned int lastvideo = 0;
  long long unsigned int lastkey = 0;
  long long unsigned int totalvideo = 0;
  long long unsigned int totalaudio = 0;
  long long unsigned int keyframes = 0;
  long long unsigned int key_min = 0xffffffff;
  long long unsigned int key_max = 0;
  long long unsigned int vid_min = 0xffffffff;
  long long unsigned int vid_max = 0;
  long long unsigned int aud_min = 0xffffffff;
  long long unsigned int aud_max = 0;
  long long unsigned int bfrm_min = 0xffffffff;
  long long unsigned int bfrm_max = 0;
  std::string datatype;
  long long unsigned int bps = 0;

  while(std::cin.good()){
    //invalidate the current buffer
    if (Strm.parsePacket(inBuffer)){
      if (!doneheader){
        doneheader = true;
        Strm.metadata.Print();
      }
      Strm.getPacket().Print();
      //get current timestamp
      nowpack = Strm.getPacket().getContentP("time")->NumValue();
      if (firstpack == 0){firstpack = nowpack;}
      datatype = Strm.getPacket().getContentP("datatype")->StrValue();

      if (datatype == "audio"){
        if (lastaudio != 0 && (nowpack - lastaudio) != 0){
          bps = Strm.lastData().size() / (nowpack - lastaudio);
          if (bps < aud_min){aud_min = bps;}
          if (bps > aud_max){aud_max = bps;}
        }
        totalaudio += Strm.lastData().size();
        lastaudio = nowpack;
      }

      if (datatype == "video"){
        if (lastvideo != 0 && (nowpack - lastvideo) != 0){
          bps = Strm.lastData().size() / (nowpack - lastvideo);
          if (bps < vid_min){vid_min = bps;}
          if (bps > vid_max){vid_max = bps;}
        }
        if (Strm.getPacket().getContentP("keyframe") != 0){
          if (lastkey != 0){
            bps = nowpack - lastkey;
            if (bps < key_min){key_min = bps;}
            if (bps > key_max){key_max = bps;}
          }
          keyframes++;
          lastkey = nowpack;
        }
        if (Strm.getPacket().getContentP("offset") != 0){
          bps = Strm.getPacket().getContentP("offset")->NumValue();
          if (bps < bfrm_min){bfrm_min = bps;}
          if (bps > bfrm_max){bfrm_max = bps;}
        }
        totalvideo += Strm.lastData().size();
        lastvideo = nowpack;
      }

    }else{
      std::cin.read(charBuffer, 1024*10);
      charCount = std::cin.gcount();
      inBuffer.append(charBuffer, charCount);
    }
  }
  std::cout << std::endl << "Summary:" << std::endl;
  if (Strm.metadata.getContentP("audio") != 0){
    std::cout << "  Audio: " << Strm.metadata.getContentP("audio")->getContentP("codec")->StrValue() << std::endl;
    std::cout << "    Bitrate: " << aud_min << " - " << aud_max << " (avg: " << totalaudio / ((lastaudio - firstpack) / 1000) << ")" << std::endl;
  }
  if (Strm.metadata.getContentP("video") != 0){
    std::cout << "  Video: " << Strm.metadata.getContentP("video")->getContentP("codec")->StrValue() << std::endl;
    std::cout << "    Bitrate: " << vid_min << " - " << vid_max << " (avg: " << totalvideo / ((lastvideo - firstpack) / 1000) << ")" << std::endl;
    std::cout << "    Keyframes: " << key_min << " - " << key_max << " (avg: " << ((lastvideo - firstpack) / keyframes) << ")" << std::endl;
    std::cout << "    B-frames: " << bfrm_min << " - " << bfrm_max << std::endl;
  }
  
  return 0;
}
