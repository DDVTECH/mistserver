#pragma once

#include "output.h"
#include <mist/h264.h>
#include <mist/http_parser.h>
#include <mist/rtp.h>
#include <mist/sdp.h>
#include <mist/socket.h>
#include <mist/url.h>

namespace Mist{
  class OutRTSP : public Output{
  public:
    OutRTSP(Socket::Connection &myConn);
    static void init(Util::Config *cfg);
    void sendNext();
    void onRequest();
    void requestHandler();
    bool onFinish();
    void incomingPacket(const DTSC::Packet &pkt);
    void incomingRTP(const uint64_t track, const RTP::Packet &p);

  private:
    uint64_t pausepoint; ///< Position to pause at, when reached
    SDP::State sdpState;
    HTTP::Parser HTTP_R, HTTP_S;
    std::string source;
    uint64_t lastTimeSync;
    bool setPacketOffset;
    int64_t packetOffset;
    bool expectTCP;
    bool checkPort;
    std::string generateSDP(std::string reqUrl);
    bool handleTCP();
    void handleUDP();
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutRTSP mistOut;
#endif
