#include "comms.h"
#include "dtsc.h"

namespace CMAF{
  size_t payloadSize(const DTSC::Meta &M, size_t track, uint64_t startTime, uint64_t endTime);
  std::string trackHeader(const DTSC::Meta & M, size_t trackIndex, bool simplifyTrackIds = false);
  size_t keyHeaderSize(const DTSC::Meta &M, size_t track, size_t fragment);
  size_t keyHeaderSize(const DTSC::Meta &M, size_t track, uint64_t startTime, uint64_t endTime);
  std::string keyHeader(const DTSC::Meta &M, size_t track, uint64_t startTime, uint64_t endTime, uint64_t segmentNum, bool simplifyTrackIds = false, bool UTCTime = false);

  bool header(Util::ResizeablePointer & headOut, const DTSC::Meta & M, const std::map<size_t, Comms::Users> & userSelect);

  bool fragmentHeader(Util::ResizeablePointer & headOut, const DTSC::Meta & M,
                      const std::map<size_t, Comms::Users> & userSelect, size_t fragmentIndex);
}// namespace CMAF
