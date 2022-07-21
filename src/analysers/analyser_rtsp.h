#pragma once

#include "analyser.h"
#include <mist/h264.h>
#include <mist/http_parser.h>
#include <mist/rtp.h>
#include <mist/sdp.h>

class AnalyserRTSP : public Analyser, public Util::DataCallback{
public:
  AnalyserRTSP(Util::Config &conf);
  static void init(Util::Config &cfg);
  bool parsePacket();
  void incoming(const DTSC::Packet &pkt);
//  virtual bool open(const std::string &filename);
  bool isOpen();
  void dataCallback(const char * ptr, size_t size);
  void sendCommand(const std::string &cmd, const std::string &cUrl, const std::string &body,
                     const std::map<std::string, std::string> *extraHeaders = 0, bool reAuth=true);
  void parseStreamHeader();

private:
  Socket::Connection myConn;
  HTTP::Parser HTTP;
  DTSC::Meta myMeta;
  HTTP::Parser sndH, recH;

    bool seenSDP;
    std::string session;
    uint64_t cSeq;
  SDP::State sdpState;
  Socket::Buffer buffer;
};
