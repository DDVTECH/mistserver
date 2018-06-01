/*

  SOME NOTES ON MIST

     - When a user wants to start pushing video into Mist we need to 
       check if the user is actually allowed to do this. When the user
       is allowed to push we have to call the function `allowPush("")`. 
  
  SIGNALING 
    
     1. Client sends the offer:
     
          {
            type: "offer_sdp",
            offer_sdp: <the-client-offer-sdp>,
          } 
     
        Server responds with:
     
          SUCCESS:
          {
            type: "on_answer_sdp",
            result: true,
            answer_sdp: <the-server-answer-sdp>,
          }

          ERROR:
          {
            type: "on_answer_sdp",
            result: false,
          }

     2. Client request new bitrate:

          {
            type: "video_bitrate"
            video_bitrate: 600000
          } 

        Server responds with:

          SUCCESS:
          {
            type: "on_video_bitrate"
            result: true
          }
  
          ERROR:
          {
             type: "on_video_bitrate"
             result: false
          }
 
 */
#pragma once

#include "output.h"
#include "output_http.h"
#include <mist/h264.h>
#include <mist/http_parser.h>
#include <mist/rtp_fec.h>
#include <mist/sdp_media.h>
#include <mist/socket.h>
#include <mist/tinythread.h>
#include <mist/websocket.h>
#include <mist/certificate.h> 
#include <mist/stun.h>       
#include <mist/dtls_srtp_handshake.h>
#include <mist/srtp.h>        

#if defined(WEBRTC_PCAP)
#  include <mist/pcap.h>
#endif

namespace Mist {
  
  /* ------------------------------------------------ */

  class WebRTCTrack {
  public:
    WebRTCTrack();                       ///< Initializes to some defaults.
    
  public:
    RTP::toDTSC rtpToDTSC;               ///< Converts RTP packets into DTSC packets. 
    RTP::FECSorter sorter;                  ///< Takes care of sorting the received RTP packet and keeps track of some statistics. Will call a callback whenever a packet can be used. (e.g. not lost, in correct order).
    RTP::Packet rtpPacketizer;           ///< Used when we're sending RTP data back to the other peer. 
    uint64_t payloadType;                ///< The payload type that was extracted from the `m=` media line in the SDP. 
    std::string localIcePwd; 
    std::string localIceUFrag;
    uint32_t SSRC;                       ///< The SSRC of the RTP packets.
    uint32_t timestampMultiplier;        ///< Used for outgoing streams to convert the DTSC timestamps into RTP timestamps.
    uint8_t ULPFECPayloadType;           ///< When we've enabled FEC for a video stream this holds the payload type that is used to distinguish between ordinary video RTP packets and FEC packets.
    uint8_t REDPayloadType;              ///< When using RED and ULPFEC this holds the payload type of the RED stream.
    uint8_t RTXPayloadType;              ///< The retransmission payload type when we use RTX (retransmission with separate SSRC/payload type)
    uint16_t prevReceivedSequenceNumber; ///< The previously received sequence number. This is used to NACK packets when we loose one. 
  };

  /* ------------------------------------------------ */
  
  class OutWebRTC : public HTTPOutput {
  public:
    OutWebRTC(Socket::Connection &myConn);
    ~OutWebRTC();
    static void init(Util::Config *cfg);
    virtual void sendHeader();
    virtual void sendNext();
    virtual void onWebsocketFrame();
    bool doesWebsockets(){return true;}
    void handleWebRTCInputOutputFromThread();
    int onDTLSHandshakeWantsToWrite(const uint8_t* data, int* nbytes);
    void onRTPSorterHasPacket(const uint64_t trackID, const RTP::Packet &pkt);
    void onDTSCConverterHasPacket(const DTSC::Packet& pkt);
    void onDTSCConverterHasInitData(const uint64_t trackID, const std::string &initData);
    void onRTPPacketizerHasRTPPacket(char* data, uint32_t nbytes);
    void onRTPPacketizerHasRTCPPacket(char* data, uint32_t nbytes);

  private:
    bool handleWebRTCInputOutput(); ///< Reads data from the UDP socket. Returns true when we read some data, othewise false. 
    void handleReceivedSTUNPacket();
    void handleReceivedDTLSPacket();
    void handleReceivedRTPOrRTCPPacket();
    void handleSignalingCommand(HTTP::Websocket& webSock, const JSON::Value &command);
    bool handleSignalingCommandRemoteOffer(HTTP::Websocket &webSock, const JSON::Value &command);
    bool handleSignalingCommandRemoteOfferForInput(HTTP::Websocket &webSocket, SDP::Session &sdpSession, const std::string &sdpOffer);
    bool handleSignalingCommandRemoteOfferForOutput(HTTP::Websocket &webSocket, SDP::Session &sdpSession, const std::string &sdpOffer);
    bool handleSignalingCommandVideoBitrate(HTTP::Websocket& webSock, const JSON::Value &command);
    bool handleSignalingCommandSeek(HTTP::Websocket& webSock, const JSON::Value &command);
    bool handleSignalingCommandKeyFrameInterval(HTTP::Websocket &webSock, const JSON::Value &command); ///< Handles the command that can be used to set the keyframe interval for the current connection. We will sent RTCP PLI messages every X-millis; the other agent -should- generate keyframes when it receives PLI messages (Picture Loss Indication). 
    void sendSignalingError(HTTP::Websocket& webSock, const std::string& commandType, const std::string& errorMessage);
    bool validateSignalingCommand(HTTP::Websocket& webSock, const JSON::Value &command, JSON::Value &errorResult);
    
    bool createWebRTCTrackFromAnswer(const SDP::Media& mediaAnswer, const SDP::MediaFormat& formatAnswer, WebRTCTrack& result);
    void sendRTCPFeedbackREMB(const WebRTCTrack &rtcTrack);
    void sendRTCPFeedbackPLI(const WebRTCTrack &rtcTrack); ///< Picture Los Indication: request keyframe.
    void sendRTCPFeedbackRR(WebRTCTrack &rtcTrack);
    void sendRTCPFeedbackNACK(const WebRTCTrack &rtcTrack, uint16_t missingSequenceNumber); ///< Notify sender that we're missing a sequence number.
    void sendSPSPPS(DTSC::Track& dtscTrack, WebRTCTrack& rtcTrack);///< When we're streaming H264 to e.g. the browser we inject the PPS and SPS nals.
    void extractFrameSizeFromVP8KeyFrame(const DTSC::Packet &pkt);
    void updateCapabilitiesWithSDPOffer(SDP::Session &sdpSession);
    bool bindUDPSocketOnLocalCandidateAddress(uint16_t port); ///< Binds our UDP socket onto the IP address that we shared via our SDP answer. We *have to* bind on a specific IP, see https://gist.github.com/roxlu/6c5ab696840256dac71b6247bab59ce9
    std::string getLocalCandidateAddress();
    
  private:
    SDP::Session sdp;                             ///< SDP parser. 
    SDP::Answer sdpAnswer;                        ///< WIP: Replacing our `sdp` member .. 
    Certificate cert;                             ///< The TLS certificate. Used to generate a fingerprint in SDP answers.
    DTLSSRTPHandshake dtlsHandshake;              ///< Implements the DTLS handshake using the mbedtls library (fork). 
    SRTPReader srtpReader;                        ///< Used to unprotect incoming RTP and RTCP data. Uses the keys that were exchanged with DTLS. 
    SRTPWriter srtpWriter;                        ///< Used to protect our RTP and RTCP data when sending data to another peer. Uses the keys that were exchanged with DTLS. 
    Socket::UDPConnection udp;                    ///< Our UDP socket over which WebRTC data is received and sent. 
    StunReader stunReader;                        ///< Decodes STUN messages; during a session we keep receiving STUN messages to which we need to reply.
    std::map<uint64_t, WebRTCTrack> webrtcTracks; ///< WebRTCTracks indexed by payload type for incoming data and indexed by myMeta.tracks[].trackID for outgoing data. 
    tthread::thread* webRTCInputOutputThread;     ///< The thread in which we read WebRTC data when we're receive media from another peer. 
    uint16_t udpPort;                             ///< The port on which our webrtc socket is bound. This is where we receive RTP, STUN, DTLS, etc. */
    uint32_t SSRC;                                ///< The SSRC for this local instance. Is used when generating RTCP reports. */
    uint64_t rtcpTimeoutInMillis;                 ///< When current time in millis exceeds this timeout we have to send a new RTCP packet.
    uint64_t rtcpKeyFrameTimeoutInMillis;
    uint64_t rtcpKeyFrameDelayInMillis;
    char* rtpOutBuffer;                           ///< Buffer into which we copy (unprotected) RTP data that we need to deliver to the other peer. This gets protected.
    uint32_t videoBitrate;                        ///< The bitrate to use for incoming video streams. Can be configured via the signaling channel. Defaults to 6mbit.

    bool didReceiveKeyFrame; /* TODO burst delay */
    
#if defined(WEBRTC_PCAP)
    PCAPWriter pcapOut;                           ///< Used during development to write unprotected packets that can be inspected in e.g. wireshark.
    PCAPWriter pcapIn;                            ///< Used during development to write unprotected packets that can be inspected in e.g. wireshark.
#endif

    std::map<uint8_t, uint64_t> payloadTypeToWebRTCTrack; ///< Maps e.g. RED to the corresponding track. Used when input supports RED/ULPFEC; can also be used to map RTX in the future.
  };
}

typedef Mist::OutWebRTC mistOut;
