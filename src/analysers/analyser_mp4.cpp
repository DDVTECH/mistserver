#include "analyser_mp4.h"

void AnalyserMP4::init(Util::Config &conf){
  Analyser::init(conf);
}

AnalyserMP4::AnalyserMP4(Util::Config &conf) : Analyser(conf){
  curPos = prePos = 0;
}

bool AnalyserMP4::parsePacket(){
  prePos = curPos;
  // Read in smart bursts until we have enough data
  while (isOpen() && mp4Buffer.size() < neededBytes()){
    uint64_t needed = neededBytes();
    mp4Buffer.reserve(needed);
    for (uint64_t i = mp4Buffer.size(); i < needed; ++i){
      mp4Buffer += std::cin.get();
      ++curPos;
      if (!std::cin.good()){mp4Buffer.erase(mp4Buffer.size() - 1, 1);}
    }
  }

  if (mp4Data.read(mp4Buffer)){
    INFO_MSG("Read a box at position %d", prePos);
    if (detail >= 2){std::cout << mp4Data.toPrettyString(0) << std::endl;}
    ///\TODO update mediaTime with the current timestamp
    return true;
  }
  FAIL_MSG("Could not read box at position %llu", prePos);
  return false;
}

/// Calculates how many bytes we need to read a whole box.
uint64_t AnalyserMP4::neededBytes(){
  if (mp4Buffer.size() < 4){return 4;}
  uint64_t size = ntohl(((int *)mp4Buffer.data())[0]);
  if (size != 1){return size;}
  if (mp4Buffer.size() < 16){return 16;}
  size = 0 + ntohl(((int *)mp4Buffer.data())[2]);
  size <<= 32;
  size += ntohl(((int *)mp4Buffer.data())[3]);
  return size;
}

