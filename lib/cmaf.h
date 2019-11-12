#include "dtsc.h"
#include "mp4_dash.h"
#include "mp4_generic.h"
#include <set>

namespace CMAF{
  size_t payloadSize(const DTSC::Meta &M, size_t track, size_t index, bool isKeyIndex = false);
  size_t trackHeaderSize(const DTSC::Meta &M, size_t track);
  std::string trackHeader(const DTSC::Meta &M, size_t track, bool simplifyTrackIds = false);
  size_t fragmentHeaderSize(const DTSC::Meta &M, size_t track, size_t fragment);
  std::string fragmentHeader(const DTSC::Meta &M, size_t track, size_t fragment, bool simplifyTrackIds = false, bool UTCTime = false);
  size_t keyHeaderSize(const DTSC::Meta &M, size_t track, size_t key);
  std::string keyHeader(const DTSC::Meta &M, size_t track, size_t key, bool simplifyTrackIds = false, bool UTCTime = false);
}// namespace CMAF
