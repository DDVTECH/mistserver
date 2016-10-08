#pragma once

#include "output.h"
#include <mist/socket.h>
#include <mist/rtp.h>
#include <mist/http_parser.h>
#include <mist/encode.h>
#include <mist/h264.h>

namespace Mist {  
  ///Structure used to keep track of selected tracks.
  class RTPTrack {
    public:
      Socket::UDPConnection data;
      Socket::UDPConnection rtcp;
      RTP::Packet pack;
      long long rtcpSent;
      uint64_t firstTime;
      int channel;/// Channel number, used in TCP sending
      uint64_t packCount;
      uint16_t rtpSeq;
      std::map<uint16_t, RTP::Packet> packBuffer;
      uint32_t cPort;
      std::string transportString;
      std::string control;
      std::string fmtp;
      uint64_t fpsTime;
      double fps;
      RTPTrack(){
        rtcpSent = 0;
        channel = -1;
        firstTime = 0;
        packCount = 0;
        cPort = 0;
        rtpSeq = 0;
        fpsTime = 0;
        fps = 0;
      }
      std::string getParamString(const std::string & param) const{
        if (!fmtp.size()){return "";}
        size_t pos = fmtp.find(param);
        if (pos == std::string::npos){return "";}
        pos += param.size()+1;
        size_t ePos = fmtp.find_first_of(" ;", pos);
        return fmtp.substr(pos, ePos-pos);
      }
      uint64_t getParamInt(const std::string & param) const{
        return atoll(getParamString(param).c_str());
      }
      std::string mediaDescription(const DTSC::Track & trk){
        std::stringstream mediaDesc;
        if (trk.codec == "H264") {
          MP4::AVCC avccbox;
          avccbox.setPayload(trk.init);
          mediaDesc << "m=video 0 RTP/AVP 97\r\n"
          "a=rtpmap:97 H264/90000\r\n"
          "a=cliprect:0,0," << trk.height << "," << trk.width << "\r\n"
          "a=framesize:97 " << trk.width << '-' << trk.height << "\r\n"
          "a=fmtp:97 packetization-mode=1;profile-level-id="
          << std::hex << std::setw(2) << std::setfill('0') << (int)trk.init.data()[1] << std::dec << "E0"
          << std::hex << std::setw(2) << std::setfill('0') << (int)trk.init.data()[3] << std::dec << ";"
          "sprop-parameter-sets="
          << Encodings::Base64::encode(std::string(avccbox.getSPS(), avccbox.getSPSLen()))
          << ","
          << Encodings::Base64::encode(std::string(avccbox.getPPS(), avccbox.getPPSLen()))
          << "\r\n"
          "a=framerate:" << ((double)trk.fpks)/1000.0 << "\r\n"
          "a=control:track" << trk.trackID << "\r\n";
        } else if (trk.codec == "AAC") {
          mediaDesc << "m=audio 0 RTP/AVP 96" << "\r\n"
          "a=rtpmap:96 mpeg4-generic/" << trk.rate << "/" << trk.channels << "\r\n"
          "a=fmtp:96 streamtype=5; profile-level-id=15; config=";
          for (unsigned int i = 0; i < trk.init.size(); i++) {
            mediaDesc << std::hex << std::setw(2) << std::setfill('0') << (int)trk.init[i] << std::dec;
          }
          //these values are described in RFC 3640
          mediaDesc << "; mode=AAC-hbr; SizeLength=13; IndexLength=3; IndexDeltaLength=3;\r\n"
          "a=control:track" << trk.trackID << "\r\n";
        }else if (trk.codec == "MP3") {
          mediaDesc << "m=" << trk.type << " 0 RTP/AVP 14" << "\r\n"
          "a=rtpmap:14 MPA/90000/" << trk.channels << "\r\n"
          "a=control:track" << trk.trackID << "\r\n";
        }else if ( trk.codec == "AC3") {
          mediaDesc << "m=audio 0 RTP/AVP 100" << "\r\n"
          "a=rtpmap:100 AC3/" << trk.rate << "/" << trk.channels << "\r\n"
          "a=control:track" << trk.trackID << "\r\n";
        }
        return mediaDesc.str();
      }
      bool parseTransport(const std::string & transport, const std::string & host, const std::string & source, const DTSC::Track & trk){
        unsigned int SSrc = rand();
        if (trk.codec == "H264") {
          pack = RTP::Packet(97, 1, 0, SSrc);
        }else if(trk.codec == "AAC"){
          pack = RTP::Packet(96, 1, 0, SSrc);
        }else if(trk.codec == "AC3"){
          pack = RTP::Packet(100, 1, 0, SSrc);
        }else if(trk.codec == "MP3"){
          pack = RTP::Packet(14, 1, 0, SSrc);
        }else{
          ERROR_MSG("Unsupported codec %s for RTSP on track %u", trk.codec.c_str(), trk.trackID);
          return false;
        }
        std::cerr << transport << std::endl;
        if (transport.find("TCP") != std::string::npos) {
          std::string chanE =  transport.substr(transport.find("interleaved=") + 12, (transport.size() - transport.rfind('-') - 1)); //extract channel ID
          channel = atol(chanE.c_str());
          rtcpSent = 0;
          transportString = transport;
        } else {
          channel = -1;
          size_t port_loc = transport.rfind("client_port=") + 12;
          cPort = atol(transport.substr(port_loc, transport.rfind('-') - port_loc).c_str());
          uint32_t portA, portB;
          //find available ports locally;
          int sendbuff = 4*1024*1024;
          data.SetDestination(host, cPort);
          portA = data.bind(0);
          setsockopt(data.getSock(), SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));
          rtcp.SetDestination(host, cPort + 1);
          portB = rtcp.bind(0);
          setsockopt(rtcp.getSock(), SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff));
          std::stringstream tStr;
          tStr << "RTP/AVP/UDP;unicast;client_port=" << cPort << '-' << cPort + 1 << ";";
          if (source.size()){
            tStr << "source=" << source << ";";
          }
          tStr << "server_port=" << portA << "-" << portB << ";ssrc=" << std::hex << SSrc << std::dec;
          transportString = tStr.str();
          INFO_MSG("Transport string: %s", transportString.c_str());
        }
        return true;
      }
      std::string rtpInfo(const DTSC::Track & trk, const std::string & source, uint64_t currentTime){
        unsigned int timeMultiplier = 1;
        if (trk.codec == "H264") {
          timeMultiplier = 90;
        } else if (trk.codec == "AAC" || trk.codec == "MP3" || trk.codec == "AC3") {
          timeMultiplier = ((double)trk.rate / 1000.0);
        }
        std::stringstream rInfo;
        rInfo << "url=" << source << "/track" << trk.trackID << ";"; //get the current url, not localhost
        rInfo << "sequence=" << pack.getSequence() << ";rtptime=" << currentTime * timeMultiplier;
        return rInfo.str();
      }
  };
  
  class OutRTSP : public Output {
    public:
      OutRTSP(Socket::Connection & myConn);
      static void init(Util::Config * cfg);
      void sendNext();
      void onRequest();
      void requestHandler();
      bool isReadyForPlay();
      bool onFinish();
    private:
      bool isPushing;
      void parseSDP(const std::string & sdp);
      long long connectedAt;///< The timestamp the connection was made, as reference point for RTCP packets.
      std::map<int, RTPTrack> tracks;///< List of selected tracks with RTSP-specific session data.
      std::map<int, h264::SPSMeta> h264meta;///< Metadata from SPS of H264 tracks, for input handling.
      unsigned int pausepoint;///< Position to pause at, when reached
      HTTP::Parser HTTP_R, HTTP_S;
      std::string source;
      bool expectTCP;
      bool handleTCP();
      void handleUDP();
      void handleIncomingRTP(const uint64_t track, const RTP::Packet & pkt);
      void h264Packet(uint64_t ts, const uint64_t track, const char * buffer, const uint32_t len, bool isKey);
      bool nextIsKey;
  };
}

typedef Mist::OutRTSP mistOut;
