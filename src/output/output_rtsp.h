#pragma once

#include "output.h"
#include <mist/socket.h>
#include <mist/rtp.h>
#include <mist/http_parser.h>

namespace Mist {  
  ///Structure used to keep track of selected tracks.
  class trackmeta {
    public:
      trackmeta(){
        rtcpSent = 0;
        channel = 0;
        UDP = false;
        initSent = false;
      }
      Socket::UDPConnection data;
      Socket::UDPConnection rtcp;
      RTP::Packet rtpPacket;/// The RTP packet instance used for this track.
      long long rtcpSent;
      int channel;/// Channel number, used in TCP sending
      bool UDP;/// True if sending over UDP, false otherwise
      bool initSent;
  };
  
  class OutRTSP : public Output {
    public:
      OutRTSP(Socket::Connection & myConn);
      static void init(Util::Config * cfg);
      void sendNext();
      void onRequest();
    private:
      void handleDescribe();
      void handleSetup();
      void handlePlay();
      void handlePause();
      
      long long connectedAt;///< The timestamp the connection was made, as reference point for RTCP packets.
      std::map<int, trackmeta> tracks;///< List of selected tracks with RTSP-specific session data.
      unsigned int seekpoint;///< Current play position
      unsigned int pausepoint;///< Position to pause at, when reached
      HTTP::Parser HTTP_R, HTTP_S;
  };
}

typedef Mist::OutRTSP mistOut;
