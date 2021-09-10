#pragma once
#include "dtsc.h"
#include <cstdio>
#include <deque>
#include <string>

namespace nalu{
  struct nalData{
    uint8_t nalType;
    size_t nalSize;
  };

  std::deque<int> parseNalSizes(DTSC::Packet &pack);
  std::string removeEmulationPrevention(const std::string &data);

  unsigned long toAnnexB(const char *data, unsigned long dataSize, char *&result);
  unsigned long fromAnnexB(const char *data, unsigned long dataSize, char *&result);
  const char *scanAnnexB(const char *data, uint32_t dataSize);
  const char *nalEndPosition(const char *data, uint32_t dataSize);
}// namespace nalu
