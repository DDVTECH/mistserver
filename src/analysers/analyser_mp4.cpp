#include "analyser_mp4.h"
#include <mist/bitfields.h>

void AnalyserMP4::init(Util::Config &conf){
  Analyser::init(conf);
}

AnalyserMP4::AnalyserMP4(Util::Config &conf) : Analyser(conf){
  curPos = prePos = 0;
  buffer.splitter.clear();
}

bool AnalyserMP4::parsePacket(){
  prePos = curPos;
  // Read in smart bursts until we have enough data

  uint64_t bytesNeeded = neededBytes();
  while (mp4Buffer.size() < (bytesNeeded = (neededBytes()))) {
    bytesNeeded = neededBytes();
    mp4Buffer.reserve(bytesNeeded);

    while (!buffer.available(bytesNeeded)) {
      if (buffer.available(bytesNeeded - mp4Buffer.length())) {
        mp4Buffer += buffer.remove(bytesNeeded - mp4Buffer.length());
        break;
      }

      if (uri.isEOF()) {
       if(mp4Buffer.size() <= 0){
          FAIL_MSG("End of file");
          return false;
        }
      }else{
//        FAIL_MSG("readsome, %llu", bytesNeeded-buffer.bytes(bytesNeeded));
        uri.readSome(bytesNeeded - buffer.bytes(bytesNeeded), *this);
      }
    }

    if (!uri.isEOF()) {
      mp4Buffer += buffer.remove(bytesNeeded);
    }
  }

  curPos += bytesNeeded;

  if (mp4Data.read(mp4Buffer)){
    DONTEVEN_MSG("Read a box at position %" PRIu64, prePos);
    if(!validate){ 
      if (detail >= 2){std::cout << mp4Data.toPrettyString(0) << std::endl;}
    }

    ///\TODO update mediaTime with the current timestamp
    return true;
  }
  FAIL_MSG("Could not read box at position %" PRIu64, prePos);
  return false;
}


/*
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
*/

/// Calculates how many bytes we need to read a whole box.
uint64_t AnalyserMP4::neededBytes(){
  
  if (mp4Buffer.size() < 4){return 4;}

  uint64_t size = Bit::btohl(mp4Buffer.data());

  if (size != 1){
    return size;
  }

  if (mp4Buffer.size() < 16){
    return 16;
  }
  
  size = Bit::btohll(mp4Buffer.data() + 8);

  return size;
}

void AnalyserMP4::dataCallback(const char *ptr, size_t size) {
//  WARN_MSG("callback# buffersize: %u, needebytes: %lu, adding: %lu", buffer.bytes(0xffffffff),neededBytes(), size);
  buffer.append(ptr, size);
}
