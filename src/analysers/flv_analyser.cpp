/// \file DTSC_Analyser/main.cpp
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
#include "../../lib/flv_tag.h" //FLV support

/// Reads DTSC from stdin and outputs human-readable information to stderr.
int main() {

  FLV::Tag FLV_in; // Temporary storage for incoming FLV data.
  

  while (!feof(stdin)){
    if (FLV_in.FileLoader(stdin)){
      std::cout << "Tag: " << FLV_in.tagType() << std::endl;
      printf("%hhX %hhX %hhX %hhX %hhX %hhX %hhX %hhX %hhX %hhX\n", FLV_in.data[11], FLV_in.data[12], FLV_in.data[13], FLV_in.data[14], FLV_in.data[15], FLV_in.data[16], FLV_in.data[17], FLV_in.data[18], FLV_in.data[19], FLV_in.data[20]);
      printf("%hhX %hhX %hhX %hhX %hhX %hhX %hhX %hhX %hhX %hhX\n", FLV_in.data[FLV_in.len-10], FLV_in.data[FLV_in.len-9], FLV_in.data[FLV_in.len-8], FLV_in.data[FLV_in.len-7], FLV_in.data[FLV_in.len-6], FLV_in.data[FLV_in.len-5], FLV_in.data[FLV_in.len-4], FLV_in.data[FLV_in.len-3], FLV_in.data[FLV_in.len-2], FLV_in.data[FLV_in.len-1]);
      std::cout << std::endl;
    }
  }

      
  DTSC::Stream Strm;

  std::string inBuffer;
  char charBuffer[1024*10];
  unsigned int charCount;
  bool doneheader = false;

  while(std::cin.good()){
    //invalidate the current buffer
    std::cin.read(charBuffer, 1024*10);
    charCount = std::cin.gcount();
    inBuffer.append(charBuffer, charCount);
    if (Strm.parsePacket(inBuffer)){
      if (!doneheader){
        doneheader = true;
        Strm.metadata.Print();
      }
      Strm.getPacket().Print();
    }
  }
  return 0;
}
