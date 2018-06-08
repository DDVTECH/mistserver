#pragma once
#include "dtsc.h"
#include "json.h"
#include "mp4.h"
#include "mp4_generic.h"
#include "socket.h"
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
    char *data;           ///<The actual RTP packet that is being sent
    uint32_t maxDataLen;  ///< Amount of reserved bytes for the packet(s)
    unsigned int datalen; ///<Size of rtp packet
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
    unsigned int getTimeStamp() const;
    void setSequence(unsigned int seq);
    unsigned int getSSRC() const;
    void setSSRC(unsigned long ssrc);

    void setTimestamp(unsigned int t);
    void increaseSequence();
    void sendH264(void *socket, void callBack(void *, char *, unsigned int, unsigned int),
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
    void sendRTCP_RR(long long &connectedAt, SDP::Track & sTrk, unsigned int tid, DTSC::Meta &metadata,
                  void callBack(void *, char *, unsigned int, unsigned int));

    Packet();
    Packet(unsigned int pt, unsigned int seq, unsigned int ts, unsigned int ssr,
           unsigned int csrcCount = 0);
    Packet(const Packet &o);
    void operator=(const Packet &o);
    ~Packet();
    Packet(const char *dat, unsigned int len);
    char *getData();
  };

  class Sorter{
    public:
      Sorter();
      void addPacket(const char *dat, unsigned int len);
      void addPacket(const Packet & pack);
      void setCallback(uint64_t track, void (*callback)(const uint64_t track, const Packet &p));
      uint16_t rtpSeq;
      int32_t lostTotal, lostCurrent;
      uint32_t packTotal, packCurrent;
    private:
      uint64_t packTrack;
      std::map<uint16_t, Packet> packBuffer;
      void (*callback)(const uint64_t track, const Packet &p);
  };

  class MPEGVideoHeader{
    public:
      MPEGVideoHeader(char * d);
      void clear();
      uint16_t getTotalLen() const;
      std::string toString() const;
      void setTempRef(uint16_t ref);
      void setPictureType(uint8_t pType);
      void setSequence();
      void setBegin();
      void setEnd();
    private:
      char * data;
  };

}

