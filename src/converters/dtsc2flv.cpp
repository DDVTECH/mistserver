/// \file DTSC2FLV/main.cpp
/// Contains the code that will transform any valid DTSC input into valid FLVs.

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "../../lib/flv_tag.h" //FLV support
#include "../../lib/dtsc.h" //DTSC support
#include "../../lib/amf.h" //AMF support

/// Holds all code that converts filetypes to DTSC.
namespace Converters{

  /// Reads DTSC from STDIN, outputs FLV to STDOUT.
  int DTSC2FLV() {
    FLV::Tag FLV_out; // Temporary storage for outgoing FLV data.
    DTSC::Stream Strm;
    std::string inBuffer;
    char charBuffer[1024*10];
    unsigned int charCount;
    bool doneheader = false;

    while (std::cin.good()){
      std::cin.read(charBuffer, 1024*10);
      charCount = std::cin.gcount();
      inBuffer.append(charBuffer, charCount);
      if (Strm.parsePacket(inBuffer)){
        if (!doneheader){
          doneheader = true;
          std::cout.write(FLV::Header, 13);
          FLV_out.DTSCMetaInit(Strm);
          std::cout.write(FLV_out.data, FLV_out.len);
          if (Strm.metadata.getContentP("video") && Strm.metadata.getContentP("video")->getContentP("init")){
            FLV_out.DTSCVideoInit(Strm);
            std::cout.write(FLV_out.data, FLV_out.len);
          }
          if (Strm.metadata.getContentP("audio") && Strm.metadata.getContentP("audio")->getContentP("init")){
            FLV_out.DTSCAudioInit(Strm);
            std::cout.write(FLV_out.data, FLV_out.len);
          }
        }
        if (FLV_out.DTSCLoader(Strm)){
          std::cout.write(FLV_out.data, FLV_out.len);
        }
      }
    }

    std::cerr << "Done!" << std::endl;
    
    return 0;
  }//FLV2DTSC

};//Converter namespace

/// Entry point for DTSC2FLV, simply calls Converters::DTSC2FLV().
int main(){
  return Converters::DTSC2FLV();
}//main
