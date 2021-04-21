#include "dtsc.h"
#include "mp4_dash.h"
#include "mp4_generic.h"
#include <set>

namespace CMAF{
  size_t payloadSize(const DTSC::Meta &M, size_t track, size_t fragment);
  size_t trackHeaderSize(const DTSC::Meta &M, size_t track);
  std::string trackHeader(const DTSC::Meta &M, size_t track);
  size_t fragmentHeaderSize(const DTSC::Meta &M, size_t track, size_t fragment);
  std::string fragmentHeader(const DTSC::Meta &M, size_t track, size_t fragment);
}// namespace CMAF
