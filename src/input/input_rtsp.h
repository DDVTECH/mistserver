#include "input.h"
#include <mist/dtsc.h>
#include <mist/http_parser.h>
#include <mist/nal.h>
#include <mist/rtp.h>
#include <mist/sdp.h>
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

  protected:
    // Private Functions
    bool checkArguments();
    bool needHeader(){return false;}
    bool readHeader(){return true;}
    void getNext(bool smart = true){}
    bool openStreamSource();
    void closeStreamSource();
    void parseStreamHeader();
    void seek(int seekTime){}
    void sendCommand(const std::string &cmd, const std::string &cUrl, const std::string &body,
                     const std::map<std::string, std::string> *extraHeaders = 0);
    bool parsePacket();
    bool handleUDP();
    std::string streamMainLoop();
    Socket::Connection tcpCon;
    HTTP::Parser sndH, recH;
    HTTP::URL url;
    std::string username, password, authRequest;
    uint64_t cSeq;
    SDP::State sdpState;
    bool seenSDP;
    bool transportSet;
    bool TCPmode;
    std::string session;
    long long connectedAt; ///< The timestamp the connection was made, as reference point for RTCP
                           /// packets.
  };
}// namespace Mist

typedef Mist::InputRTSP mistIn;

