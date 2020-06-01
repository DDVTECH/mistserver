#pragma once
#include "rtp.h"
#include "sdp_media.h"
#include "util.h"
#include <set>

namespace RTP{

  /// Util class that can be used to retrieve information from a
  /// FEC packet. A FEC packet is contains recovery data.  This
  /// data can be used to reconstruct a media packet.  This class
  /// was created and tested for the WebRTC implementation where
  /// each FEC packet is encapsulated by a RED packet (RFC 1298).
  /// A RED packet may contain ordinary payload data -or- FEC
  /// data (RFC 5109).  We assume that the data given into
  /// `initWithREDPacket()` contains FEC data and you did a bit
  /// of parsing to figure this out: by checking if the `block
  /// PT` from the RED header is the ULPFEC payload type; if so
  /// this PacketFEC class can be used.
  class PacketFEC{
  public:
    PacketFEC();
    ~PacketFEC();
    bool initWithREDPacket(const char *data,
                           size_t nbytes); /// Initialize using the given data.  `data` must point to the first byte of
                                           /// the RTP packet which contains the RED and FEC headers and data.
    uint8_t getExtensionFlag(); ///< From fec header: should be 0, see
                                ///< https://tools.ietf.org/html/rfc5109#section-7.3.
    uint8_t getLongMaskFlag(); ///< From fec header: returns 0 when the short mask version is used (16
                               ///< bits), otherwise 1 (48 bits). The mask is used to calculate what
                               ///< sequence numbers are protected, starting at the base sequence number.
    uint16_t getSequenceBaseNumber(); ///< From fec header: get the base sequence number. The base
                                      ///< sequence number is used together with the mask to
                                      ///< determine what the sequence numbers of the media packets
                                      ///< are that the fec data protects.
    uint8_t getNumBytesUsedForMask(); ///< From fec level header: a fec packet can protected up to
                                      ///< 48 media packets. Which sequence numbers are stored using
                                      ///< a mask bit string. This returns either 2 or 6.
    char *getLevel0Header(); ///< Get a pointer to the start of the fec-level-0 header (contains the
                             ///< protection-length and mask)
    char *getLevel0Payload(); /// < Get a pointer to the actual FEC data. This is the XOR'd header
                              /// and paylaod.
    char *getFECHeader();     ///< Get a pointer to the first byte of the FEC header.
    uint16_t getLevel0ProtectionLength(); ///< Get the length of the `getLevel0Payload()`.
    uint16_t getLengthRecovery(); ///< Get the `length recovery` value (Little Endian). This value is used
                                  ///< while XORing to recover the length of the missing media packet.
    bool coversSequenceNumber(uint16_t sn); ///< Returns true when this `PacketFEC` instance is used
                                            ///< to protect the given sequence number.
    void addReceivedSequenceNumber(uint16_t sn); ///< Whenever you receive a media packet (complete) call
                                                 ///< this as we need to know if enough media packets
                                                 ///< exist that are needed to recover another one.
    void tryToRecoverMissingPacket(
        std::map<uint16_t, RTP::Packet> &receivedMediaPackets,
        Packet &reconstructedPacket); ///< Pass in a `std::map` indexed by sequence number of -all-
                                      ///< the media packets that you keep as history. When this
                                      ///< `PacketFEC` is capable of recovering a media packet it
                                      ///< will fill the packet passed by reference.

  private:
    bool extractCoveringSequenceNumbers(); ///< Used internally to fill the `coveredSeqNums` member which
                                           ///< tell us what media packets this FEC packet rotects.

  public:
    Util::ResizeablePointer fecPacketData;
    Util::ResizeablePointer recoverData;
    std::set<uint16_t> coveredSeqNums; ///< The sequence numbers of the packets that this FEC protects.
    std::set<uint16_t> receivedSeqNums; ///< We keep track of sequence numbers that were received (at some higher
                                        ///< level). We can only recover 1 media packet and this is used to check
                                        ///< if this `PacketFEC` instance is capable of recovering anything.
  };

  class FECSorter : public Sorter{
  public:
    void addPacket(const Packet &pack);
    void addREDPacket(char *dat, unsigned int len, uint8_t codecPayloadType, uint8_t REDPayloadType,
                      uint8_t ULPFECPayloadType);
    PacketFEC *getFECPacketWhichCoversSequenceNumber(uint16_t sn);
    uint8_t tmpVideoLossPrevention; ///< TMP used to drop packets for FEC; see output_webrtc.cpp
                                    ///< `handleSignalingCommandRemoteOfferForInput()`.  This
                                    ///< variable should be rmeoved when cleaning up.
  private:
    std::map<uint16_t, Packet> packetHistory;
    std::vector<PacketFEC *> fecPackets;
  };

  class FECPacket : public Packet{
  public:
    void sendRTCP_RR(RTP::FECSorter &sorter, uint32_t mySSRC, uint32_t theirSSRC, void *userData,
                     void callBack(void *userData, const char *payload, size_t nbytes, uint8_t channel), uint32_t jitter = 0);
  };

}// namespace RTP
