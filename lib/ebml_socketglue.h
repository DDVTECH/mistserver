#pragma once
#include "bitfields.h"
#include "dtsc.h"
#include "ebml.h"
#include "socket.h"

#define PKT_COUNT 24

namespace EBML{
  void sendUniInt(Socket::Connection &C, const uint64_t val);
  void sendElemHead(Socket::Connection &C, uint32_t ID, const uint64_t size);
  void sendElemUInt(Socket::Connection &C, uint32_t ID, const uint64_t val);
  void sendElemID(Socket::Connection &C, uint32_t ID, const uint64_t val);
  void sendElemDbl(Socket::Connection &C, uint32_t ID, const double val);
  void sendElemStr(Socket::Connection &C, uint32_t ID, const std::string &val);
  void sendElemEBML(Socket::Connection &C, const std::string &doctype);
  void sendElemInfo(Socket::Connection &C, const std::string &appName, double duration, int64_t date = 0);
  uint32_t sizeElemEBML(const std::string &doctype);
  uint32_t sizeElemInfo(const std::string &appName, double duration, int64_t date = 0);

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

  void sendSimpleBlock(Socket::Connection & C, const char *dataPointer, const size_t dataLen, size_t trackId,
                       uint64_t time, bool keyFrame, uint64_t clusterTime);
  uint32_t sizeSimpleBlock(uint64_t trackId, uint32_t dataSize);

  bool parseTrackEntry(const Element & E, DTSC::Meta & meta);

  class packetData {
    public:
      uint64_t time, offset, track, dsize, bpos;
      bool key;
      Util::ResizeablePointer ptr;
      packetData() : time(0), offset(0), track(0), dsize(0), bpos(0), key(false) {}
      void set(uint64_t packTime, uint64_t packOffset, uint64_t packTrack, uint64_t packDataSize, uint64_t packBytePos,
               bool isKeyframe, void *dataPtr = 0) {
        time = packTime;
        offset = packOffset;
        track = packTrack;
        dsize = packDataSize;
        bpos = packBytePos;
        key = isKeyframe;
        if (dataPtr) { ptr.assign(dataPtr, packDataSize); }
      }
      packetData(uint64_t packTime, uint64_t packOffset, uint64_t packTrack, uint64_t packDataSize,
                 uint64_t packBytePos, bool isKeyframe, void *dataPtr = 0) {
        set(packTime, packOffset, packTrack, packDataSize, packBytePos, isKeyframe, dataPtr);
      }
  };

  class trackPredictor {
    public:
      packetData pkts[PKT_COUNT]; /// Buffer for packet data
      uint64_t times[PKT_COUNT]; /// Sorted timestamps of buffered packets
      size_t maxDelay{0}; /// Maximum amount of bframes we expect
      uint32_t timeOffset{0}; /// Milliseconds we need to subtract from times so that offsets are always > 0
      uint64_t ctr{0}; /// ingested frame count
      uint64_t rem{0}; /// removed frame count
      uint64_t finished{INVALID_TRACK_ID};
      bool initialized{false};
      bool hasPackets();
      void finish();
      void flush();
      packetData & getPacketData(bool mustCalcOffsets);

      void add(uint64_t packTime, uint64_t packTrack, uint64_t packDataSize, uint64_t packBytePos, bool isKeyframe,
               bool isVideo, void *dataPtr = 0);
      void remove();
  };

  class toDTSC {
    public:
      void enableData(bool enable);
      void parseElement(const Element & E, const uint64_t bpos, DTSC::Meta & meta);
      void parseBlock(const Block & B, const DTSC::Meta & M);
      void finish();
      void flush();
      void postHeader(DTSC::Meta & meta);
      bool hasPackets();
      bool fillPacket(const DTSC::Meta & M, size_t & thisIdx, uint64_t & thisTime, DTSC::Packet & thisPacket);
      void fillPacketData(DTSC::Meta & meta);

      uint64_t lastClusterBPos{0};
      uint64_t lastClusterTime{0};
      uint64_t bufferedPacks{0};
      std::map<uint64_t, EBML::trackPredictor> packBuf;
      std::set<uint64_t> swapEndianness;

    private:
      bool withData{true};
      double timeScale{0};
      int64_t dateVal{0};
  };

}// namespace EBML
