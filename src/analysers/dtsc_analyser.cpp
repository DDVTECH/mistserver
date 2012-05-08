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
#include "../../lib/dtsc.h" //DTSC support

/// Reads DTSC from stdin and outputs human-readable information to stderr.
int main() {
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
