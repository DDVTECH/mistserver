#pragma once

#include "analyser.h"
#include <mist/h264.h>
#include <mist/http_parser.h>
#include <mist/rtp.h>
#include <mist/sdp.h>

class AnalyserRTSP : public Analyser{
public:
  AnalyserRTSP(Util::Config &conf);
  static void init(Util::Config &cfg);
  bool parsePacket();
  void incoming(const DTSC::Packet &pkt);
  bool isOpen();

private:
  Socket::Connection myConn;
  HTTP::Parser HTTP;
  DTSC::Meta myMeta;
  SDP::State sdpState;
};

