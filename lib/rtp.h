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

  extern unsigned int MAX_SEND;

  /// This class is used to make RTP packets. Currently, H264, and AAC are supported. RTP
  /// mechanisms, like increasing sequence numbers and setting timestamps are all taken care of in
  /// here.
  class Packet{
  private:
    bool managed;
    char *data;           ///< The actual RTP packet that is being sent
    uint32_t maxDataLen;  ///< Amount of reserved bytes for the packet(s)
    int sentPackets;
    int sentBytes; // Because ugly is beautiful
  public:
    static double startRTCP;
    unsigned int getHsize() const;
    unsigned int getPayloadSize() const;
    char *getPayload() const;
    unsigned int getVersion() const;
    unsigned int getPadding() const;
    unsigned int getExtension() const;
    unsigned int getContribCount() const;
    unsigned int getMarker() const;
    unsigned int getPayloadType() const;
    unsigned int getSequence() const;
    uint32_t getTimeStamp() const;
    void setSequence(unsigned int seq);
    unsigned int getSSRC() const;
    void setSSRC(unsigned long ssrc);

    void setTimestamp(uint32_t t);
    void increaseSequence();
    void sendH264(void *socket, void callBack(void *, char *, unsigned int, unsigned int),
                  const char *payload, unsigned int payloadlen, unsigned int channel, bool lastOfAccesUnit);
    void sendVP8(void *socket, void callBack(void *, char *, unsigned int, unsigned int),
                 const char *payload, unsigned int payloadlen, unsigned int channel);
    void sendH265(void *socket, void callBack(void *, char *, unsigned int, unsigned int),
                  const char *payload, unsigned int payloadlen, unsigned int channel);
    void sendMPEG2(void *socket, void callBack(void *, char *, unsigned int, unsigned int),
                   const char *payload, unsigned int payloadlen, unsigned int channel);
    void sendData(void *socket, void callBack(void *, char *, unsigned int, unsigned int),
                  const char *payload, unsigned int payloadlen, unsigned int channel,
                  std::string codec);
    void sendRTCP_SR(long long &connectedAt, void *socket, unsigned int tid, DTSC::Meta &metadata,
                     void callBack(void *, char *, unsigned int, unsigned int));
    void sendRTCP_RR(long long &connectedAt, SDP::Track &sTrk, unsigned int tid,
                     DTSC::Meta &metadata,
                     void callBack(void *, char *, unsigned int, unsigned int));

    Packet();
    Packet(unsigned int pt, unsigned int seq, unsigned int ts, unsigned int ssr,
           unsigned int csrcCount = 0);
    Packet(const Packet &o);
    void operator=(const Packet &o);
    ~Packet();
    Packet(const char *dat, unsigned int len);
    char *getData();
    char *ptr() const { return data; }
  };
  
  /// Sorts RTP packets, outputting them through a callback in correct order.
  /// Also keeps track of statistics, which it expects to be read/reset externally (for now).
  /// Optionally can be inherited from with the outPacket function overridden to not use a callback.
  class Sorter{
  public:
    Sorter(uint64_t trackId = 0, void (*callback)(const uint64_t track, const Packet &p) = 0);
    bool wantSeq(uint16_t seq) const;
    void addPacket(const char *dat, unsigned int len);
    void addPacket(const Packet &pack);
    // By default, calls the callback function, if set.
    virtual void outPacket(const uint64_t track, const Packet &p){
      if (callback){callback(track, p);}
    }
    void setCallback(uint64_t track, void (*callback)(const uint64_t track, const Packet &p));
    uint16_t rtpSeq;
    int32_t lostTotal, lostCurrent;
    uint32_t packTotal, packCurrent;

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
    void setProperties(const uint64_t track, const std::string &codec, const std::string &type,
                       const std::string &init, const double multiplier);
    void setProperties(const DTSC::Track &Trk);
    void setCallbacks(void (*cbPack)(const DTSC::Packet &pkt),
                      void (*cbInit)(const uint64_t track, const std::string &initData));
    void addRTP(const RTP::Packet &rPkt);
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
    void handleH264(uint64_t msTime, char *pl, uint32_t plSize, bool missed, bool hasPadding);
    void handleH264Single(uint64_t ts, const char *buffer, const uint32_t len, bool isKey);
    void handleH264Multi(uint64_t ts, char *buffer, const uint32_t len);
    std::string spsData; ///< SPS for H264
    std::string ppsData; ///< PPS for H264
    void handleVP8(uint64_t msTime, const char *buffer, const uint32_t len, bool missed,
                   bool hasPadding);
    Util::ResizeablePointer
        vp8FrameBuffer; ///< Stores successive VP8 payload data. We always start with the first
                        ///< partition; but we might be missing other partitions when they were
                        ///< lost. (a partition is basically what's called a slice in H264).
    bool vp8BufferHasKeyframe;
  };
}// namespace RTP

