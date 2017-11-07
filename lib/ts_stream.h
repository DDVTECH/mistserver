#include "adts.h"
#include "h265.h"
#include "ts_packet.h"
#include "tinythread.h"
#include <deque>
#include <map>
#include <set>

#include "shared_memory.h"
#define TS_PTS_ROLLOVER 95443718 

namespace TS{
  enum codecType{H264 = 0x1B, AAC = 0x0F, AC3 = 0x81, MP3 = 0x03, H265 = 0x24, ID3 = 0x15, MPEG2 = 0x02, MP2 = 0x03};

  class ADTSRemainder{
  private:
    char *data;
    uint32_t max;
    uint32_t now;
    uint32_t len;
    uint64_t bpos;

  public:
    void setRemainder(const aac::adts &p, const void *source, const uint32_t avail,
                      const uint64_t bPos);

    ADTSRemainder();
    ~ADTSRemainder();
    uint32_t getLength();
    uint64_t getBpos();
    uint32_t getTodo();
    char *getData();

    void append(const char *p, uint32_t pLen);
    bool isComplete();
    void clear();
  };

  class Stream{
  public:
    Stream(bool _threaded = false);
    ~Stream();
    void add(char *newPack, unsigned long long bytePos = 0);
    void add(Packet &newPack, unsigned long long bytePos = 0);
    void parse(Packet &newPack, unsigned long long bytePos);
    void parse(char *newPack, unsigned long long bytePos);
    void parse(unsigned long tid);
    void parseNal(uint32_t tid, const char *pesPayload, const char *packetPtr, bool &isKeyFrame);
    bool hasPacketOnEachTrack() const;
    bool hasPacket(unsigned long tid) const;
    bool hasPacket() const;
    void getPacket(unsigned long tid, DTSC::Packet &pack);
    void getEarliestPacket(DTSC::Packet &pack);
    void initializeMetadata(DTSC::Meta &meta, unsigned long tid = 0, unsigned long mappingId = 0);
    void partialClear();
    void clear();
    void finish();
    void eraseTrack(unsigned long tid);
    bool isDataTrack(unsigned long tid);
    void parseBitstream(uint32_t tid, const char *pesPayload, uint32_t realPayloadSize,
                        uint64_t timeStamp, int64_t timeOffset, uint64_t bPos, bool alignment);
    std::set<unsigned long> getActiveTracks();

    void setLastms(unsigned long tid, uint64_t timestamp);
  private:
    unsigned long long lastPAT;
    ProgramAssociationTable associationTable;
    std::map<unsigned long, ADTSRemainder> remainders;

    std::map<unsigned long, unsigned long long> lastPMT;
    std::map<unsigned long, ProgramMappingTable> mappingTable;

    std::map<unsigned long, std::deque<Packet> > pesStreams;
    std::map<unsigned long, std::deque<unsigned long long> > pesPositions;
    std::map<unsigned long, std::deque<DTSC::Packet> > outPackets;
    std::map<unsigned long, DTSC::Packet> buildPacket;
    std::map<unsigned long, unsigned long> pidToCodec;
    std::map<unsigned long, aac::adts> adtsInfo;
    std::map<unsigned long, std::string> spsInfo;
    std::map<unsigned long, std::string> ppsInfo;
    std::map<unsigned long, h265::initData> hevcInfo;
    std::map<unsigned long, std::string> metaInit;
    std::map<unsigned long, std::string> descriptors;
    std::map<unsigned long, uint32_t> seenUnitStart;
    std::map<unsigned long, std::string> mpeg2SeqHdr;
    std::map<unsigned long, std::string> mpeg2SeqExt;
    std::map<unsigned long, std::string> mp2Hdr;

    std::map<unsigned long, size_t> rolloverCount;
    std::map<unsigned long, unsigned long long> lastms;

    mutable tthread::recursive_mutex tMutex;

    bool threaded;

    std::set<unsigned long> pmtTracks;

    void parsePES(unsigned long tid, bool finished = false);
  };
}

