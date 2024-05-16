#pragma once

#include "output_http.h"
#include <mist/certificate.h>
#include <mist/h264.h>
#include <mist/http_parser.h>
#include <mist/rtp_fec.h>
#include <mist/sdp_media.h>
#include <mist/socket.h>
#include <mist/stun.h>
#include <mist/tinythread.h>
#include <mist/websocket.h>
#include <fstream>
#include "output_webrtc_srtp.h"

#ifdef WITH_DATACHANNELS
  #include <usrsctp.h>
#endif

#define NACK_BUFFER_SIZE 1024

#if defined(WEBRTC_PCAP)
#include <mist/pcap.h>
#endif

namespace Mist{

  /* ------------------------------------------------ */

  class nackBuffer{
  public:
    bool isBuffered(uint16_t seq){
      if (!bufs[seq % NACK_BUFFER_SIZE].size()){return false;}
      RTP::Packet tmpPkt(bufs[seq % NACK_BUFFER_SIZE], bufs[seq % NACK_BUFFER_SIZE].size());
      return (tmpPkt.getSequence() == seq);
    }
    const char *getData(uint16_t seq){return bufs[seq % NACK_BUFFER_SIZE];}
    size_t getSize(uint16_t seq){return bufs[seq % NACK_BUFFER_SIZE].size();}
    void assign(uint16_t seq, const char *p, size_t s){
      bufs[seq % NACK_BUFFER_SIZE].assign(p, s);
    }

  private:
    Util::ResizeablePointer bufs[NACK_BUFFER_SIZE];
  };

  class WebRTCTrack{
  public:
    WebRTCTrack(); ///< Initializes to some defaults.

    RTP::toDTSC rtpToDTSC; ///< Converts RTP packets into DTSC packets.
    RTP::FECSorter sorter; ///< Takes care of sorting the received RTP packet and keeps track of some
                           ///< statistics. Will call a callback whenever a packet can be used. (e.g. not lost, in correct order).
    RTP::Packet rtpPacketizer; ///< Used when we're sending RTP data back to the other peer.
    uint64_t payloadType; ///< The payload type that was extracted from the `m=` media line in the SDP.
    std::string localIcePwd;
    std::string localIceUFrag;
    uint32_t SSRC;             ///< The SSRC of the RTP packets.
    uint8_t ULPFECPayloadType; ///< When we've enabled FEC for a video stream this holds the payload
                               ///< type that is used to distinguish between ordinary video RTP
                               ///< packets and FEC packets.
    uint8_t REDPayloadType;    ///< When using RED and ULPFEC this holds the payload type of the RED
                               ///< stream.
    uint8_t RTXPayloadType;    ///< The retransmission payload type when we use RTX (retransmission
                               ///< with separate SSRC/payload type)
    void gotPacket(uint32_t ts);
    uint32_t lastTransit;
    uint32_t lastPktCount;
    double jitter;
  };

  class WebRTCSocket{
  public:
    WebRTCSocket();
    Socket::UDPConnection* udpSock;
    SRTPReader srtpReader; ///< Used to unprotect incoming RTP and RTCP data. Uses the keys that
                           ///< were exchanged with DTLS.
    SRTPWriter srtpWriter; ///< Used to protect our RTP and RTCP data when sending data to another
                           ///< peer. Uses the keys that were exchanged with DTLS.
    std::map<uint32_t, nackBuffer> outBuffers;
    size_t sendRTCP(const char * data, size_t len);
    size_t ackNACK(uint32_t pSSRC, uint16_t seq);
    Util::ResizeablePointer dataBuffer;
  };

  class OutWebRTC : public HTTPOutput{
  public:
    OutWebRTC(Socket::Connection &myConn);
    ~OutWebRTC();
    static void init(Util::Config *cfg);
    virtual void sendNext();
    virtual void onWebsocketFrame();
    virtual void respondHTTP(const HTTP::Parser & req, bool headersOnly);
    virtual void preHTTP(){}
    virtual void preWebsocketConnect();
    virtual bool dropPushTrack(uint32_t trackId, const std::string & dropReason);
    void handleWebsocketIdle();
    virtual void onFail(const std::string &msg, bool critical = false);
    bool doesWebsockets(){return true;}
    void handleWebRTCInputOutputFromThread();
    bool handleUDPSocket(Socket::UDPConnection & sock);
    bool handleUDPSocket(WebRTCSocket & wSock);
    void sendSCTPPacket(const char * data, size_t len);
    void sendPaced(uint64_t uSendWindow);
    void onSCTP(const char * data, size_t len, uint16_t stream, uint32_t ppid);
    void onRTPSorterHasPacket(size_t tid, const RTP::Packet &pkt);
    void onDTSCConverterHasPacket(const DTSC::Packet &pkt);
    void onDTSCConverterHasInitData(const size_t trackID, const std::string &initData);
    void onRTPPacketizerHasRTPPacket(const char *data, size_t nbytes);
    void onRTPPacketizerHasRTCPPacket(const char *data, uint32_t nbytes);
    virtual void connStats(uint64_t now, Comms::Connections &statComm);
    inline virtual bool keepGoing(){return config->is_active && (noSignalling || myConn);}
    virtual void requestHandler();
  protected:
    virtual void idleTime(uint64_t ms){sendPaced(ms*1000);}
  private:
    bool noSignalling;
    uint64_t lastRecv;
    uint64_t lastPackMs;
    uint64_t totalPkts;
    uint64_t totalLoss;
    uint64_t totalRetrans;
    std::ofstream jitterLog;
    std::ofstream packetLog;
    std::string externalAddr;
    void ackNACK(uint32_t SSRC, uint16_t seq);
    bool handleWebRTCInputOutput(); ///< Reads data from the UDP socket. Returns true when we read
                                    ///< some data, othewise false.
    void handleReceivedSTUNPacket(WebRTCSocket &wSock);
    void handleReceivedRTPOrRTCPPacket(WebRTCSocket &wSock);
    bool handleSignalingCommandRemoteOfferForInput(SDP::Session &sdpSession);
    bool handleSignalingCommandRemoteOfferForOutput(SDP::Session &sdpSession);
    void sendSignalingError(const std::string &commandType, const std::string &errorMessage);

    bool createWebRTCTrackFromAnswer(const SDP::Media &mediaAnswer,
                                     const SDP::MediaFormat &formatAnswer, WebRTCTrack &result);
    void sendRTCPFeedbackREMB(const WebRTCTrack &rtcTrack);
    void sendRTCPFeedbackPLI(const WebRTCTrack &rtcTrack); ///< Picture Los Indication: request keyframe.
    void sendRTCPFeedbackRR(WebRTCTrack &rtcTrack);
    void sendRTCPFeedbackNACK(const WebRTCTrack &rtcTrack,
                              uint16_t missingSequenceNumber); ///< Notify sender that we're missing a sequence number.
    void sendSPSPPS(size_t dtscIdx,
                    WebRTCTrack &rtcTrack); ///< When we're streaming H264 to e.g. the browser we
                                            ///< inject the PPS and SPS nals.
    void extractFrameSizeFromVP8KeyFrame(const DTSC::Packet &pkt);
    void updateCapabilitiesWithSDPOffer(SDP::Session &sdpSession);
    bool bindUDPSocketOnLocalCandidateAddress(uint16_t port); ///< Binds our UDP socket onto the IP address that we shared via our SDP
                                                              ///< answer. We *have to* bind on a specific IP, see
                                                              ///< https://gist.github.com/roxlu/6c5ab696840256dac71b6247bab59ce9
    std::string getLocalCandidateAddress();

    SDP::Session sdp;      ///< SDP parser.
    SDP::Answer sdpAnswer; ///< WIP: Replacing our `sdp` member ..
    Certificate cert;      ///< The TLS certificate. Used to generate a fingerprint in SDP answers.
    Socket::UDPConnection mainSocket; //< Main socket created during the initial handshake
    std::map<int, WebRTCSocket> sockets; ///< UDP sockets over which WebRTC data is received and sent.
    std::set<int> rtpSockets; ///< UDP sockets over which (S)RTP data is transmitted/received
    std::set<int> sctpSockets; ///< UDP sockets over which (S)RTP data is transmitted/received
    uint16_t lastMediaSocket; //< Last socket number we received video/audio on
    uint16_t lastMetaSocket; //< Last socket number we received non-media data on
    uint16_t udpPort; ///< Port where we receive RTP, STUN, DTLS, etc.
    StunReader stunReader;     ///< Decodes STUN messages; during a session we keep receiving STUN
                               ///< messages to which we need to reply.
    std::map<uint64_t, WebRTCTrack> webrtcTracks; ///< WebRTCTracks indexed by payload type for incoming data and indexed by
                                                  ///< myMeta.tracks[].trackID for outgoing data.
    tthread::thread *webRTCInputOutputThread; ///< The thread in which we read WebRTC data when
                                              ///< we're receive media from another peer.
    uint32_t SSRC; ///< The SSRC for this local instance. Is used when generating RTCP reports. */
    uint64_t rtcpTimeoutInMillis; ///< When current time in millis exceeds this timeout we have to
                                  ///< send a new RTCP packet.
    uint64_t rtcpKeyFrameTimeoutInMillis;
    uint64_t rtcpKeyFrameDelayInMillis;
    Util::ResizeablePointer rtpOutBuffer; ///< Buffer into which we copy (unprotected) RTP data that we need to deliver
                                          ///< to the other peer. This gets protected.
    uint32_t videoBitrate; ///< The bitrate to use for incoming video streams. Can be configured via
                           ///< the signaling channel. Defaults to 6mbit.
    uint32_t videoConstraint;

    size_t audTrack, vidTrack, prevVidTrack, metaTrack;
    double target_rate; ///< Target playback speed rate (1.0 = normal, 0 = auto)

    bool didReceiveKeyFrame;
    bool setPacketOffset;
    int64_t packetOffset;    ///< For timestamp rewrite with BMO
    uint64_t lastTimeSync;
    bool firstKey;
    bool repeatInit;

    double stats_jitter;
    uint64_t stats_nacknum;
    uint64_t stats_lossnum;
    double stats_lossperc;
    std::deque<double> stats_loss_avg;
    std::map<uint32_t, uint32_t> lostPackets;

#if defined(WEBRTC_PCAP)
    PCAPWriter pcapOut; ///< Used during development to write unprotected packets that can be
                        ///< inspected in e.g. wireshark.
    PCAPWriter pcapIn;  ///< Used during development to write unprotected packets that can be
                        ///< inspected in e.g. wireshark.
#endif

    std::map<uint8_t, uint64_t> payloadTypeToWebRTCTrack; ///< Maps e.g. RED to the corresponding track. Used when input
                                                          ///< supports RED/ULPFEC; can also be used to map RTX in the
                                                          ///< future.
    uint64_t lastSR;
    std::set<size_t> mustSendSR;

    int64_t ntpClockDifference;
    bool syncedNTPClock;

#ifdef WITH_DATACHANNELS
    bool sctpInited;
    bool sctpConnected;
    struct socket * sctp_sock;
    std::map<std::string, uint16_t> dataChannels;
    std::deque<std::string> queuedJSON;
#endif
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutWebRTC mistOut;
#endif
