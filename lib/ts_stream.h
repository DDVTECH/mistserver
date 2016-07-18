#include "ts_packet.h"
#include "adts.h"
#include <map>
#include <set>
#include <deque>
#include "h265.h"

#include "shared_memory.h"

namespace TS {
  enum codecType {
    H264 = 0x1B,
    AAC = 0x0F,
    AC3 = 0x81,
    MP3 = 0x03,
    H265 = 0x24,
    ID3 = 0x15
  };

  class Stream{
    public:
      Stream(bool _threaded = false);
      ~Stream();
      void add(char * newPack, unsigned long long bytePos = 0);
      void add(Packet & newPack, unsigned long long bytePos = 0);
      void parse(Packet & newPack, unsigned long long bytePos);
      void parse(char * newPack, unsigned long long bytePos);
      void parse(unsigned long tid);
      bool hasPacketOnEachTrack() const;
      bool hasPacket(unsigned long tid) const;
      void getPacket(unsigned long tid, DTSC::Packet & pack);
      void getEarliestPacket(DTSC::Packet & pack);
      void initializeMetadata(DTSC::Meta & meta, unsigned long tid = 0);
      void clear();
      void eraseTrack(unsigned long tid);
      bool isDataTrack(unsigned long tid);
      std::set<unsigned long> getActiveTracks();
    private:
      unsigned long long lastPAT;
      ProgramAssociationTable associationTable;

      std::map<unsigned long, unsigned long long> lastPMT;
      std::map<unsigned long, ProgramMappingTable> mappingTable;

      std::map<unsigned long, std::deque<Packet> > pesStreams;
      std::map<unsigned long, std::deque<unsigned long long> > pesPositions;
      std::map<unsigned long, std::deque<DTSC::Packet> > outPackets;
      std::map<unsigned long, unsigned long> pidToCodec;
      std::map<unsigned long, aac::adts > adtsInfo;
      std::map<unsigned long, std::string > spsInfo;
      std::map<unsigned long, std::string > ppsInfo;
      std::map<unsigned long, h265::initData > hevcInfo;
      std::map<unsigned long, std::string> metaInit;
      std::map<unsigned long, std::string> descriptors;

      mutable IPC::semaphore globalSem;

      bool threaded;

      std::set<unsigned long> pmtTracks;

      void parsePES(unsigned long tid);
  };
}
