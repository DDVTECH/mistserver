#pragma once
#include "input.h"
#include <mist/dtsc.h>
#include <mist/http_parser.h>
#include <mist/nal.h>
#include <mist/rtp.h>
#include <mist/sdp.h>
#include <mist/url.h>
#include <set>
#include <string>

namespace Mist{
  /// This class contains all functions needed to implement TS Input
  class InputRTSP : public Input{
  public:
    InputRTSP(Util::Config *cfg);
    bool needsLock(){return false;}
    void incoming(const DTSC::Packet &pkt);
    void incomingRTP(const uint64_t track, const RTP::Packet &p);

    virtual std::string getConnectedBinHost(){
      if (tcpCon){return tcpCon.getBinHost();}
      return Input::getConnectedBinHost();
    }

  protected:
    // Private Functions
    bool checkArguments();
    bool needHeader(){return false;}
    bool openStreamSource();
    void closeStreamSource();
    void parseStreamHeader();
    void sendCommand(const std::string &cmd, const std::string &cUrl, const std::string &body,
                     const std::map<std::string, std::string> *extraHeaders = 0, bool reAuth = true);
    bool parsePacket(bool mustHave = false);
    bool handleUDP();
    void streamMainLoop();
    Socket::Connection tcpCon;
    HTTP::Parser sndH, recH;
    HTTP::URL url;
    std::string username, password, authRequest;
    uint64_t cSeq;
    SDP::State sdpState;
    bool seenSDP;
    bool transportSet;
    bool TCPmode;
    bool needAuth;
    std::string session;
    bool setPacketOffset;
    int64_t packetOffset;

    std::string lastRequestedSetup;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::InputRTSP mistIn;
#endif
