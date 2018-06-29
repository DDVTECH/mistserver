#pragma once

#include "output.h"
#include <mist/h264.h>
#include <mist/http_parser.h>
#include <mist/rtp.h>
#include <mist/sdp.h>
#include <mist/socket.h>

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

  private:
    long long connectedAt;   ///< The timestamp the connection was made, as reference point for RTCP
                             ///packets.
    unsigned int pausepoint; ///< Position to pause at, when reached
    SDP::State sdpState;
    HTTP::Parser HTTP_R, HTTP_S;
    std::string source;
    uint64_t lastTimeSync;
    int64_t bootMsOffset;
    int64_t packetOffset;
    bool expectTCP;
    bool checkPort;
    bool handleTCP();
    void handleUDP();
  };
}

typedef Mist::OutRTSP mistOut;

