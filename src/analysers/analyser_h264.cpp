/// \file analyser_h264.cpp
/// Reads H264 data and prints it in human-readable format.

#include "analyser_h264.h"
#include <mist/bitfields.h>
#include <mist/bitstream.h>
#include <mist/h264.h>

void AnalyserH264::init(Util::Config &conf){
  Analyser::init(conf);
  JSON::Value opt;
  opt["long"] = "size-prepended";
  opt["short"] = "S";
  opt["help"] = "Parse size-prepended style instead of Annex B style";
  conf.addOption("size-prepended", opt);
  opt.null();
}

AnalyserH264::AnalyserH264(Util::Config &conf) : Analyser(conf){
  curPos = prePos = 0;
  sizePrepended = conf.getBool("size-prepended");
}

bool AnalyserH264::parsePacket(){
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

  size_t size = 0;
  h264::nalUnit *nalPtr =
      h264::nalFactory(dataBuffer.data(), dataBuffer.size(), size, !sizePrepended);
  if (!nalPtr){
    FAIL_MSG("Could not read a NAL unit at position %llu", prePos);
    return false;
  }
  HIGH_MSG("Read a %lu-byte NAL unit at position %llu", size, prePos);
  dataBuffer.erase(0, size); // erase the NAL unit we just read
  if (detail >= 2){nalPtr->toPrettyString(std::cout);}
  ///\TODO update mediaTime with current timestamp
  return true;
}

uint64_t AnalyserH264::neededBytes(){
  // We buffer a megabyte if AnnexB
  if (!sizePrepended){return 1024 * 1024;}
  // otherwise, buffer the exact size needed
  if (dataBuffer.size() < 4){return 4;}
  return Bit::btohl(dataBuffer.data())+4;
}

