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
  
  void parseAnswer(const std::string &ans);
  void doFeedback();

  void onTime(const std::string &value);
  bool handleReceivedSTUNPacket();
  bool handleReceivedRTPOrRTCPPacket();
  void sendDTLS();

  bool isOpen();

private:
  uint16_t udpPort;
  Socket::Connection conn;
  HTTP::Parser P;
  std::string ice_ufrag, ice_pwd;
   
  std::string remoteHost;
  Socket::UDPConnection udp;
  HTTP::Websocket *ws;
  SRTPReader srtpReader; 
  DTLSSRTPHandshake dtlsHandshake; 
  StunReader stunReader;
  uint64_t lastFeedback;

};
