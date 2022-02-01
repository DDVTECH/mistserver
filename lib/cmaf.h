#include "dtsc.h"
#include "mp4_dash.h"
#include "mp4_generic.h"
#include <set>

namespace CMAF{
  size_t payloadSize(const DTSC::Meta &M, size_t track, uint64_t startTime, uint64_t endTime);
  std::string trackHeader(const DTSC::Meta &M, size_t track, bool simplifyTrackIds = false);
  size_t keyHeaderSize(const DTSC::Meta &M, size_t track, size_t fragment);
  size_t keyHeaderSize(const DTSC::Meta &M, size_t track, uint64_t startTime, uint64_t endTime);
  std::string keyHeader(const DTSC::Meta &M, size_t track, uint64_t startTime, uint64_t endTime, uint64_t segmentNum, bool simplifyTrackIds = false, bool UTCTime = false);
}// namespace CMAF
