/// \file analyser_av1.cpp
/// Reads AV1 data OBUs and prints it in human-readable format.

#include "analyser_av1.h"

#include <mist/av1.h>

#include <iostream>

void AnalyserAV1::init(Util::Config & conf) {
  Analyser::init(conf);
  JSON::Value opt;
  opt["long"] = "size-prepended";
  opt["short"] = "S";
  opt["help"] = "Parse size-prepended style instead of low overhead bitstream style";
  conf.addOption("size-prepended", opt);
  opt.null();
}

AnalyserAV1::AnalyserAV1(Util::Config & conf) : Analyser(conf) {
  curPos = prePos = 0;
  sizePrepended = conf.getBool("size-prepended");
}

bool AnalyserAV1::parsePacket() {
  // Read in smart bursts until we have enough data
  while (isOpen() && dataBuffer.size() < neededBytes()) {
    uint64_t needed = neededBytes();
    dataBuffer.reserve(needed);
    for (uint64_t i = dataBuffer.size(); i < needed; ++i) {
      dataBuffer += std::cin.get();
      ++curPos;
      if (!std::cin.good()) { dataBuffer.erase(dataBuffer.size() - 1, 1); }
    }
  }

  AV1::OBU obu(dataBuffer.data(), dataBuffer.size());
  HIGH_MSG("Read a %zu-byte OBU unit at position %" PRIu64, obu.getSize(), prePos);
  if (detail >= 2) { std::cout << obu.toString() << std::endl; }
  prePos += obu.getSize();
  dataBuffer.erase(0, obu.getSize()); // erase the NAL unit we just read
  return true;
}

uint64_t AnalyserAV1::neededBytes() {
  if (dataBuffer.size() < 2) { return 2; }
  AV1::OBU obu(dataBuffer.data(), dataBuffer.size());
  if (!obu) { return dataBuffer.size() + 1; }
  return obu.getSize();
}
