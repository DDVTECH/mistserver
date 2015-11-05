#include "ts_packet.h"
#include "adts.h"
#include <map>
#include <deque>
#include "h265.h"

namespace TS {
  enum codecType {
    H264 = 0x1B,
    AAC = 0x0F,
    AC3 = 0x81,
    MP3 = 0x03,
    HEVC = 0x06,
    H265 = 0x24
  };

  class Stream{
    public:
      void parse(Packet & newPack, unsigned long long bytePos);
      void parse(char * newPack, unsigned long long bytePos);
      bool hasPacketOnEachTrack() const;
      bool hasPacket(unsigned long tid) const;
      void getPacket(unsigned long tid, DTSC::Packet & pack);
      void getEarliestPacket(DTSC::Packet & pack);
      void initializeMetadata(DTSC::Meta & meta);
      void clear();
    private:
      ProgramAssociationTable associationTable;
      std::map<unsigned long, ProgramMappingTable> mappingTable;
      std::map<unsigned long, std::deque<Packet> > pesStreams;
      std::map<unsigned long, std::deque<unsigned long long> > pesPositions;
      std::map<unsigned long, unsigned long> payloadSize;
      std::map<unsigned long, std::deque<DTSC::Packet> > outPackets;
      std::map<unsigned long, unsigned long> pidToCodec;
      std::map<unsigned long, aac::adts > adtsInfo;
      std::map<unsigned long, std::string > spsInfo;
      std::map<unsigned long, std::string > ppsInfo;
      std::map<unsigned long, h265::initData > hevcInfo;


      std::set<unsigned long> pmtTracks;

      void parsePES(unsigned long tid);
  };
}
