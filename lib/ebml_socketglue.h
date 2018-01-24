#include "ebml.h"
#include "socket.h"
#include "bitfields.h"
#include "dtsc.h"

namespace EBML{
  static void sendUniInt(Socket::Connection &C, const uint64_t val);
  void sendElemHead(Socket::Connection &C, uint32_t ID, const uint64_t size);
  void sendElemUInt(Socket::Connection &C, uint32_t ID, const uint64_t val);
  void sendElemID(Socket::Connection &C, uint32_t ID, const uint64_t val);
  void sendElemDbl(Socket::Connection &C, uint32_t ID, const double val);
  void sendElemStr(Socket::Connection &C, uint32_t ID, const std::string &val);
  void sendElemEBML(Socket::Connection &C, const std::string &doctype);
  void sendElemInfo(Socket::Connection &C, const std::string &appName, double duration);
  uint32_t sizeElemEBML(const std::string &doctype);
  uint32_t sizeElemInfo(const std::string &appName, double duration);

  void sendElemSeek(Socket::Connection &C, uint32_t ID, uint64_t bytePos);
  uint32_t sizeElemSeek(uint32_t ID, uint64_t bytePos);
  void sendElemCuePoint(Socket::Connection &C, uint64_t time, uint64_t track, uint64_t clusterPos, uint64_t relaPos);
  uint32_t sizeElemCuePoint(uint64_t time, uint64_t track, uint64_t clusterPos, uint64_t relaPos);

  uint8_t sizeUInt(const uint64_t val);
  uint32_t sizeElemHead(uint32_t ID, const uint64_t size);
  uint32_t sizeElemUInt(uint32_t ID, const uint64_t val);
  uint32_t sizeElemID(uint32_t ID, const uint64_t val);
  uint32_t sizeElemDbl(uint32_t ID, const double val);
  uint32_t sizeElemStr(uint32_t ID, const std::string &val);

  void sendSimpleBlock(Socket::Connection &C, DTSC::Packet & pkt, uint64_t clusterTime, bool forceKeyframe = false);
  uint32_t sizeSimpleBlock(uint64_t trackId, uint32_t dataSize);
}

