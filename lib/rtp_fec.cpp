#include "defines.h"
#include "rtp.h"
#include "rtp_fec.h"

namespace RTP{
  /// Based on the `block PT` value, we can either find the
  /// contents of the codec payload (e.g. H264, VP8) or a ULPFEC header
  /// (RFC 5109). The structure of the ULPFEC data is as follows.
  ///
  ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///      |                RTP Header (12 octets or more)                 |
  ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///      |                    FEC Header (10 octets)                     |
  ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///      |                      FEC Level 0 Header                       |
  ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///      |                     FEC Level 0 Payload                       |
  ///      |                                                               |
  ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///      |                      FEC Level 1 Header                       |
  ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///      |                     FEC Level 1 Payload                       |
  ///      |                                                               |
  ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///      |                            Cont.                              |
  ///      |                                                               |
  ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///
  /// FEC HEADER:
  ///
  ///       0                   1                   2                   3
  ///       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///      |E|L|P|X|  CC   |M| PT recovery |            SN base            |
  ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///      |                          TS recovery                          |
  ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///      |        length recovery        |
  ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///
  ///
  /// FEC LEVEL HEADER
  ///
  ///       0                   1                   2                   3
  ///       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///      |       Protection Length       |             mask              |
  ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///      |              mask cont. (present only when L = 1)             |
  ///      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///
  PacketFEC::PacketFEC(){}

  PacketFEC::~PacketFEC(){
    receivedSeqNums.clear();
    coveredSeqNums.clear();
  }

  bool PacketFEC::initWithREDPacket(const char *data, size_t nbytes){

    if (!data){
      FAIL_MSG("Given fecData pointer is NULL.");
      return false;
    }

    if (nbytes < 23){
      FAIL_MSG("Given fecData is too small. Should be at least: 12 (RTP) + 1 (RED) + 10 (FEC) 23 "
               "bytes.");
      return false;
    }

    if (coveredSeqNums.size() != 0){
      FAIL_MSG("It seems we're already initialized; coveredSeqNums already set.");
      return false;
    }

    if (receivedSeqNums.size() != 0){
      FAIL_MSG("It seems we're already initialized; receivedSeqNums is not empty.");
      return false;
    }

    // Decode RED header.
    RTP::Packet rtpPkt(data, nbytes);
    uint8_t *redHeader = (uint8_t *)(data + rtpPkt.getHsize());
    uint8_t moreBlocks = redHeader[0] & 0x80;
    if (moreBlocks == 1){
      FAIL_MSG("RED header indicates there are multiple blocks. Haven't seen this before (@todo "
               "implement, exiting now).");
      // \todo do not EXIT!
      return false;
    }

    // Copy the data, starting at the FEC header (skip RTP + RED header)
    size_t numHeaderBytes = rtpPkt.getHsize() + 1;
    if (numHeaderBytes > nbytes){
      FAIL_MSG("Invalid FEC packet; too small to contain FEC data.");
      return false;
    }

    fecPacketData.assign(NULL, 0);
    fecPacketData.append(data + numHeaderBytes, nbytes - numHeaderBytes);

    // Extract the sequence numbers this packet protects.
    if (!extractCoveringSequenceNumbers()){
      FAIL_MSG("Failed to extract the protected sequence numbers for this FEC.");
      // @todo we probably want to reset our set.
      return false;
    }

    return true;
  }

  uint8_t PacketFEC::getExtensionFlag(){

    if (fecPacketData.size() == 0){
      FAIL_MSG("Cannot get extension-flag from the FEC header; fecPacketData member is not set. "
               "Not initialized?");
      return 0;
    }

    return ((fecPacketData[0] & 0x80) >> 7);
  }

  uint8_t PacketFEC::getLongMaskFlag(){

    if (fecPacketData.size() == 0){
      FAIL_MSG("Cannot get the long-mask-flag from the FEC header. fecPacketData member is not "
               "set. Not initialized?");
      return 0;
    }

    return ((fecPacketData[0] & 0x40) >> 6);
  }

  // Returns 0 (error), 2 or 6, wich are the valid sizes of the mask.
  uint8_t PacketFEC::getNumBytesUsedForMask(){

    if (fecPacketData.size() == 0){
      FAIL_MSG("Cannot get the number of bytes used by the mask. fecPacketData member is not set. "
               "Not initialized?");
      return 0;
    }

    if (getLongMaskFlag() == 0){return 2;}

    return 6;
  }

  uint16_t PacketFEC::getSequenceBaseNumber(){

    if (fecPacketData.size() == 0){
      FAIL_MSG(
          "Cannot get the sequence base number. fecPacketData member is not set. Not initialized?");
      return 0;
    }

    return (uint16_t)(fecPacketData[2] << 8) | fecPacketData[3];
  }

  char *PacketFEC::getFECHeader(){

    if (fecPacketData.size() == 0){
      FAIL_MSG("Cannot get fec header. fecPacketData member is not set. Not initialized?");
    }

    return fecPacketData;
  }

  char *PacketFEC::getLevel0Header(){

    if (fecPacketData.size() == 0){
      FAIL_MSG("Cannot get the level 0 header. fecPacketData member is not set. Not initialized?");
      return NULL;
    }

    return (char *)(fecPacketData + 10);
  }

  char *PacketFEC::getLevel0Payload(){

    if (fecPacketData.size() == 0){
      FAIL_MSG("Cannot get the level 0 payload. fecPacketData member is not set. Not initialized?");
      return NULL;
    }

    // 10 bytes for FEC header
    // 2 bytes for `Protection Length`
    // 2 or 6 bytes for `mask`.
    return (char *)(fecPacketData + 10 + 2 + getNumBytesUsedForMask());
  }

  uint16_t PacketFEC::getLevel0ProtectionLength(){

    if (fecPacketData.size() == 0){
      FAIL_MSG("Cannot get the level 0 protection length. fecPacketData member is not set. Not "
               "initialized?");
      return 0;
    }

    char *level0Header = getLevel0Header();
    if (!level0Header){
      FAIL_MSG("Failed to get the level 0 header, cannot get protection length.");
      return 0;
    }

    uint16_t protectionLength = (level0Header[0] << 8) | level0Header[1];
    return protectionLength;
  }

  uint16_t PacketFEC::getLengthRecovery(){

    char *fecHeader = getFECHeader();
    if (!fecHeader){
      FAIL_MSG("Cannot get the FEC header which we need to get the `length recovery` field. Not "
               "initialized?");
      return 0;
    }

    uint16_t lengthRecovery = (fecHeader[8] << 8) | fecHeader[9];
    return lengthRecovery;
  }

  // Based on InsertFecPacket of forward_error_correction.cc from
  // Chromium. (used as reference). The `mask` from the
  // FEC-level-header can be 2 or 6 bytes long. Whenever a bit is
  // set to 1 it means that we have to calculate the sequence
  // number for that bit. To calculate the sequence number we
  // start with the `SN base` value (base sequence number) and
  // use the bit offset to increment the SN-base value. E.g.
  // when it's bit 4 and SN-base is 230, it meas that this FEC
  // packet protects the media packet with sequence number
  // 230. We have to start counting the bit numbers from the
  // most-significant-bit (e.g. 1 << 7).
  bool PacketFEC::extractCoveringSequenceNumbers(){

    if (coveredSeqNums.size() != 0){
      FAIL_MSG("Cannot extract protected sequence numbers; looks like we already did that.");
      return false;
    }

    size_t maskNumBytes = getNumBytesUsedForMask();
    if (maskNumBytes != 2 && maskNumBytes != 6){
      FAIL_MSG("Invalid mask size (%zu) cannot extract sequence numbers.", maskNumBytes);
      return false;
    }

    char *maskPtr = getLevel0Header();
    if (!maskPtr){
      FAIL_MSG("Failed to get the level-0 header ptr. Cannot extract protected sequence numbers.");
      return false;
    }

    uint16_t seqNumBase = getSequenceBaseNumber();
    if (seqNumBase == 0){WARN_MSG("Base sequence number is 0; it's possible but unlikely.");}

    // Skip the `Protection Length`
    maskPtr = maskPtr + 2;

    for (uint16_t byteDX = 0; byteDX < maskNumBytes; ++byteDX){
      uint8_t maskByte = maskPtr[byteDX];
      for (uint16_t bitDX = 0; bitDX < 8; ++bitDX){
        if (maskByte & ((1 << 7) - bitDX)){
          uint16_t seqNum = seqNumBase + (byteDX << 3) + bitDX;
          coveredSeqNums.insert(seqNum);
        }
      }
    }

    return true;
  }

  // \todo rename coversSequenceNumber
  bool PacketFEC::coversSequenceNumber(uint16_t sn){
    return (coveredSeqNums.count(sn) == 0) ? false : true;
  }

  void PacketFEC::addReceivedSequenceNumber(uint16_t sn){
    if (false == coversSequenceNumber(sn)){
      FAIL_MSG("Trying to add a received sequence number this instance is not handling (%u).", sn);
      return;
    }

    receivedSeqNums.insert(sn);
  }

  /// This function can be called to recover a missing packet. A
  /// FEC packet is received with a list of media packets it
  /// might be able to recover; this PacketFEC is received after
  /// we should have received the media packets it's protecting.
  ///
  /// Here we first fill al list with the received sequence
  /// numbers that we're protecting; when we're missing one
  /// packet this function will try to recover it.
  ///
  /// The `receivedMediaPackets` is the history of media packets
  /// that you received and keep in a memory. These are used
  /// when XORing when we reconstruct a packet.
  void PacketFEC::tryToRecoverMissingPacket(std::map<uint16_t, Packet> &receivedMediaPackets,
                                            Packet &reconstructedPacket){

    // Mark all the media packets that we protect and which have
    // been received as "received" in our internal list.
    std::set<uint16_t>::iterator protIt = coveredSeqNums.begin();
    while (protIt != coveredSeqNums.end()){
      if (receivedMediaPackets.count(*protIt) == 1){addReceivedSequenceNumber(*protIt);}
      protIt++;
    }

    // We have received all media packets that we could recover;
    // so there is no need for this FEC packet.
    // @todo Jaron shall we reuse allocs/PacketFECs?
    if (receivedSeqNums.size() == coveredSeqNums.size()){return;}

    if (coveredSeqNums.size() != (receivedSeqNums.size() + 1)){
      // missing more then 1 packet. we can only recover when
      // one packet is lost.
      return;
    }

    // Find missing sequence number.
    uint16_t missingSeqNum = 0;
    protIt = coveredSeqNums.begin();
    while (protIt != coveredSeqNums.end()){
      if (receivedSeqNums.count(*protIt) == 0){
        missingSeqNum = *protIt;
        break;
      }
      ++protIt;
    }
    if (!coversSequenceNumber(missingSeqNum)){
      WARN_MSG("We cannot recover %u.", missingSeqNum);
      return;
    }

    // Copy FEC into new RTP-header
    char *fecHeader = getFECHeader();
    if (!fecHeader){
      FAIL_MSG("Failed to get the fec header. Cannot recover.");
      return;
    }
    recoverData.assign(NULL, 0);
    recoverData.append(fecHeader, 12);

    // Copy FEC into new RTP-payload
    char *level0Payload = getLevel0Payload();
    if (!level0Payload){
      FAIL_MSG("Failed to get the level-0 payload data (XOR'd media data from FEC packet).");
      return;
    }
    uint16_t level0ProtLen = getLevel0ProtectionLength();
    if (level0ProtLen == 0){
      FAIL_MSG("Failed to get the level-0 protection length.");
      return;
    }
    recoverData.append(level0Payload, level0ProtLen);

    uint8_t recoverLength[2] ={fecHeader[8], fecHeader[9]};

    // XOR headers
    protIt = coveredSeqNums.begin();
    while (protIt != coveredSeqNums.end()){

      uint16_t seqNum = *protIt;
      if (seqNum == missingSeqNum){
        ++protIt;
        continue;
      }

      Packet &mediaPacket = receivedMediaPackets[seqNum];
      char *mediaData = mediaPacket.ptr();
      uint16_t mediaSize = mediaPacket.getPayloadSize();
      uint8_t *mediaSizePtr = (uint8_t *)&mediaSize;

      WARN_MSG(" => XOR header with %u, size: %u.", seqNum, mediaSize);

      // V, P, X, CC, M, PT
      recoverData[0] ^= mediaData[0];
      recoverData[1] ^= mediaData[1];

      // Timestamp
      recoverData[4] ^= mediaData[4];
      recoverData[5] ^= mediaData[5];
      recoverData[6] ^= mediaData[6];
      recoverData[7] ^= mediaData[7];

      // Length of recovered media packet
      recoverLength[0] ^= mediaSizePtr[1];
      recoverLength[1] ^= mediaSizePtr[0];

      ++protIt;
    }

    uint16_t recoverPayloadSize = ntohs(*(uint16_t *)recoverLength);

    // XOR payloads
    protIt = coveredSeqNums.begin();
    while (protIt != coveredSeqNums.end()){
      uint16_t seqNum = *protIt;
      if (seqNum == missingSeqNum){
        ++protIt;
        continue;
      }
      Packet &mediaPacket = receivedMediaPackets[seqNum];
      char *mediaData = mediaPacket.ptr() + mediaPacket.getHsize();
      for (size_t i = 0; i < recoverPayloadSize; ++i){recoverData[12 + i] ^= mediaData[i];}
      ++protIt;
    }

    // And setup the reconstructed packet.
    reconstructedPacket = Packet(recoverData, recoverPayloadSize);
    reconstructedPacket.setSequence(missingSeqNum);
    // @todo check what other header fields we need to fix.
  }

  void FECSorter::addPacket(const Packet &pack){
    if (tmpVideoLossPrevention & SDP_LOSS_PREVENTION_ULPFEC){
      packetHistory[pack.getSequence()] = pack;
      while (packetHistory.begin()->first < pack.getSequence() - 500){
        packetHistory.erase(packetHistory.begin());
      }
    }
    Sorter::addPacket(pack);
  }

  /// This function will handle RED packets that may be used to
  /// encapsulate ULPFEC or simply the codec payload (e.g. H264,
  /// VP8). This function is created to handle FEC with
  /// WebRTC. When we want to use FEC with WebRTC we have to add
  /// both the `a=rtpmap:<ulp-fmt> ulpfec/90000` and
  /// `a=rtpmap<red-fmt> red/90000` lines to the SDP. FEC is
  /// always used together with RED (RFC 1298).  It turns out
  /// that with WebRTC the RED only adds one byte after the RTP
  /// header (only the `F` and `block PT`, see below)`. The
  /// `block PT` is the payload type of the data that
  /// follows. This would be `<ulp-fmt>` for FEC data. Though
  /// these RED packets may contain FEC or just the media:
  /// H264/VP8.
  ///
  /// RED HEADER:
  ///
  ///    0                   1                    2                   3
  ///    0 1 2 3 4 5 6 7 8 9 0 1 2 3  4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  ///   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///   |F|   block PT  |  timestamp offset         |   block length    |
  ///   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  ///
  void FECSorter::addREDPacket(char *dat, unsigned int len, uint8_t codecPayloadType,
                               uint8_t REDPayloadType, uint8_t ULPFECPayloadType){

    RTP::Packet pkt(dat, len);
    if (pkt.getPayloadType() != REDPayloadType){
      FAIL_MSG("Requested to add a RED packet, but it has an invalid payload type.");
      return;
    }

    // Check if the `F` flag is set. Chromium will always set
    // this to 0 (at time of writing, check: https://goo.gl/y1eJ6k
    uint8_t *REDHeader = (uint8_t *)(dat + pkt.getHsize());
    uint8_t moreBlocksAvailable = REDHeader[0] & 0x80;
    if (moreBlocksAvailable == 1){
      FAIL_MSG("Not yet received a RED packet that had it's F bit set; @todo implement.");
      exit(EXIT_FAILURE);
      return;
    }

    // Extract the `block PT` field which can be the media-pt,
    // fec-pt. When it's just media that follows, we move all
    // data one byte up and reconstruct a normal media packet.
    uint8_t blockPayloadType = REDHeader[0] & 0x7F;
    if (blockPayloadType == codecPayloadType){
      memmove(dat + pkt.getHsize(), dat + pkt.getHsize() + 1, len - pkt.getHsize() - 1);
      dat[1] &= 0x80;
      dat[1] |= codecPayloadType;
      RTP::Packet mediaPacket((const char *)dat, len - 1);
      addPacket(mediaPacket);
      return;
    }

    // When the payloadType equals our ULP/FEC payload type, we
    // received a REC packet (RFC 5109) that contains FEC data
    // and a list of sequence number that it covers and can
    // reconstruct.
    //
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    //
    // \todo Jaron, I'm now just generating a `PacketFEC` on the heap
    //       and we're not managing destruction anywhere atm; I guess
    //       re-use or destruction needs to be part of the algo that
    //       is going to deal with FEC.
    //
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    if (blockPayloadType == ULPFECPayloadType){
      WARN_MSG(" => got fec packet: %u", pkt.getSequence());
      PacketFEC *fec = new PacketFEC();
      if (!fec->initWithREDPacket(dat, len)){
        delete fec;
        fec = NULL;
        FAIL_MSG("Failed to initialize a `PacketFEC`");
      }
      fecPackets.push_back(fec);

      Packet recreatedPacket;
      fec->tryToRecoverMissingPacket(packetHistory, recreatedPacket);
      if (recreatedPacket.ptr() != NULL){
        char *pl = (char *)recreatedPacket.getPayload();
        WARN_MSG(" => reconstructed %u, %02X %02X %02X %02X | %02X %02X %02X %02X",
                 recreatedPacket.getSequence(), pl[0], pl[1], pl[2], pl[3], pl[4], pl[5], pl[6], pl[7]);
        addPacket(recreatedPacket);
      }
      return;
    }

    FAIL_MSG("Unhandled RED block payload type %u. Check the answer SDP.", blockPayloadType);
  }

  /// Each FEC packet is capable of recovering a limited amount
  /// of media packets. Some experimentation showed that most
  /// often one FEC is used to protect somewhere between 2-10
  /// media packets.  Each FEC packet has a list of sequence
  /// number that it can recover when all other media packets
  /// have been received except the one that we want to
  /// recover. This function returns the FEC packet might be able
  /// to recover the given sequence number.
  PacketFEC *FECSorter::getFECPacketWhichCoversSequenceNumber(uint16_t sn){
    size_t nfecs = fecPackets.size();
    for (size_t i = 0; i < nfecs; ++i){
      PacketFEC *fec = fecPackets[i];
      if (fec->coversSequenceNumber(sn)){return fec;}
    }
    return NULL;
  }

  void FECPacket::sendRTCP_RR(RTP::FECSorter &sorter, uint32_t mySSRC, uint32_t theirSSRC, void *userData,
                              void callBack(void *userData, const char *payload, size_t nbytes, uint8_t channel), uint32_t jitter){
    char *rtcpData = (char *)malloc(32);
    if (!rtcpData){
      FAIL_MSG("Could not allocate 32 bytes. Something is seriously messed up.");
      return;
    }
    if (!(sorter.lostCurrent + sorter.packCurrent)){sorter.packCurrent++;}
    rtcpData[0] = 0x81;                  // version 2, no padding, one receiver report
    rtcpData[1] = 201;                   // receiver report
    Bit::htobs(rtcpData + 2, 7);         // 7 4-byte words follow the header
    Bit::htobl(rtcpData + 4, mySSRC);    // set receiver identifier
    Bit::htobl(rtcpData + 8, theirSSRC); // set source identifier
    rtcpData[12] = (sorter.lostCurrent * 255) / (sorter.lostCurrent + sorter.packCurrent); // fraction lost since prev RR
    Bit::htob24(rtcpData + 13, sorter.lostTotal); // cumulative packets lost since start
    Bit::htobl(rtcpData + 16,
               sorter.rtpSeq | (sorter.packTotal & 0xFFFF0000ul)); // highest sequence received
    Bit::htobl(rtcpData + 20, jitter); // jitter
    Bit::htobl(rtcpData + 24, sorter.lastNTP); // last SR NTP time (middle 32 bits)
    if (sorter.lastBootMS){
      Bit::htobl(rtcpData + 28, (Util::bootMS() - sorter.lastBootMS) * 65.536); // delay since last SR in 1/65536th of a second
    }else{
      Bit::htobl(rtcpData + 28, 0); // no delay since last SR yet
    }
    callBack(userData, rtcpData, 32, 0);
    sorter.lostCurrent = 0;
    sorter.packCurrent = 0;
    free(rtcpData);
  }

}// namespace RTP
