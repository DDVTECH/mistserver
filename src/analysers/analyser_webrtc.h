#include "analyser.h"
#include <mist/sdp.h>
#include <mist/websocket.h>

#include <mist/certificate.h>
#include <mist/dtls_srtp_handshake.h>
#include <mist/h264.h>
#include <mist/http_parser.h>
#include <mist/rtp_fec.h>
#include <mist/sdp_media.h>
#include <mist/socket.h>
#include <mist/srtp.h>
#include <mist/stun.h>

class AnalyserWebRTC : public Analyser{
public:
  AnalyserWebRTC(Util::Config &conf);
  bool parsePacket();
  static void init(Util::Config &conf);

  bool open(const std::string &url);
  bool handlePacket();

  void sendSDPOffer();
  void sendFinish();
  
  void parseAnswer(std::string &ans);

  void onAnswerSDP(const std::string &value);
  void onTime(const std::string &value);
  bool handleReceivedSTUNPacket();
  bool handleReceivedRTPOrRTCPPacket();
  void sendDTLS();

private:
  uint16_t udpPort; 
  Socket::Connection conn;
  HTTP::Parser P;
   
  std::string remoteHost;
  Socket::UDPConnection udp;
  HTTP::Websocket *ws;
  SRTPReader srtpReader; 
  DTLSSRTPHandshake dtlsHandshake; 
  StunReader stunReader;
  uint64_t firstPacketNr;
  uint64_t lastFeedback;
  size_t packetCount;
 
  std::deque<uint16_t> packetsSince;
  std::deque<uint16_t> totalPackets;
  uint16_t highestSeqNumber;


  size_t currTime;
  size_t packetLoss;
  uint64_t firstMediaTime;

};
