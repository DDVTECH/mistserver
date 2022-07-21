#include "analyser_riff.h"
#include <iostream>
#include <mist/riff.h>
const int chunkSize = 8192;

void AnalyserRIFF::init(Util::Config &conf){
  Analyser::init(conf);
}

AnalyserRIFF::AnalyserRIFF(Util::Config &conf) : Analyser(conf){
  curPos = prePos = 0;
}

bool AnalyserRIFF::parsePacket(){
  prePos = curPos;

  while (dataBuffer.size() < neededBytes()) {
    uint64_t needed = neededBytes();
    uint64_t required = 0;
    dataBuffer.reserve(needed);

    while (dataBuffer.size() < needed ) {
      if (uri.isEOF()) {
        FAIL_MSG("End of file");
        return false;
      }
      required = needed - dataBuffer.size() - buffer.bytes(0xffffffff);
      if(required > chunkSize){
        uri.readSome(chunkSize, *this);
      }else{
        uri.readSome(required, *this);
      }

      dataBuffer.append(buffer.remove(buffer.bytes(0xffffffff)));
    }

    uint64_t appending = needed - dataBuffer.size();
    curPos += appending; 
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

void AnalyserRIFF::dataCallback(const char *ptr, size_t size) {
  buffer.append(ptr, size);
}
