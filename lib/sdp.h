#pragma once
#include "dtsc.h"
#include "h265.h"
#include "http_parser.h"
#include "rtp.h"
#include "socket.h"
#include <vector>

namespace SDP{

  double getMultiplier(const DTSC::Meta *M, size_t tid);

  /// Structure used to keep track of selected tracks.
  class Track{
  public:
    Track();
    std::string generateTransport(uint32_t trackNo, const std::string &dest = "", bool TCPmode = true);
    /// Tries to bind a RTP/RTCP UDP port pair
    bool bindUDPPort(std::string portInfo, std::string hostInfo);
    std::string getParamString(const std::string &param) const;
    uint64_t getParamInt(const std::string &param) const;
    bool parseTransport(const std::string &transport, const std::string &host,
                        const std::string &source, const DTSC::Meta *M, size_t tid);
    std::string rtpInfo(const DTSC::Meta &M, size_t tid, const std::string &source, uint64_t currentTime);
    bool prepareSockets(const std::string dest, uint32_t port);
    bool setPackCodec(const DTSC::Meta *M, size_t tid);

  public:
    Socket::UDPConnection data;
    Socket::UDPConnection rtcp;
    RTP::Packet pack;
    RTP::Sorter sorter;
    long long rtcpSent;
    uint64_t firstTime;
    int channel; /// Channel number, used in TCP sending
    uint64_t packCount;
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
  };

  class State{
  public:
    State();
    void (*incomingPacketCallback)(const DTSC::Packet &pkt);
    void parseSDP(const std::string &sdp);
    void parseSDPEx(const std::string &sdp);
    void updateH264Init(uint64_t trackNo);
    void updateH265Init(uint64_t trackNo);
    void updateInit(const uint64_t trackNo, const std::string &initData);
    size_t getTrackNoForChannel(uint8_t chan);
    size_t parseSetup(HTTP::Parser &H, const std::string &host, const std::string &source);
    void handleIncomingRTP(const uint64_t track, const RTP::Packet &pkt);
    // Sets up the transport from SDP data
    bool parseTransport(const std::string &sdpString);
    // Re-inits internal variables and removes all tracks from meta
    void reinitSDP();

  public:
    DTSC::Meta *myMeta;
    std::map<uint64_t, RTP::toDTSC> tConv; ///< Converters to DTSC
    std::map<uint64_t, Track> tracks; ///< List of selected tracks with SDP-specific session data.
  };

  std::string mediaDescription(const DTSC::Meta *M, size_t tid, uint64_t port = 0);
}// namespace SDP
