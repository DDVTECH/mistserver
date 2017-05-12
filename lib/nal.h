#pragma once
#include <deque>
#include <string>
#include <cstdio>
#include <deque>
#include "dtsc.h"

namespace nalu {
  struct nalData {
    unsigned char nalType;
    unsigned long nalSize;
  };

  std::deque<int> parseNalSizes(DTSC::Packet & pack);
  std::string removeEmulationPrevention(const std::string & data);

  unsigned long toAnnexB(const char * data, unsigned long dataSize, char *& result);
  unsigned long fromAnnexB(const char * data, unsigned long dataSize, char *& result);
  void scanAnnexB(const char * data, uint32_t dataSize, const char *& packetPointer);
  const char* nalEndPosition(const char * data, uint32_t dataSize);
}
