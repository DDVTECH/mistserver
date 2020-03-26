#include "analyser_webrtc.h"
#include <mist/url.h>

Socket::UDPConnection * uSocket = 0;
void callback(void * cb, const char * data, size_t s, uint8_t t){
  if(!uSocket){return;}
  uSocket->SendNow(data, s);
}


void AnalyserWebRTC::init(Util::Config &conf){
  Analyser::init(conf);
  JSON::Value opt;
  opt["long"] = "allowedPacketLoss";
  opt["short"] = "L";
  opt["arg"] = "num";
  opt["default"] = "5";
  opt["help"] = "Set allowed packetloss percentage";
  conf.addOption("packetloss", opt);
  opt.null();
}

AnalyserWebRTC::AnalyserWebRTC(Util::Config &conf) : Analyser(conf){
//  filter = conf.getInteger("filter");
  packetCount       = 0;
  firstPacketNr     = 0;
  currTime          = 0;
  highestSeqNumber  = 0;
  lastFeedback      = 0;

  packetLoss = conf.getInteger("packetloss"); 
  
}

void AnalyserWebRTC::sendSDPOffer(){
  std::string val = "{\"type\":\"offer_sdp\",\"offer_sdp\":\"v=0\r\no=- 157193033142093826 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\na=group:BUNDLE 0 1\r\na=msid-semantic: WMS\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111 103 104 9 0 8 106 105 13 110 112 113 126\r\nc=IN IP4 0.0.0.0\r\na=rtcp:9 IN IP4 0.0.0.0\r\na=ice-ufrag:NAG5\r\na=ice-pwd:rN7USyMCTgbpc3zM8/OoCas1\r\na=ice-options:trickle\r\na=fingerprint:sha-256 77:BB:13:84:C8:C9:B5:51:E2:F6:08:99:38:53:BF:48:55:3F:DD:F1:25:B3:93:33:C2:56:C3:E9:3B:8B:9F:4C\r\na=setup:actpass\r\na=mid:0\r\na=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level\r\na=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time\r\na=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01\r\na=extmap:4 urn:ietf:params:rtp-hdrext:sdes:mid\r\na=extmap:5 urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id\r\na=extmap:6 urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id\r\na=recvonly\r\na=rtcp-mux\r\na=rtpmap:111 opus/48000/2\r\na=rtcp-fb:111 transport-cc\r\na=fmtp:111 minptime=10;useinbandfec=1\r\na=rtpmap:103 ISAC/16000\r\na=rtpmap:104 ISAC/32000\r\na=rtpmap:9 G722/8000\r\na=rtpmap:0 PCMU/8000\r\na=rtpmap:8 PCMA/8000\r\na=rtpmap:106 CN/32000\r\na=rtpmap:105 CN/16000\r\na=rtpmap:13 CN/8000\r\na=rtpmap:110 telephone-event/48000\r\na=rtpmap:112 telephone-event/32000\r\na=rtpmap:113 telephone-event/16000\r\na=rtpmap:126 telephone-event/8000\r\nm=video 9 UDP/TLS/RTP/SAVPF 96 97 98 99 100 101 102 122 127 121 125 107 108 109 124 120 123\r\nc=IN IP4 0.0.0.0\r\na=rtcp:9 IN IP4 0.0.0.0\r\na=ice-ufrag:NAG5\r\na=ice-pwd:rN7USyMCTgbpc3zM8/OoCas1\r\na=ice-options:trickle\r\na=fingerprint:sha-256 77:BB:13:84:C8:C9:B5:51:E2:F6:08:99:38:53:BF:48:55:3F:DD:F1:25:B3:93:33:C2:56:C3:E9:3B:8B:9F:4C\r\na=setup:actpass\r\na=mid:1\r\na=extmap:14 urn:ietf:params:rtp-hdrext:toffset\r\na=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time\r\na=extmap:13 urn:3gpp:video-orientation\r\na=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01\r\na=extmap:12 http://www.webrtc.org/experiments/rtp-hdrext/playout-delay\r\na=extmap:11 http://www.webrtc.org/experiments/rtp-hdrext/video-content-type\r\na=extmap:7 http://www.webrtc.org/experiments/rtp-hdrext/video-timing\r\na=extmap:8 http://tools.ietf.org/html/draft-ietf-avtext-framemarking-07\r\na=extmap:9 http://www.webrtc.org/experiments/rtp-hdrext/color-space\r\na=extmap:4 urn:ietf:params:rtp-hdrext:sdes:mid\r\na=extmap:5 urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id\r\na=extmap:6 urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id\r\na=recvonly\r\na=rtcp-mux\r\na=rtcp-rsize\r\na=rtpmap:96 VP8/90000\r\na=rtcp-fb:96 goog-remb\r\na=rtcp-fb:96 transport-cc\r\na=rtcp-fb:96 ccm fir\r\na=rtcp-fb:96 nack\r\na=rtcp-fb:96 nack pli\r\na=rtpmap:97 rtx/90000\r\na=fmtp:97 apt=96\r\na=rtpmap:98 VP9/90000\r\na=rtcp-fb:98 goog-remb\r\na=rtcp-fb:98 transport-cc\r\na=rtcp-fb:98 ccm fir\r\na=rtcp-fb:98 nack\r\na=rtcp-fb:98 nack pli\r\na=fmtp:98 profile-id=0\r\na=rtpmap:99 rtx/90000\r\na=fmtp:99 apt=98\r\na=rtpmap:100 VP9/90000\r\na=rtcp-fb:100 goog-remb\r\na=rtcp-fb:100 transport-cc\r\na=rtcp-fb:100 ccm fir\r\na=rtcp-fb:100 nack\r\na=rtcp-fb:100 nack pli\r\na=fmtp:100 profile-id=2\r\na=rtpmap:101 rtx/90000\r\na=fmtp:101 apt=100\r\na=rtpmap:102 H264/90000\r\na=rtcp-fb:102 goog-remb\r\na=rtcp-fb:102 transport-cc\r\na=rtcp-fb:102 ccm fir\r\na=rtcp-fb:102 nack\r\na=rtcp-fb:102 nack pli\r\na=fmtp:102 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f\r\na=rtpmap:122 rtx/90000\r\na=fmtp:122 apt=102\r\na=rtpmap:127 H264/90000\r\na=rtcp-fb:127 goog-remb\r\na=rtcp-fb:127 transport-cc\r\na=rtcp-fb:127 ccm fir\r\na=rtcp-fb:127 nack\r\na=rtcp-fb:127 nack pli\r\na=fmtp:127 level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42001f\r\na=rtpmap:121 rtx/90000\r\na=fmtp:121 apt=127\r\na=rtpmap:125 H264/90000\r\na=rtcp-fb:125 goog-remb\r\na=rtcp-fb:125 transport-cc\r\na=rtcp-fb:125 ccm fir\r\na=rtcp-fb:125 nack\r\na=rtcp-fb:125 nack pli\r\na=fmtp:125 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\na=rtpmap:107 rtx/90000\r\na=fmtp:107 apt=125\r\na=rtpmap:108 H264/90000\r\na=rtcp-fb:108 goog-remb\r\na=rtcp-fb:108 transport-cc\r\na=rtcp-fb:108 ccm fir\r\na=rtcp-fb:108 nack\r\na=rtcp-fb:108 nack pli\r\na=fmtp:108 level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42e01f\r\na=rtpmap:109 rtx/90000\r\na=fmtp:109 apt=108\r\na=rtpmap:124 red/90000\r\na=rtpmap:120 rtx/90000\r\na=fmtp:120 apt=124\r\na=rtpmap:123 ulpfec/90000\r\n\", \"encrypt\":\"placebo\"}";
  


  //INFO_MSG("send offer");
  ws->sendFrame(val.data(), val.size(), 1);
}

void AnalyserWebRTC::parseAnswer(std::string &ans){
  //INFO_MSG("answer: %s", ans.c_str());
  
  int tmp = ans.find("a=candidate:");
  if(tmp != std::string::npos){
    ans = ans.substr(tmp);  
  }
  
  tmp = ans.find("udp");
  if(tmp != std::string::npos){
    ans = ans.substr(tmp+3);  
  }

  tmp = ans.find(" typ host");
  if(tmp != std::string::npos){
    ans = ans.substr(0,tmp);  
  }

  tmp = ans.rfind(" ");
  std::string port;
  if(tmp != std::string::npos){
    port = ans.substr(tmp+1);  
    ans = ans.substr(0,tmp);
  }

  tmp = ans.rfind(" ");
  std::string ip;
  if(tmp != std::string::npos){
    ip= ans.substr(tmp+1);  
    ans = ans.substr(0,tmp);
  }

  int p = atoi(port.c_str());
  
  udpPort = udp.bind(0, "0.0.0.0");
  udp.SetDestination(ip, p);
  uSocket = &udp;

  INFO_MSG("setting destination to: %s, port: %d", remoteHost.c_str(), p);
}

bool AnalyserWebRTC::open(const std::string &url){
  std::map<std::string, std::string> headers;
  headers["User-Agent"] = "MistLoadTest/" PACKAGE_VERSION "/" RELEASE "/" + JSON::Value(getpid()).asString();
  HTTP::URL purl(url);

  conn = Socket::Connection(purl.host, purl.getPort(), false);
  P.url = "/" + purl.path;

  P.SetHeader("Connection", "upgrade");
  P.SetHeader("Upgrade", "websocket");
  P.SetHeader("Sec-WebSocket-Version", "13");
  P.SetHeader("Sec-WebSocket-Key", "1");
  P.SetHeader("Host", purl.host);
  P.SendRequest(conn);
  remoteHost = purl.host;

  P.Clean();
  while(conn){
    if(conn.spool()){
      if(P.Read(conn)){
        break;
      }
    }else{
      Util::sleep(100); //TODO:timeout
    }
  }

  //TODO:check if 101 status, abort if not 101

  ws = new HTTP::Websocket(conn, P);

  sendSDPOffer();

  while(*ws){ //wait for answer
    if(ws->readFrame()){
      std::string answer((char*)ws->data);
      parseAnswer(answer);
      break;
    }else{
      Util::sleep(100);
    }
  }
  WARN_MSG("answer received");
  if(!*ws){return false;}

  std::string remoteIP = "";
  uint32_t remotePort = 0;
  udp.GetDestination(remoteIP, remotePort);

  StunMessage stun_msg;

  // create the binding success response
  stun_msg.removeAttributes();
  stun_msg.setType(STUN_MSG_TYPE_BINDING_REQUEST);
   
  StunWriter stun_writer;
  stun_writer.begin(stun_msg);
  stun_writer.writeUsername("S22J:bla");

  stun_writer.writeXorMappedAddress(STUN_IP4, remotePort, remoteIP);
  stun_writer.writeMessageIntegrity("sdfd");
  stun_writer.writeFingerprint();
  stun_writer.end();

  udp.SendNow((const char *)stun_writer.getBufferPtr(),stun_writer.getBufferSize());

  return true;
}

bool AnalyserWebRTC::parsePacket(){
  bool hadPack = false;
  int retryLimit = 6;

  if(lastFeedback != Util::bootSecs()){
    lastFeedback = Util::bootSecs(); 
    SDP::Track track;
    RTP::Packet p;
    p.sendRTCP_RR(track, callback);
  }

  while(retryLimit >0 && *isActive){
    if ((udp.getSock() != -1) && udp.Receive()){
      hadPack = true;
      uint8_t fb = (uint8_t)udp.data[0];

      if (fb > 127 && fb < 192){
        if(!handleReceivedRTPOrRTCPPacket()){
          return false;
        }
      }else if (fb > 19 && fb < 64){
        //handleReceivedDTLSPacket();
        INFO_MSG("dtls packet");
      }else if (fb < 2){
        INFO_MSG("stun packet");
        handleReceivedSTUNPacket();
  //      sendDTLS();
      }else{
        FAIL_MSG("Unhandled WebRTC data. Type: %02X", fb);
      }
      return true;
    }else{
      retryLimit--;

      if(lastFeedback != Util::bootSecs()){
        lastFeedback = Util::bootSecs(); 
        SDP::Track track;
        RTP::Packet p;
        p.sendRTCP_RR(track, callback);
      }

      Util::sleep(30);
    }

    if(*ws){
        if(ws->readFrame()){
          JSON::Value frame = JSON::fromString(ws->data, ws->data.size());
          HIGH_MSG("WS: %s", frame.toString().c_str());
        }
    }
  }

  if (udp.getSock() == -1)
  {
    FAIL_MSG("UDP socket closed");
  }

  return hadPack;
}


void AnalyserWebRTC::sendDTLS(){
  INFO_MSG("send dtls");
}

bool AnalyserWebRTC::handleReceivedRTPOrRTCPPacket(){
  RTP::Packet rtp_pkt((const char *)udp.data, (unsigned int)udp.data.size());
  uint16_t currSeqNum = rtp_pkt.getSequence();
  uint32_t timestamp = rtp_pkt.getTimeStamp();
  uint32_t payloadType = rtp_pkt.getPayloadType();

  if(payloadType != 72 && payloadType != 73){

    if(!firstPacketNr){
      firstPacketNr = currSeqNum;
      highestSeqNumber = currSeqNum;
//      firstMediaTime = timestamp / 90;
    }

    if((uint16_t)(currSeqNum - highestSeqNumber) < 0x8000){
      highestSeqNumber = currSeqNum;
    }

    if(Util::bootSecs() > currTime){
      currTime = Util::bootSecs();

      packetsSince.push_back(highestSeqNumber);
      totalPackets.push_back(packetCount);

      if(packetsSince.size() > 5){
        uint16_t tmpPsince = packetsSince.front();
        uint16_t tmpTotalP = totalPackets.front();
        packetsSince.pop_front();
        totalPackets.pop_front();
        
        size_t packLoss = 100- (uint16_t)(packetCount - tmpTotalP) *100  / (uint16_t)(highestSeqNumber - tmpPsince)  ;
        uint16_t packPLoss = (highestSeqNumber  - tmpPsince) -(packetCount - tmpTotalP) ;

        if(packLoss>0){
//        INFO_MSG("tmptotalP %ld highestSeqNumber %ld packetCount %ld, tmpPsince %ld", tmpTotalP , highestSeqNumber , packetCount, tmpPsince );
          INFO_MSG("Packet loss: %lu%% (%" PRIu16 "), packetCount: %lu", packLoss, packPLoss, packetCount) ;
//        WARN_MSG("fintime: %llu, upTime: %llu, mediatime: %llu, fin-up: %llu, mediatime/1000: %llu, mediatime: %llu, timestamp: %llu", finTime, upTime, mediaTime, (finTime - upTime), (mediaTime /1000) +2, mediaTime, timestamp);
        }

        if(packLoss > packetLoss){
          FAIL_MSG("More than %ld%% packetLoss!", packetLoss);
          stop(); 
          return false;
        }
      }
    }
    
    packetCount++;
    mediaTime = timestamp / 90 ;//- firstMediaTime;
  }

  return true;
}

bool AnalyserWebRTC::handleReceivedSTUNPacket(){
  StunMessage stun_msg;
  stun_msg.removeAttributes();
  size_t nparsed = 0;
  if (stunReader.parse((uint8_t *)(void *)udp.data, udp.data.size(), nparsed, stun_msg) != 0){
    FAIL_MSG("Failed to parse a stun message.");
    return false;
  }

  if (stun_msg.type != STUN_MSG_TYPE_BINDING_RESPONSE_SUCCESS){
    INFO_MSG("Received wrong STUN message, should be binding response success...");
    return false;
  }else{
    INFO_MSG("got stun resonse success packet!");
  }
  return true;
}
