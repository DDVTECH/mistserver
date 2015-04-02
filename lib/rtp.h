#pragma once
#include <string>
#include <vector>
#include <set>
#include <iostream>
#include <iomanip>
#include <cstdio>
#include <stdint.h>
#include <sstream>
#include <deque>
#include <algorithm>
#include "socket.h"
#include "json.h"
#include "dtsc.h"
#include "mp4.h"
#include "mp4_generic.h"

/// This namespace holds all RTP-parsing and sending related functionality.
namespace RTP {

  /// This class is used to make RTP packets. Currently, H264, and AAC are supported. RTP mechanisms, like increasing sequence numbers and setting timestamps are all taken care of in here.
  class Packet {
    private:
      bool managed;
      char * data; ///<The actual RTP packet that is being sent
      unsigned int datalen;  ///<Size of rtp packet
      int sentPackets;
      int sentBytes;//Because ugly is beautiful
      inline void htobll(char * p, long long val);
    public:
      static double startRTCP;
      unsigned int getHsize() const;
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
      void sendH264(void * socket, void callBack(void *, char *, unsigned int, unsigned int), const char * payload, unsigned int payloadlen, unsigned int channel);
      void sendData(void * socket, void callBack(void *, char *, unsigned int, unsigned int), const char * payload, unsigned int payloadlen, unsigned int channel, std::string codec);
      void sendRTCP(long long & connectedAt, void * socket,  unsigned int tid, DTSC::Meta & metadata, void callBack(void *, char *, unsigned int, unsigned int));


      Packet();
      Packet(unsigned int pt, unsigned int seq, unsigned int ts, unsigned int ssr, unsigned int csrcCount = 0);
      Packet(const Packet & o);
      void operator=(const Packet & o);
      ~Packet();
      Packet(const char * dat, unsigned int len);
      char * getData();
  };

}
