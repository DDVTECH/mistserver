#include "analyser_riff.h"
#include <iostream>
#include <mist/riff.h>

void AnalyserRIFF::init(Util::Config &conf){
  Analyser::init(conf);
}

AnalyserRIFF::AnalyserRIFF(Util::Config &conf) : Analyser(conf){
  curPos = prePos = 0;
}

bool AnalyserRIFF::parsePacket(){
  prePos = curPos;
  // Read in smart bursts until we have enough data
  while (isOpen() && dataBuffer.size() < neededBytes()){
    uint64_t needed = neededBytes();
    dataBuffer.reserve(needed);
    for (uint64_t i = dataBuffer.size(); i < needed; ++i){
      dataBuffer += std::cin.get();
      ++curPos;
      if (!std::cin.good()){dataBuffer.erase(dataBuffer.size() - 1, 1);}
    }
  }

  if (dataBuffer.size() < 8){return false;}

  RIFF::Chunk C(dataBuffer.data(), dataBuffer.size());
  INFO_MSG("Read a chunk at position %d", prePos);
  if (detail >= 2){C.toPrettyString(std::cout);}
  ///\TODO update mediaTime with the current timestamp
  if (C){
    dataBuffer.erase(0, C.getPayloadSize()+8);
    return true;
  }
  return false;
}

/// Calculates how many bytes we need to read a whole box.
uint64_t AnalyserRIFF::neededBytes(){
  if (dataBuffer.size() < 8){return 8;}
  return RIFF::Chunk(dataBuffer.data()).getPayloadSize()+8;
}

