#pragma once
#include "dtsc.h"
#include "h264.h"
#include "h265.h"
#include "json.h"
#include "mp4.h"
#include "mp4_generic.h"
#include "socket.h"
#include "util.h"
#include <algorithm>
#include <cstdio>
#include <deque>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdint.h>
#include <string>
#include <vector>

namespace SDP{
  class Track;
};

/// This namespace holds all RTP-parsing and sending related functionality.
namespace RTP{

  extern uint32_t MAX_SEND;
  extern unsigned int PACKET_REORDER_WAIT;
  extern unsigned int PACKET_DROP_TIMEOUT;

    struct FecData{
    public:
      uint16_t sequence;
      uint32_t timestamp;
      uint8_t *bitstring;
  };

  struct FEC{
    public:
      bool needsInit;
      bool isFirst;
      uint16_t maxIndex;
      // Track the amount of row/column FEC packets were sent, as they have their own index
      uint16_t columnSN;
      uint16_t rowSN;
      // Determines what row/column of FEC data we are currently on
      uint8_t rows;
      uint8_t columns;
      uint16_t index;
      uint16_t lengthRecovery;
      uint32_t pktSize;
      uint32_t rtpBufSize;
      uint32_t bitstringSize;
      FecData fecBufferRows; // Stores intermediate results or XOR'd RTP packets
      std::map<uint8_t, FecData> fecBufferColumns;
  };

  /// This class is used to make RTP packets. Currently, H264, and AAC are supported. RTP
  /// mechanisms, like increasing sequence numbers and setting timestamps are all taken care of in
  /// here.
  class Packet{
  private:
    bool managed;
    char *data;          ///< The actual RTP packet that is being sent
    uint32_t maxDataLen; ///< Amount of reserved bytes for the packet(s)
    uint32_t sentPackets;
    uint32_t sentBytes; // Because ugly is beautiful
    bool fecEnabled;
    FEC fecContext;
  public:
    static double startRTCP;
    uint32_t getHsize() const;
    uint32_t getPayloadSize() const;
    char *getPayload() const;
    uint32_t getVersion() const;
    uint32_t getPadding() const;
    uint32_t getExtension() const;
    uint32_t getContribCount() const;
    uint32_t getMarker() const;
    uint32_t getPayloadType() const;
    uint16_t getSequence() const;
    uint32_t getTimeStamp() const;
    void setSequence(uint16_t seq);
    uint32_t getSSRC() const;
    void setSSRC(uint32_t ssrc);

    void setTimestamp(uint32_t t);
    void increaseSequence();
    void initFEC(uint64_t bufSize);
    void applyXOR(const uint8_t *in1, const uint8_t *in2, uint8_t *out, uint64_t size);
    void generateBitstring(const char *payload, unsigned int payloadlen, uint8_t *bitstring);
    bool configureFEC(uint8_t rows, uint8_t columns);
    void sendFec(void *socket, FecData *fecData, bool isColumn);
    void parseFEC(void *columnSocket, void *rowSocket, uint64_t & bytesSent, const char *payload, unsigned int payloadlen);
    void sendNoPacket(unsigned int payloadlen);
    void sendTS(void *socket, const char *payload, unsigned int payloadlen);
    void sendH264(void *socket, void callBack(void *, const char *, size_t, uint8_t), const char *payload,
                  unsigned int payloadlen, unsigned int channel, bool lastOfAccessUnit);
    void sendVP8(void *socket, void callBack(void *, const char *, size_t, uint8_t),
                 const char *payload, unsigned int payloadlen, unsigned int channel);
    void sendH265(void *socket, void callBack(void *, const char *, size_t, uint8_t),
                  const char *payload, unsigned int payloadlen, unsigned int channel);
    void sendMPEG2(void *socket, void callBack(void *, const char *, size_t, uint8_t),
                   const char *payload, unsigned int payloadlen, unsigned int channel);
    void sendData(void *socket, void callBack(void *, const char *, size_t, uint8_t), const char *payload,
                  unsigned int payloadlen, unsigned int channel, std::string codec);
    void sendRTCP_SR(void *socket, uint8_t channel, void callBack(void *, const char *, size_t, uint8_t));
    void sendRTCP_RR(SDP::Track &sTrk, void callBack(void *, const char *, size_t, uint8_t));

    Packet();
    Packet(uint32_t pt, uint32_t seq, uint64_t ts, uint32_t ssr, uint32_t csrcCount = 0);
    Packet(const Packet &o);
    void operator=(const Packet &o);
    ~Packet();
    Packet(const char *dat, uint64_t len);
    const char *getData();
    char *ptr() const{return data;}
    std::string toString() const;
  };

  /// Sorts RTP packets, outputting them through a callback in correct order.
  /// Also keeps track of statistics, which it expects to be read/reset externally (for now).
  /// Optionally can be inherited from with the outPacket function overridden to not use a callback.
  class Sorter{
  public:
    Sorter(uint64_t trackId = 0, void (*callback)(const uint64_t track, const Packet &p) = 0);
    void addPacket(const char *dat, unsigned int len);
    void addPacket(const Packet &pack);
    // By default, calls the callback function, if set.
    virtual void outPacket(const uint64_t track, const Packet &p){
      if (callback){callback(track, p);}
    }
    void setCallback(uint64_t track, void (*callback)(const uint64_t track, const Packet &p));
    uint16_t rtpSeq;
    uint16_t rtpWSeq;
    bool first;
    bool preBuffer;
    int32_t lostTotal, lostCurrent;
    uint32_t packTotal, packCurrent;
    std::set<uint16_t> wantedSeqs;
    uint32_t lastNTP; ///< Middle 32 bits of last Sender Report NTP timestamp
    uint64_t lastBootMS; ///< bootMS time of last Sender Report
  private:
    uint64_t packTrack;
    std::map<uint16_t, Packet> packBuffer;
    std::map<uint16_t, Packet> packetHistory;
    void (*callback)(const uint64_t track, const Packet &p);
  };

  class MPEGVideoHeader{
  public:
    MPEGVideoHeader(char *d);
    void clear();
    uint16_t getTotalLen() const;
    std::string toString() const;
    void setTempRef(uint16_t ref);
    void setPictureType(uint8_t pType);
    void setSequence();
    void setBegin();
    void setEnd();

  private:
    char *data;
  };

  /// Converts (sorted) RTP packets into DTSC packets.
  /// Outputs DTSC packets through a callback function or overridden virtual function.
  /// Updates init data through a callback function or overridden virtual function.
  class toDTSC{
  public:
    toDTSC();
    virtual ~toDTSC(){}
    void setProperties(const uint64_t track, const std::string &codec, const std::string &type,
                       const std::string &init, const double multiplier);
    void setProperties(const DTSC::Meta &M, size_t tid);
    void setCallbacks(void (*cbPack)(const DTSC::Packet &pkt),
                      void (*cbInit)(const uint64_t track, const std::string &initData));
    void addRTP(const RTP::Packet &rPkt);
    void timeSync(uint32_t rtpTime, int64_t msDiff);
    virtual void outPacket(const DTSC::Packet &pkt){
      if (cbPack){cbPack(pkt);}
    }
    virtual void outInit(const uint64_t track, const std::string &initData){
      if (cbInit){cbInit(track, initData);}
    }

  public:
    uint64_t trackId;
    double multiplier;    ///< Multiplier to convert from millis to RTP time
    std::string codec;    ///< Codec of this track
    std::string type;     ///< Type of this track
    std::string init;     ///< Init data of this track
    unsigned int lastSeq; ///< Last sequence number seen
    uint64_t packCount;   ///< Amount of DTSC packets outputted, for H264/HEVC
    double fps;           ///< Framerate, for H264, HEVC
    uint32_t wrapArounds; ///< Counter for RTP timestamp wrapArounds
    bool recentWrap;      ///< True if a wraparound happened recently.
    uint32_t prevTime;
    uint64_t firstTime;
    int64_t milliSync;
    void (*cbPack)(const DTSC::Packet &pkt);
    void (*cbInit)(const uint64_t track, const std::string &initData);
    // Codec-specific handlers
    void handleAAC(uint64_t msTime, char *pl, uint32_t plSize);
    void handleMP2(uint64_t msTime, char *pl, uint32_t plSize);
    void handleMPEG2(uint64_t msTime, char *pl, uint32_t plSize);
    void handleHEVC(uint64_t msTime, char *pl, uint32_t plSize, bool missed);
    void handleHEVCSingle(uint64_t ts, const char *buffer, const uint32_t len, bool isKey);
    h265::initData hevcInfo;            ///< For HEVC init parsing
    Util::ResizeablePointer fuaBuffer;  ///< For H264/HEVC FU-A packets
    Util::ResizeablePointer packBuffer; ///< For H264/HEVC regular and STAP packets
    uint64_t currH264Time;//Time of the DTSC packet currently being built (pre-conversion)
    Util::ResizeablePointer h264OutBuffer; ///< For collecting multiple timestamps into one packet
    bool h264BufferWasKey;
    void handleH264(uint64_t msTime, char *pl, uint32_t plSize, bool missed, bool hasPadding);
    void handleH264Single(uint64_t ts, const char *buffer, const uint32_t len, bool isKey);
    void handleH264Multi(uint64_t ts, char *buffer, const uint32_t len);
    std::string spsData; ///< SPS for H264
    uint8_t curPicParameterSetId;
    std::map<uint8_t,std::string> ppsData; ///< PPS for H264
    void handleVP8(uint64_t msTime, const char *buffer, const uint32_t len, bool missed, bool hasPadding);
    Util::ResizeablePointer vp8FrameBuffer; ///< Stores successive VP8 payload data. We always start with the first
                                            ///< partition; but we might be missing other partitions when they were
                                            ///< lost. (a partition is basically what's called a slice in H264).
    bool vp8BufferHasKeyframe;
  };
}// namespace RTP
