#include "dtsc.h"
#include "http_parser.h"
#include "rtp.h"
#include "socket.h"
#include "h265.h"

namespace SDP{

  double getMultiplier(const DTSC::Track & Trk);

  /// Structure used to keep track of selected tracks.
  class Track{
  public:
    Socket::UDPConnection data;
    Socket::UDPConnection rtcp;
    RTP::Packet pack;
    long long rtcpSent;
    uint64_t firstTime;
    int channel; /// Channel number, used in TCP sending
    uint64_t packCount;
    uint16_t rtpSeq;
    int32_t lostTotal, lostCurrent;
    uint32_t packTotal, packCurrent;
    std::map<uint16_t, RTP::Packet> packBuffer;
    std::string transportString; /// Current transport string.
    std::string control;
    std::string fmtp; /// fmtp string, used by getParamString / getParamInt
    std::string spsData;
    std::string ppsData;
    uint32_t mySSRC, theirSSRC, portA, portB, cPortA, cPortB;
    h265::initData hevcInfo;
    uint64_t fpsTime;
    double fpsMeta;
    double fps;
    Track();
    std::string generateTransport(uint32_t trackNo, const std::string &dest = "", bool TCPmode = true);
    std::string getParamString(const std::string &param) const;
    uint64_t getParamInt(const std::string &param) const;
    bool parseTransport(const std::string &transport, const std::string &host,
                        const std::string &source, const DTSC::Track &trk);
    std::string rtpInfo(const DTSC::Track &trk, const std::string &source, uint64_t currentTime);
  };

  class State{
  public:
    State(){
      incomingPacketCallback = 0;
      myMeta = 0;
    }
    DTSC::Meta *myMeta;
    void (*incomingPacketCallback)(const DTSC::Packet &pkt);
    std::map<uint32_t, Track> tracks; ///< List of selected tracks with SDP-specific session data.
    void parseSDP(const std::string &sdp);
    void updateH264Init(uint64_t trackNo);
    void updateH265Init(uint64_t trackNo);
    uint32_t getTrackNoForChannel(uint8_t chan);
    uint32_t parseSetup(HTTP::Parser &H, const std::string &host,
                        const std::string &source);
    void handleIncomingRTP(const uint64_t track, const RTP::Packet &pkt);
    void h264MultiParse(uint64_t ts, const uint64_t track, char *buffer, const uint32_t len);
    void h264Packet(uint64_t ts, const uint64_t track, const char *buffer, const uint32_t len,
                    bool isKey);
    void h265Packet(uint64_t ts, const uint64_t track, const char *buffer, const uint32_t len,
                    bool isKey);
  };

  std::string mediaDescription(const DTSC::Track &trk);
}

