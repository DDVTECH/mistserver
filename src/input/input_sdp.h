#pragma once
#include "input.h"
#include <mist/dtsc.h>
#include <mist/urireader.h>
#include <mist/nal.h>
#include <mist/rtp.h>
#include <mist/sdp.h>
#include <mist/url.h>
#include <set>
#include <string>

namespace Mist{
  class InputSDP : public Input{
  public:
    InputSDP(Util::Config *cfg);

    // Buffers incoming DTSC packets (from SDP tracks -> RTP sorter)
    void incoming(const DTSC::Packet &pkt);

    void incomingRTP(const uint64_t track, const RTP::Packet &p);

    // Compare two c strings char by char
    bool compareStrings(char* str1, char* str2);

  protected:
    void streamMainLoop();
    bool checkArguments();
    // Overwrite default functions from input
    bool needHeader(){return false;}
    // Force to stream > serve
    bool needsLock(){return false;}
    // Open connection with input
    bool openStreamSource();
    void closeStreamSource();

    // Gets and parses SDP file
    void parseStreamHeader();
    // Passes incoming RTP packets to sorter
    bool parsePacket();
    // Checks if there are updates available to the SDP file
    // and updates the SDP file accordingly
    bool updateSDP();
    
    // Used to read SDP file
    HTTP::URIReader reader;
    // Contains track info
    SDP::State sdpState;
    // Total bytes downloaded
    size_t bytesRead;
    // Local buffer to read into
    char* buffer;
    // Copy of parsed SDP file in order to detect changes
    char* oldBuffer;

    bool setPacketOffset;
    int64_t packetOffset;

    // Count amount of pulls without a packet
    int count;
    // Flag to re-init SDP state
    bool hasBork;

    // Map SSRC to tracks in order to recognize when video source changes
    std::map<size_t, uint32_t> currentSSRC;
    // Map prev SSRC in order to detect old packages to ignore
    std::map<size_t, uint32_t> oldSSRC;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::InputSDP mistIn;
#endif
