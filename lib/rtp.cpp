#include "rtp.h"

#include "adts.h"
#include "av1.h"
#include "bitfields.h"
#include "defines.h"
#include "h264.h"
#include "mpeg.h"
#include "sdp.h"
#include "timing.h"

#include <arpa/inet.h>

namespace RTP{
  double Packet::startRTCP = 0;
  unsigned int MAX_SEND = 1500 - 28;
  unsigned int PACKET_REORDER_WAIT = 5;
  unsigned int PACKET_DROP_TIMEOUT = 30;

  unsigned int Packet::getHsize() const{
    unsigned int r = 12 + 4 * getContribCount();
    if (getExtension()){r += (1 + Bit::btohs(data + r + 2)) * 4;}
    return r;
  }

  unsigned int Packet::getPayloadSize() const{
    // If there is more padding than content, ignore the packet
    if (getHsize() + (getPadding() ? data[maxDataLen - 1] : 0) >= maxDataLen){
      WARN_MSG("Packet has more padding than payload; ignoring packet");
      return 0;
    }
    return maxDataLen - getHsize() - (getPadding() ? data[maxDataLen - 1] : 0);
  }

  char *Packet::getPayload() const{return data + getHsize();}

  uint32_t Packet::getVersion() const{return (data[0] >> 6) & 0x3;}

  uint32_t Packet::getPadding() const{return (data[0] >> 5) & 0x1;}

  uint32_t Packet::getExtension() const{return (data[0] >> 4) & 0x1;}

  uint32_t Packet::getContribCount() const{return (data[0]) & 0xE;}

  uint32_t Packet::getMarker() const{return (data[1] >> 7) & 0x1;}

  uint32_t Packet::getPayloadType() const{return (data[1]) & 0x7F;}

  uint16_t Packet::getSequence() const{return Bit::btohs(data + 2);}

  uint32_t Packet::getTimeStamp() const{return Bit::btohl(data + 4);}

  unsigned int Packet::getSSRC() const{return Bit::btohl(data + 8);}

  const char *Packet::getData(){return data + 8 + 4 * getContribCount() + getExtension();}

  void Packet::setTimestamp(uint32_t timestamp){Bit::htobl(data + 4, timestamp);}

  void Packet::setSequence(uint16_t seq){Bit::htobs(data + 2, seq);}

  void Packet::setSSRC(uint32_t ssrc){Bit::htobl(data + 8, ssrc);}

  void Packet::increaseSequence(){setSequence(getSequence() + 1);}

  /// \brief Enables Pro-MPEG FEC with the specified amount of rows and columns
  bool Packet::configureFEC(uint8_t rows, uint8_t columns){
    if (rows < 4 || rows > 20){
      ERROR_MSG("Rows should have a value between 4-20");
      return false;
    } else if (columns < 1 || columns > 20){
      ERROR_MSG("Columns should have a value between 1-20");
      return false;
    } else if (rows * columns > 100){
      ERROR_MSG("The product of rows * columns cannot exceed 100");
      return false;
    }
    fecEnabled = true;
    fecContext.needsInit = true;
    fecContext.rows = rows;
    fecContext.columns = columns;
    fecContext.maxIndex = rows * columns;
    INFO_MSG("Enabling 2d-fec with %u rows and %u columns", rows, columns);
    return true;
  }

  void Packet::initFEC(uint64_t bufSize){
    fecContext.needsInit = false;
    fecContext.isFirst = true;
    fecContext.index = 0;
    fecContext.pktSize = bufSize;
    fecContext.lengthRecovery = bufSize - 12;
    // Add room for FEC and RTP header
    fecContext.rtpBufSize = fecContext.lengthRecovery + 28;
    // Add room for P, X, CC, M, PT, SN, TS fields
    fecContext.bitstringSize = fecContext.lengthRecovery + 8;
    fecContext.fecBufferRows.bitstring = 0;
    fecContext.fecBufferColumns.clear();
    fecContext.columnSN = 0;
    fecContext.rowSN = 0;
  }

  /// \brief Takes an RTP packet containing TS packets and returns the modified payload
  void Packet::generateBitstring(const char *payload, unsigned int payloadlen, uint8_t *bitstring){
    // Write 8 bits of header data (P, X, CC, M, PT, timestamp)
    bitstring[0] = data[0] & 0x3f;
    bitstring[1] = data[1];
    bitstring[2] = data[4];
    bitstring[3] = data[5];
    bitstring[4] = data[6];
    bitstring[5] = data[7];
    // Set length recovery
    bitstring[7] = fecContext.lengthRecovery;
    bitstring[6] = fecContext.lengthRecovery >> 8;
    // Append payload of RTP packet
    memcpy(bitstring + 8, payload, fecContext.lengthRecovery);
  }

  void Packet::applyXOR(const uint8_t *in1, const uint8_t *in2, uint8_t *out, uint64_t size){
    uint64_t index = 0;
    for (index = 0; index < size; index++) {
      out[index] = in1[index] ^ in2[index];
    }
  }

  /// \brief Sends buffered FEC packets
  /// \param socket UDP socket ready to send packets
  /// \param buf bitstring we want to contain in a FEC packet
  /// \param isColumn whether the buf we want to send represents a completed column or row
  void Packet::sendFec(void *socket, FecData *fecData, bool isColumn){
    uint8_t *data = fecData->bitstring;
    // Create zero filled buffer
    uint8_t *rtpBuf = (uint8_t *)malloc(fecContext.rtpBufSize);
    memset(rtpBuf, 0, fecContext.rtpBufSize);
    uint16_t thisSN = isColumn ? ++fecContext.columnSN : ++fecContext.rowSN;

    // V, P, X, CC
    rtpBuf[0] = 0x80 | (data[0] & 0x3f);
    // M, PT
    rtpBuf[1] = (data[1] & 0x80) | 0x60;
    // SN
    rtpBuf[3] = thisSN;
    rtpBuf[2] = thisSN >> 8;
    // TS
    rtpBuf[7] = fecData->timestamp;
    rtpBuf[6] = fecData->timestamp >> 8;
    rtpBuf[5] = fecData->timestamp >> 16;
    rtpBuf[4] = fecData->timestamp >> 24;
    // Keep SSRC 0 and skip CSRC

    // SNBase low (lowest sequence number of the sequence of RTP packets in this FEC packet)
    rtpBuf[13] = fecData->sequence;
    rtpBuf[12] = fecData->sequence >> 8;
    // Length recovery
    rtpBuf[14] = data[6];
    rtpBuf[15] = data[7];
    // E=1, PT recovery
    rtpBuf[16] = 0x80 | data[1];
    // Keep Mask 0
    // TS recovery
    rtpBuf[20] = data[2];
    rtpBuf[21] = data[3];
    rtpBuf[22] = data[4];
    rtpBuf[23] = data[5];
    // X=0, D, type=0, index=0
    rtpBuf[24] = isColumn ? 0x0 : 0x40;
    // offset (number of columns)
    rtpBuf[25] = isColumn ? fecContext.columns : 0x1;
    // NA (number of rows)
    rtpBuf[26] = isColumn ? fecContext.rows : fecContext.columns;
    // Keep SNBase ext bits 0
    // Payload
    memcpy(rtpBuf + 28, data + 8, fecContext.lengthRecovery);

    ((Socket::UDPConnection *)socket)->SendNow(reinterpret_cast<char*>(rtpBuf), fecContext.rtpBufSize);
    sentPackets++;
    sentBytes += fecContext.rtpBufSize;
    free(rtpBuf);
  }

  /// \brief Parses new RTP packets
  void Packet::parseFEC(void *columnSocket, void *rowSocket, uint64_t & bytesSent, const char *payload, unsigned int payloadlen){
    if (!fecEnabled){
      return;
    }
    uint8_t *bitstring;
    uint8_t thisColumn;
    uint8_t thisRow;
    // Check to see if we need to reinit FEC data
    if (fecContext.needsInit){
      // Add space for the RTP header
      initFEC(payloadlen + 12);
    }
    // Check the buffer size which should be constant
    if (payloadlen != fecContext.lengthRecovery){
      WARN_MSG("RTP packet size should be constant, expected %u but got %u", fecContext.lengthRecovery, payloadlen);
      return;
    }
    // Create bitstring
    bitstring = (uint8_t *)malloc(fecContext.pktSize);
    generateBitstring(payload, payloadlen, bitstring);

    thisColumn = fecContext.index % fecContext.columns;
    thisRow = (fecContext.index / fecContext.columns) % fecContext.rows;
    // Check for completed rows of data
    if (thisColumn == 0){
      // Double check if we have a final FEC row of data before sending it
      if (!fecContext.isFirst || fecContext.index > 0){
        if (thisRow == 0){
          INSANE_MSG("Sending completed FEC packet at row %u", fecContext.rows - 1);
        } else {
          INSANE_MSG("Sending completed FEC packet at row %u", thisRow - 1);
        }
        sendFec(rowSocket, &fecContext.fecBufferRows, false);
        bytesSent += fecContext.rtpBufSize;
      }
      free(fecContext.fecBufferRows.bitstring);
      fecContext.fecBufferRows.bitstring = bitstring;
      // Set the SN and TS of this first packet in the sequence
      fecContext.fecBufferRows.sequence = getSequence() - 1;
      fecContext.fecBufferRows.timestamp = getTimeStamp();
    } else {
      // This is an intermediate packet, apply XOR operation and continue
      applyXOR(fecContext.fecBufferRows.bitstring, bitstring, fecContext.fecBufferRows.bitstring, fecContext.bitstringSize);    
    }
    // XOR or set new bitstring
    if (thisRow == 0){
      // Make a copy if we are already using this bitstring for the FEC row
      if (thisColumn == 0){
        uint8_t *bitstringCopy;
        bitstringCopy = (uint8_t *)malloc(fecContext.pktSize);
        memcpy(bitstringCopy, bitstring, fecContext.pktSize);
        fecContext.fecBufferColumns[thisColumn].bitstring = bitstringCopy;
      } else {
        fecContext.fecBufferColumns[thisColumn].bitstring = bitstring;
      }
      fecContext.fecBufferColumns[thisColumn].sequence = getSequence() - 1;
      fecContext.fecBufferColumns[thisColumn].timestamp = getTimeStamp();
    } else {
      // This is an intermediate packet, apply XOR operation and continue
      applyXOR(fecContext.fecBufferColumns[thisColumn].bitstring, bitstring, fecContext.fecBufferColumns[thisColumn].bitstring, fecContext.bitstringSize);
    }

    // Check for completed columns of data
    if (thisRow == fecContext.rows - 1){
      INSANE_MSG("Sending completed FEC packet at column %u", thisColumn);
      sendFec(columnSocket, &fecContext.fecBufferColumns[thisColumn], true);
      bytesSent += fecContext.rtpBufSize;
      free(fecContext.fecBufferColumns[thisColumn].bitstring);
    }

    // Update variables
    fecContext.index++;
    if (fecContext.index >= fecContext.maxIndex){
      fecContext.isFirst = false;
      fecContext.index = 0;
    }
  }

  void Packet::sendNoPacket(unsigned int payloadlen){
    // Increment counters
    sentPackets++;
    sentBytes += payloadlen + getHsize();
    setTimestamp(Util::bootMS());
    increaseSequence();
  }

  void Packet::sendTS(void *socket, const char *payload, unsigned int payloadlen){
    // Add TS payload
    memcpy(data + getHsize(), payload, payloadlen);
    INSANE_MSG("Sending RTP packet with header size %u and payload size %u", getHsize(), payloadlen);
    // Set timestamp to current time
    setTimestamp(Util::bootMS()*90);
    // Send RTP packet itself
    ((Socket::UDPConnection *)socket)->SendNow(data, getHsize() + payloadlen);
    // Increment counters
    sentPackets++;
    sentBytes += payloadlen + getHsize();
    increaseSequence();
  }

  void Packet::sendH264(std::function<void(const char *, size_t)> callBack, const char *payload, uint32_t payloadlen,
                        bool lastOfAccesUnit){
    /// \todo This function probably belongs in DMS somewhere.
    if (payloadlen + getHsize() + 2 <= maxDataLen){
      data[1] &= 0x7F; // setting the RTP marker bit to 0
      if (lastOfAccesUnit){
        data[1] |= 0x80; // setting the RTP marker bit to 1
      }
      uint8_t nal_type = (payload[0] & 0x1F);
      if (nal_type < 1 || nal_type > 5){
        data[1] &= 0x7F; // but not for non-vlc types
      }
      memcpy(data + getHsize(), payload, payloadlen);
      callBack(data, getHsize() + payloadlen);
      sentPackets++;
      sentBytes += payloadlen + getHsize();
      increaseSequence();
    }else{
      data[1] &= 0x7F; // setting the RTP marker bit to 0
      unsigned int sent = 0;
      unsigned int sending = maxDataLen - getHsize() - 2; // packages are of size MAX_SEND, except for the final one
      char initByte = (payload[0] & 0xE0) | 0x1C;
      char serByte = payload[0] & 0x1F; // ser is now 000
      data[getHsize()] = initByte;
      while (sent < payloadlen){
        if (sent == 0){
          serByte |= 0x80; // set first bit to 1
        }else{
          serByte &= 0x7F; // set first bit to 0
        }
        if (sent + sending >= payloadlen){
          // last package
          serByte |= 0x40;
          sending = payloadlen - sent;
          if (lastOfAccesUnit){
            data[1] |= 0x80; // setting the RTP marker bit to 1
          }
        }
        data[getHsize() + 1] = serByte;
        memcpy(data + getHsize() + 2, payload + 1 + sent, sending);
        callBack(data, getHsize() + 2 + sending);
        sentPackets++;
        sentBytes += sending + getHsize() + 2;
        sent += sending;
        increaseSequence();
      }
    }
  }

  void Packet::sendAV1(std::function<void(const char *, size_t)> callBack, const char *payload, unsigned int payloadlen) {
    // Spec: https://aomediacodec.github.io/av1-rtp-spec/
    size_t payloadOffset = 0;
    size_t rtpPacketSize = 0;
    uint32_t headerSize = getHsize();
    bool isKeyframe = false;

    // Loop over payload, check for keyframe status
    while (!isKeyframe && payloadOffset < payloadlen) {
      AV1::OBU obu(payload + payloadOffset, payloadlen - payloadOffset);
      isKeyframe |= obu.isKeyframe();
      payloadOffset += obu.getSize();
    }
    // Reset payloadOffset so we can re-use it below
    payloadOffset = 0;

    // Set up first packet
    data[1] &= 0x7F; // unset marker bit
    data[headerSize] = 0x00; // reset aggregation header
    // Set keyframe flag if this is indeed a keyframe
    if (isKeyframe) { data[headerSize] |= 0x08; }
    rtpPacketSize = headerSize + 1;

    while (payloadlen > 0) {
      size_t OBUSize = AV1::OBU(payload + payloadOffset, payloadlen).getSize();
      DONTEVEN_MSG("RTP-packing AV1 OBU (T=%" PRIu32 "): %s", getTimeStamp(),
                   AV1::OBU(payload + payloadOffset, payloadlen).toString().c_str());

      payloadlen -= OBUSize;

      // Get the string encoding the full OBU length
      std::string obuSizeBytes = AV1::leb128(OBUSize);

      // Close to a full packet and this won't fit? Send now to encourage OBU alignment.
      if (rtpPacketSize + maxDataLen / 3 > maxDataLen && rtpPacketSize + OBUSize > maxDataLen) {
        callBack(data, rtpPacketSize);
        increaseSequence();
        sentBytes += rtpPacketSize;
        sentPackets++;
        // prepare new packet
        data[headerSize] = 0x00; // reset aggregation header
        rtpPacketSize = headerSize + 1;
      }

      // Write to packet and potentially send as long as we have more data to write
      while (OBUSize) {
        size_t writeLen = OBUSize;
        if (rtpPacketSize + writeLen + 4 > maxDataLen) { writeLen = maxDataLen - rtpPacketSize - 4; }

        // Copy in OBU size
        obuSizeBytes = AV1::leb128(writeLen);
        memcpy(data + rtpPacketSize, obuSizeBytes.data(), obuSizeBytes.size());
        rtpPacketSize += obuSizeBytes.size();
        // Copy in OBU payload
        memcpy(data + rtpPacketSize, payload + payloadOffset, writeLen);
        payloadOffset += writeLen;
        rtpPacketSize += writeLen;

        // Not a finished write? Send the packet and prepare a new one
        if (writeLen != OBUSize) {
          data[headerSize] |= 0x40; // mark last OBU incomplete
          callBack(data, rtpPacketSize);
          increaseSequence();
          sentBytes += rtpPacketSize;
          sentPackets++;
          // prepare new packet
          data[headerSize] = 0x00; // reset aggregation header
          data[headerSize] |= 0x80; // Not OBU aligned
          rtpPacketSize = headerSize + 1;
        }

        OBUSize -= writeLen;
      } // send loop within single OBU
    } // loop over each OBU

    // Send final packet
    data[1] |= 0x80; // set marker bit, we completed a frame
    callBack(data, rtpPacketSize);
    increaseSequence();
    sentBytes += rtpPacketSize;
    sentPackets++;
  }

  void Packet::sendVP8(std::function<void(const char *, size_t)> callBack, const char *payload, unsigned int payloadlen){

    bool isKeyframe = ((payload[0] & 0x01) == 0) ? true : false;
    bool isStartOfPartition = true;
    size_t chunkSize = MAX_SEND;
    size_t bytesWritten = 0;
    uint32_t headerSize = getHsize();

    while (payloadlen > 0){
      chunkSize = std::min<size_t>(1200, payloadlen);
      payloadlen -= chunkSize;

      data[1] = (0 != payloadlen) ? (data[1] & 0x7F) : (data[1] | 0x80); // marker bit, 1 for last chunk.
      data[headerSize] = 0x00;                                           // reset
      data[headerSize] |= (isStartOfPartition) ? 0x10 : 0x00; // first chunk is always start of a partition.
      data[headerSize] |= (isKeyframe) ? 0x00 : 0x20; // non-reference frame. 0 = frame is needed, 1 = frame can be discarded.

      memcpy(data + headerSize + 1, payload + bytesWritten, chunkSize);
      callBack(data, headerSize + 1 + chunkSize);
      increaseSequence();

      isStartOfPartition = false;
      bytesWritten += chunkSize;
      sentBytes += headerSize + 1 + chunkSize;
      sentPackets++;
    }
  }

  void Packet::sendVP9(std::function<void(const char *, size_t)> callBack, const char *pl, unsigned int len) {
    // Spec: https://datatracker.ietf.org/doc/html/rfc9628

    bool isKeyframe = false;
    if ((pl[0] & 0x30) == 0x30) {
      // profile 3 has an extra bit
      // show existing frame == 0 && frame_type == 0, then keyframe!
      isKeyframe = (!(pl[0] & 0x04) && !(pl[0] & 0x02));
    } else {
      // show existing frame == 0 && frame_type == 0, then keyframe!
      isKeyframe = (!(pl[0] & 0x08) && !(pl[0] & 0x04));
    }
    bool isStartOfPartition = true;
    size_t chunkSize = MAX_SEND;
    size_t bytesWritten = 0;
    uint32_t headerSize = getHsize();

    while (len > 0) {
      chunkSize = std::min<size_t>(1200, len);
      len -= chunkSize;

      data[1] = len ? (data[1] & 0x7F) : (data[1] | 0x80); // marker bit, 1 for last chunk.
      data[headerSize] = 0x00; // reset VP9 payload descriptor
      if (isStartOfPartition) { data[headerSize] |= 0x08; } // Begin of frame flag (B)
      if (!len) { data[headerSize] |= 0x04; } // End of frame flag (E)
      if (!isKeyframe) { data[headerSize] |= 0x40; } // Predicted frame flag (P)
      data[headerSize] |= (isKeyframe) ? 0x00 : 0x40; // predicted frame flag. 0 = no references, 1 = has references.

      memcpy(data + headerSize + 1, pl + bytesWritten, chunkSize);
      callBack(data, headerSize + 1 + chunkSize);
      increaseSequence();

      isStartOfPartition = false;
      bytesWritten += chunkSize;
      sentBytes += headerSize + 1 + chunkSize;
      sentPackets++;
    }
  }

  void Packet::sendH265(std::function<void(const char *, size_t)> callBack, const char *payload, unsigned int payloadlen){
    /// \todo This function probably belongs in DMS somewhere.
    if (payloadlen + getHsize() + 3 <= maxDataLen){
      data[1] |= 0x80; // setting the RTP marker bit to 1
      memcpy(data + getHsize(), payload, payloadlen);
      callBack(data, getHsize() + payloadlen);
      sentPackets++;
      sentBytes += payloadlen + getHsize();
      increaseSequence();
    }else{
      data[1] &= 0x7F; // setting the RTP marker bit to 0
      unsigned int sent = 0;
      unsigned int sending = maxDataLen - getHsize() - 3; // packages are of size MAX_SEND, except for the final one
      char initByteA = (payload[0] & 0x81) | 0x62;
      char initByteB = payload[1];
      char serByte = (payload[0] & 0x7E) >> 1; // SE is now 00
      data[getHsize()] = initByteA;
      data[getHsize() + 1] = initByteB;
      while (sent < payloadlen){
        if (sent == 0){
          serByte |= 0x80; // set first bit to 1
        }else{
          serByte &= 0x7F; // set first bit to 0
        }
        if (sent + sending >= payloadlen){
          // last package
          serByte |= 0x40;
          sending = payloadlen - sent;
          data[1] |= 0x80; // setting the RTP marker bit to 1
        }
        data[getHsize() + 2] = serByte;
        memcpy(data + getHsize() + 3, payload + 2 + sent, sending);
        callBack(data, getHsize() + 3 + sending);
        sentPackets++;
        sentBytes += sending + getHsize() + 3;
        sent += sending;
        increaseSequence();
      }
    }
  }

  void Packet::sendMPEG2(std::function<void(const char *, size_t)> callBack, const char *payload, unsigned int payloadlen){
    /// \todo This function probably belongs in DMS somewhere.
    if (payloadlen + getHsize() + 4 <= maxDataLen){
      data[1] |= 0x80; // setting the RTP marker bit to 1
      Mpeg::MPEG2Info mInfo = Mpeg::parseMPEG2Headers(payload, payloadlen);
      MPEGVideoHeader mHead(data + getHsize());
      mHead.clear();
      mHead.setTempRef(mInfo.tempSeq);
      mHead.setPictureType(mInfo.frameType);
      if (mInfo.isHeader){mHead.setSequence();}
      mHead.setBegin();
      mHead.setEnd();
      memcpy(data + getHsize() + 4, payload, payloadlen);
      callBack(data, getHsize() + payloadlen + 4);
      sentPackets++;
      sentBytes += payloadlen + getHsize() + 4;
      increaseSequence();
    }else{
      data[1] &= 0x7F; // setting the RTP marker bit to 0
      unsigned int sent = 0;
      unsigned int sending = maxDataLen - getHsize() - 4; // packages are of size MAX_SEND, except for the final one
      Mpeg::MPEG2Info mInfo;
      MPEGVideoHeader mHead(data + getHsize());
      while (sent < payloadlen){
        mHead.clear();
        if (sent + sending >= payloadlen){
          mHead.setEnd();
          sending = payloadlen - sent;
          data[1] |= 0x80; // setting the RTP marker bit to 1
        }
        Mpeg::parseMPEG2Headers(payload, sent + sending, mInfo);
        mHead.setTempRef(mInfo.tempSeq);
        mHead.setPictureType(mInfo.frameType);
        if (sent == 0){
          if (mInfo.isHeader){mHead.setSequence();}
          mHead.setBegin();
        }
        memcpy(data + getHsize() + 4, payload + sent, sending);
        callBack(data, getHsize() + 4 + sending);
        sentPackets++;
        sentBytes += sending + getHsize() + 4;
        sent += sending;
        increaseSequence();
      }
    }
  }

  void Packet::sendData(std::function<void(const char *, size_t)> callBack, const char *payload, unsigned int payloadlen, std::string codec){
    if (codec == "H264"){
      unsigned long sent = 0;
      const char * lastPtr = 0;
      size_t lastLen = 0;
      while (sent < payloadlen){
        unsigned long nalSize = ntohl(*((unsigned long *)(payload + sent)));
        // Since we skip filler data, we need to delay sending by one NAL unit to reliably
        // detect the end of the access unit.
        if ((payload[sent + 4] & 0x1F) != 12){
          // If we have a pointer stored, we know it's not the last one, so send it as non-last.
          if (lastPtr){sendH264(callBack, lastPtr, lastLen, false);}
          lastPtr = payload + sent + 4;
          lastLen = nalSize;
        }
        sent += nalSize + 4;
      }
      // Still a pointer stored? That means it was the last one. Mark it as such and send.
      if (lastPtr){sendH264(callBack, lastPtr, lastLen, true);}
      return;
    }
    if (codec == "AV1") {
      sendAV1(callBack, payload, payloadlen);
      return;
    }
    if (codec == "VP8"){
      sendVP8(callBack, payload, payloadlen);
      return;
    }
    if (codec == "VP9"){
      sendVP9(callBack, payload, payloadlen);
      return;
    }
    if (codec == "HEVC"){
      unsigned long sent = 0;
      while (sent < payloadlen){
        unsigned long nalSize = ntohl(*((unsigned long *)(payload + sent)));
        sendH265(callBack, payload + sent + 4, nalSize);
        sent += nalSize + 4;
      }
      return;
    }
    if (codec == "MPEG2"){
      sendMPEG2(callBack, payload, payloadlen);
      return;
    }
    /// \todo This function probably belongs in DMS somewhere.
    data[1] |= 0x80; // setting the RTP marker bit to 1
    size_t offsetLen = 0;
    if (codec == "AAC"){
      Bit::htobl(data + getHsize(), ((payloadlen << 3) & 0x0010fff8) | 0x00100000);
      offsetLen = 4;
    }else if (codec == "MP3" || codec == "MP2"){
      // See RFC 2250, "MPEG Audio-specific header"
      Bit::htobl(data + getHsize(), 0); // this is MBZ and Frag_Offset, which are always 0
      if (payload[0] != 0xFF){FAIL_MSG("MP2/MP3 data does not start with header?");}
      offsetLen = 4;
    }else if (codec == "AC3"){
      Bit::htobs(data + getHsize(),
                 1); // this is 6 bits MBZ, 2 bits FT = 0 = full frames and 8 bits saying we send 1 frame
      offsetLen = 2;
    }
    if (maxDataLen < getHsize() + offsetLen + payloadlen){
      if (!managed){
        FAIL_MSG("RTP data too big for packet, not sending!");
        return;
      }
      uint32_t newMaxLen = getHsize() + offsetLen + payloadlen;
      char *newData = new char[newMaxLen];
      if (newData){
        memcpy(newData, data, maxDataLen);
        delete[] data;
        data = newData;
        maxDataLen = newMaxLen;
      }
    }
    memcpy(data + getHsize() + offsetLen, payload, payloadlen);
    callBack(data, getHsize() + offsetLen + payloadlen);
    sentPackets++;
    sentBytes += payloadlen + offsetLen + getHsize();
    increaseSequence();
  }

  void Packet::sendRTCP_SR(std::function<void(const char *, size_t)> callBack){
    char *rtcpData = (char *)malloc(32);
    if (!rtcpData){
      FAIL_MSG("Could not allocate 32 bytes. Something is seriously messed up.");
      return;
    }
    rtcpData[0] = 0x80;                  // version 2, no padding, zero receiver reports
    rtcpData[1] = 200;                   // sender report
    Bit::htobs(rtcpData + 2, 6);         // 6 4-byte words follow the header
    Bit::htobl(rtcpData + 4, getSSRC()); // set source identifier

    Bit::htobll(rtcpData + 8, Util::getNTP());
    Bit::htobl(rtcpData + 16, getTimeStamp()); // rtpts
    // it should be the time packet was sent maybe, after all?
    //*((int *)(rtcpData+16) ) = htonl(getTimeStamp());//rtpts
    Bit::htobl(rtcpData + 20, sentPackets); // packet
    Bit::htobl(rtcpData + 24, sentBytes);   // octet
    callBack((char *)rtcpData, 28);
    free(rtcpData);
  }

  void Packet::sendRTCP_RR(SDP::Track &sTrk, std::function<void(const char *, size_t)> callBack){
    char *rtcpData = (char *)malloc(32);
    if (!rtcpData){
      FAIL_MSG("Could not allocate 32 bytes. Something is seriously messed up.");
      return;
    }
    if (!(sTrk.sorter.lostCurrent + sTrk.sorter.packCurrent)){sTrk.sorter.packCurrent++;}
    rtcpData[0] = 0x81;                       // version 2, no padding, one receiver report
    rtcpData[1] = 201;                        // receiver report
    Bit::htobs(rtcpData + 2, 7);              // 7 4-byte words follow the header
    Bit::htobl(rtcpData + 4, sTrk.mySSRC);    // set receiver identifier
    Bit::htobl(rtcpData + 8, sTrk.theirSSRC); // set source identifier
    rtcpData[12] = (sTrk.sorter.lostCurrent * 255) / (sTrk.sorter.lostCurrent + sTrk.sorter.packCurrent); // fraction lost since prev RR
    Bit::htob24(rtcpData + 13, sTrk.sorter.lostTotal); // cumulative packets lost since start
    Bit::htobl(rtcpData + 16, sTrk.sorter.rtpSeq | (sTrk.sorter.packTotal & 0xFFFF0000ul)); // highest sequence received
    Bit::htobl(rtcpData + 20, 0); /// \TODO jitter (diff in timestamp vs packet arrival)
    Bit::htobl(rtcpData + 24, 0); /// \TODO last SR (middle 32 bits of last SR or zero)
    Bit::htobl(rtcpData + 28, 0); /// \TODO delay since last SR in 2b seconds + 2b fraction
    callBack(rtcpData, 32);
    sTrk.sorter.lostCurrent = 0;
    sTrk.sorter.packCurrent = 0;
    free(rtcpData);
  }

  Packet::Packet(){
    managed = false;
    data = 0;
    maxDataLen = 0;
    sentBytes = 0;
    sentPackets = 0;
    fecEnabled = false;
  }

  Packet::Packet(uint32_t payloadType, uint32_t sequence, uint64_t timestamp, uint32_t ssrc, uint32_t csrcCount){
    managed = true;
    data = new char[12 + 4 * csrcCount + 2 + MAX_SEND]; // headerSize, 2 for FU-A, MAX_SEND for maximum sent size
    if (data){
      maxDataLen = 12 + 4 * csrcCount + 2 + MAX_SEND;
      data[0] = ((2) << 6) | ((0 & 1) << 5) | ((0 & 1) << 4) |
                (csrcCount & 15);   // version, padding, extension, csrc count
      data[1] = payloadType & 0x7F; // marker and payload type
    }else{
      maxDataLen = 0;
    }
    setSequence(sequence - 1); // we automatically increase the sequence each time when p
    setTimestamp(timestamp);
    setSSRC(ssrc);
    sentBytes = 0;
    sentPackets = 0;
    fecEnabled = false;
  }

  Packet::Packet(const Packet &o){
    managed = true;
    maxDataLen = 0;
    if (o.data && o.maxDataLen){
      data = new char[o.maxDataLen]; // headerSize, 2 for FU-A, MAX_SEND for maximum sent size
      if (data){
        maxDataLen = o.maxDataLen;
        memcpy(data, o.data, o.maxDataLen);
      }
    }else{
      data = new char[14 + MAX_SEND]; // headerSize, 2 for FU-A, MAX_SEND for maximum sent size
      if (data){
        maxDataLen = 14 + MAX_SEND;
        memset(data, 0, maxDataLen);
      }
    }
    sentBytes = o.sentBytes;
    sentPackets = o.sentPackets;
  }

  void Packet::operator=(const Packet &o){
    if (data && managed){delete[] data;}
    managed = true;
    maxDataLen = 0;
    data = 0;

    if (o.data && o.maxDataLen){
      data = new char[o.maxDataLen]; // headerSize, 2 for FU-A, MAX_SEND for maximum sent size
      if (data){
        maxDataLen = o.maxDataLen;
        memcpy(data, o.data, o.maxDataLen);
      }
    }else{
      data = new char[14 + MAX_SEND]; // headerSize, 2 for FU-A, MAX_SEND for maximum sent size
      if (data){
        maxDataLen = 14 + MAX_SEND;
        memset(data, 0, maxDataLen);
      }
    }
    sentBytes = o.sentBytes;
    sentPackets = o.sentPackets;
  }

  Packet::~Packet(){
    if (managed){delete[] data;}
  }
  Packet::Packet(const char *dat, uint64_t len){
    managed = false;
    maxDataLen = len;
    sentBytes = 0;
    sentPackets = 0;
    fecEnabled = false;
    data = (char *)dat;
  }

  /// Describes a packet in human-readable terms
  std::string Packet::toString() const{
    std::stringstream ret;
    ret << maxDataLen << "b RTP packet ";
    if (getMarker()){ret << "(marked) ";}
    ret << "payload type " << getPayloadType() << ", #" << getSequence() << ", @" << getTimeStamp();
    ret << " (" << getHsize() << "b header, " << getPayloadSize() << "b payload, " << getPadding() << "b padding)";
    return ret.str();
  }



  MPEGVideoHeader::MPEGVideoHeader(char *d){data = d;}

  uint16_t MPEGVideoHeader::getTotalLen() const{
    uint16_t ret = 4;
    if (data[0] & 0x08){
      ret += 4;
      if (data[4] & 0x40){ret += data[8];}
    }
    return ret;
  }

  std::string MPEGVideoHeader::toString() const{
    std::stringstream ret;
    uint32_t firstHead = Bit::btohl(data);
    ret << "TR=" << ((firstHead & 0x3FF0000) >> 16);
    if (firstHead & 0x4000000){ret << " Ext";}
    if (firstHead & 0x2000){ret << " SeqHead";}
    if (firstHead & 0x1000){ret << " SliceBegin";}
    if (firstHead & 0x800){ret << " SliceEnd";}
    ret << " PicType=" << ((firstHead & 0x700) >> 8);
    if (firstHead & 0x80){ret << " FBV";}
    ret << " BFC=" << ((firstHead & 0x70) >> 4);
    if (firstHead & 0x8){ret << " FFV";}
    ret << " FFC=" << (firstHead & 0x7);
    return ret.str();
  }

  void MPEGVideoHeader::clear(){((uint32_t *)data)[0] = 0;}

  void MPEGVideoHeader::setTempRef(uint16_t ref){
    data[0] |= (ref >> 8) & 0x03;
    data[1] = ref & 0xff;
  }

  void MPEGVideoHeader::setPictureType(uint8_t pType){data[2] |= pType & 0x7;}

  void MPEGVideoHeader::setSequence(){data[2] |= 0x20;}
  void MPEGVideoHeader::setBegin(){data[2] |= 0x10;}
  void MPEGVideoHeader::setEnd(){data[2] |= 0x8;}

  Sorter::Sorter(uint64_t trackId, void (*cb)(const uint64_t track, const Packet &p)){
    packTrack = trackId;
    rtpSeq = 0;
    rtpWSeq = 0;
    lostTotal = 0;
    lostCurrent = 0;
    packTotal = 0;
    packCurrent = 0;
    callback = cb;
    first = true;
    preBuffer = true;
    lastBootMS = 0;
    lastNTP = 0;
  }

  void Sorter::setCallback(uint64_t track, std::function<void(const uint64_t track, const Packet &p)> cb){
    callback = cb;
    packTrack = track;
  }

  /// Calls addPacket(pack) with a newly constructed RTP::Packet from the given arguments.
  void Sorter::addPacket(const char *dat, unsigned int len){addPacket(RTP::Packet(dat, len));}

  /// Takes in new RTP packets for a single track.
  /// Automatically sorts them, waiting when packets come in slow or not at all.
  /// Calls the callback with packets in sorted order, whenever it becomes possible to do so.
  void Sorter::addPacket(const Packet &pack){
    uint16_t pSNo = pack.getSequence();
    if (first){
      rtpWSeq = pSNo;
      rtpSeq = pSNo - 5;
      first = false;
    }
    DONTEVEN_MSG("Received packet #%u, current packet is #%u", pSNo, rtpSeq);
    if (preBuffer){
      //If we've buffered the first 5 packets, assume we have the first one known
      if (packBuffer.size() >= 5){
        preBuffer = false;
        rtpSeq = packBuffer.begin()->first;
        rtpWSeq = rtpSeq;
      }
    }else{
      // packet is very early - assume dropped after PACKET_DROP_TIMEOUT packets
      while ((int16_t)(rtpSeq - pSNo) < -(int)PACKET_DROP_TIMEOUT){
        VERYHIGH_MSG("Giving up on track %" PRIu64 " packet %u", packTrack, rtpSeq);
        ++rtpSeq;
        ++lostTotal;
        ++lostCurrent;
        ++packTotal;
        ++packCurrent;
      }
    }
    //Update wanted counter if we passed it (1 of 2)
    if ((int16_t)(rtpWSeq - rtpSeq) < 0){rtpWSeq = rtpSeq;}
    // packet is somewhat early - ask for packet after PACKET_REORDER_WAIT packets
    while ((int16_t)(rtpWSeq - pSNo) < -(int)PACKET_REORDER_WAIT){
      //Only wanted if we don't already have it
      if (!packBuffer.count(rtpWSeq)){
        wantedSeqs.insert(rtpWSeq);
      }
      ++rtpWSeq;
    }
    // send any buffered packets we may have
    uint16_t prertpSeq = rtpSeq;
    while (packBuffer.count(rtpSeq)){
      outPacket(packTrack, packBuffer[rtpSeq]);
      packBuffer.erase(rtpSeq);
      ++rtpSeq;
      ++packTotal;
      ++packCurrent;
    }
    if (prertpSeq != rtpSeq){
      INFO_MSG("Sent packets %" PRIu16 "-%" PRIu16 ", now %zu in buffer", prertpSeq, rtpSeq, packBuffer.size());
    }
    // packet is slightly early - buffer it
    if ((int16_t)(rtpSeq - pSNo) < 0){
      VERYHIGH_MSG("Buffering early packet #%u->%u", rtpSeq, pack.getSequence());
      packBuffer[pack.getSequence()] = pack;
    }
    // packet is late
    if ((int16_t)(rtpSeq - pSNo) > 0){
      // negative difference?
      //--lostTotal;
      //--lostCurrent;
      //++packTotal;
      //++packCurrent;
      //WARN_MSG("Dropped a packet that arrived too late! (%d packets difference)", (int16_t)(rtpSeq - pSNo));
      //return;
    }
    // packet is in order
    if (rtpSeq == pSNo){
      outPacket(packTrack, pack);
      ++rtpSeq;
      ++packTotal;
      ++packCurrent;
    }
    //Update wanted counter if we passed it (2 of 2)
    if ((int16_t)(rtpWSeq - rtpSeq) < 0){rtpWSeq = rtpSeq;}
  }

  toDTSC::toDTSC(){
    wrapArounds = 0;
    recentWrap = false;
    cbPack = 0;
    cbInit = 0;
    multiplier = 1.0;
    trackId = INVALID_TRACK_ID;
    firstTime = 0;
    packCount = 0;
    lastSeq = 0;
    vp8BufferHasKeyframe = false;
    curPicParameterSetId = 0;
  }

  void toDTSC::setProperties(const uint64_t track, const std::string &c, const std::string &t,
                             const std::string &i, const double m){
    trackId = track;
    codec = c;
    type = t;
    init = i;
    multiplier = m;
    if (codec == "HEVC" && init.size()){
      hevcInfo = h265::initData(init);
      h265::metaInfo MI = hevcInfo.getMeta();
      fps = MI.fps;
    }
    if (codec == "H264" && init.size()){
      MP4::AVCC avccbox;
      avccbox.setPayload(init);
      spsData.assign(avccbox.getSPS(), avccbox.getSPSLen());
      ppsData[curPicParameterSetId].assign(avccbox.getPPS(), avccbox.getPPSLen());
      h264::sequenceParameterSet sps(spsData.data(), spsData.size());
      if (sps) { fps = sps.chars.fps; }
    }
  }

  void toDTSC::setProperties(const DTSC::Meta &M, size_t tid){
    double m = (double)M.getRate(tid) / 1000.0;
    if (M.getType(tid) == "video" || M.getCodec(tid) == "MP2" || M.getCodec(tid) == "MP3"){
      m = 90.0;
    }
    if (M.getCodec(tid) == "opus"){
      m = 48.0;
    }
    setProperties(M.getID(tid), M.getCodec(tid), M.getType(tid), M.getInit(tid), m);
  }

  void toDTSC::setCallbacks(std::function<void(const DTSC::Packet &pkt)> cbP,
                            std::function<void(const uint64_t track, const std::string &initData)> cbI){
    cbPack = cbP;
    cbInit = cbI;
  }

  /// Improves A/V sync by providing an NTP time source
  /// msDiff is the amount of millis our current NTP time is ahead of the sync moment NTP time
  /// May be negative, if we're behind instead of ahead.
  void toDTSC::timeSync(uint32_t rtpTime, int64_t msDiff){
    if (!firstTime){return;}
    uint64_t rtp64Time = rtpTime;
    if (recentWrap){
      if (rtpTime > 0x80000000lu){rtp64Time -= 0x100000000ll;}
    }
    uint64_t msTime = (rtp64Time - firstTime + 1 + 0x100000000ull * wrapArounds) / multiplier + milliSync;
    int32_t rtpDiff = msTime - (Util::bootMS() - msDiff);
    if (rtpDiff > 25 || rtpDiff < -25){
      INFO_MSG("RTP difference (%s %s): %" PRId32 "ms, syncing...", type.c_str(), codec.c_str(), rtpDiff);
      milliSync -= rtpDiff;
    }

  }

  /// Adds an RTP packet to the converter, outputting DTSC packets and/or updating init data,
  /// as-needed.
  void toDTSC::addRTP(const RTP::Packet &pkt){
    if (pkt.getPayloadType() >= 72 && pkt.getPayloadType() <= 76){
      INFO_MSG("RTCP packet, ignoring for decoding");
      return;
    }
    if (codec.empty()){
      MEDIUM_MSG("Unknown codec - ignoring RTP packet.");
      return;
    }
    // First calculate the timestamp of the packet, get the pointer and length to RTP payload.
    // This part isn't codec-specific, so we do it before anything else.
    int64_t pTime = pkt.getTimeStamp();
    if (!firstTime){
      milliSync = Util::bootMS();
      firstTime = pTime + 1;
      INFO_MSG("RTP timestamp rollover for %" PRIu64 " (%s) expected in " PRETTY_PRINT_TIME, trackId, codec.c_str(),
               PRETTY_ARG_TIME((0xFFFFFFFFul - firstTime) / multiplier / 1000));
    }else{
      if (recentWrap){
        if (pTime < 0x80000000lu && pTime > 0x40000000lu){recentWrap = false;}
        if (pTime > 0x80000000lu){pTime -= 0x100000000ll;}
      }else{
        if (prevTime > pTime && pTime < 0x40000000lu && prevTime > 0x80000000lu){
          ++wrapArounds;
          INFO_MSG("RTP timestamp rollover %" PRIu32 " for %" PRIu64 " (%s) happened; next should be in " PRETTY_PRINT_TIME, wrapArounds, trackId, codec.c_str(), PRETTY_ARG_TIME((0xFFFFFFFFul) / multiplier / 1000));
          recentWrap = true;
        }
      }
    }
    // When there are B-frames, the firstTime can be higher than the current time
    //   causing msTime to become negative and thus overflow
    if (!wrapArounds && firstTime > pTime + 1){
      WARN_MSG("firstTime was higher than current packet time. Readjusting firstTime...");
      firstTime = pTime + 1;
    }
    prevTime = pkt.getTimeStamp();
    uint64_t msTime = ((uint64_t)pTime - firstTime + 1 + 0x100000000ull * wrapArounds) / multiplier + milliSync;
    char *pl = (char *)pkt.getPayload();
    uint32_t plSize = pkt.getPayloadSize();
    bool missed = lastSeq != (pkt.getSequence() - 1);
    lastSeq = pkt.getSequence();
    INSANE_MSG("Received RTP packet for track %" PRIu64 ", time %" PRIu32 " -> %" PRIu64, trackId,
               pkt.getTimeStamp(), msTime);
    // From here on, there is codec-specific parsing. We call handler functions for each codec,
    // except for the trivial codecs.
    if (codec == "H264"){
      return handleH264(msTime, pl, plSize, missed, false);
    }
    if (codec == "AAC"){return handleAAC(msTime, pl, plSize);}
    if (codec == "MP2" || codec == "MP3"){return handleMP2(msTime, pl, plSize);}
    if (codec == "HEVC"){return handleHEVC(msTime, pl, plSize, missed);}
    if (codec == "MPEG2"){return handleMPEG2(msTime, pl, plSize);}
    if (codec == "VP8"){
      return handleVP8(msTime, pl, plSize, missed, false);
    }
    if (codec == "VP9"){
      return handleVP8(msTime, pl, plSize, missed, false);
    }
    // Trivial codecs just fill a packet with raw data and continue. Easy peasy, lemon squeezy.
    if (codec == "ALAW" || codec == "opus" || codec == "PCM" || codec == "ULAW"){
      if (codec == "opus"){
        outInit(trackId, std::string("OpusHead\001\002\000\170\200\273\000\000\000\000\000", 19));
      }
      DTSC::Packet nextPack;
      nextPack.genericFill(msTime, 0, trackId, pl, plSize, 0, false);
      outPacket(nextPack);
      return;
    }
    // If we don't know how to handle this codec in RTP, print an error and ignore the packet.
    FAIL_MSG("Unimplemented RTP reader for codec `%s`! Throwing away packet.", codec.c_str());
  }

  void toDTSC::handleAAC(uint64_t msTime, char *pl, uint32_t plSize){
    if (plSize <= 2){
      WARN_MSG("Invalid AAC data: <= 2 bytes in length");
      return;
    }
    // assume AAC packets are single AU units
    /// \todo Support other input than single AU units
    unsigned int headLen = (Bit::btohs(pl) >> 3) + 2; // in bits, so /8, plus two for the prepended size
    DTSC::Packet nextPack;
    uint16_t samples = aac::AudSpecConf::samples(init);
    uint32_t sampleOffset = 0;
    uint32_t offset = 0;
    uint32_t auSize = 0;
    for (uint32_t i = 2; i < headLen && i + 2 < plSize; i += 2){
      auSize = Bit::btohs(pl + i) >> 3; // only the upper 13 bits
      if (auSize + headLen + offset > plSize){
        WARN_MSG("Invalid AAC data: continues beyond packet size");
        break;
      }
      nextPack.genericFill(msTime + sampleOffset / multiplier, 0, trackId, pl + headLen + offset,
                           std::min(auSize, plSize - headLen - offset), 0, false);
      offset += auSize;
      sampleOffset += samples;
      outPacket(nextPack);
    }
  }

  void toDTSC::handleMP2(uint64_t msTime, char *pl, uint32_t plSize){
    if (plSize < 5){
      WARN_MSG("Empty packet ignored!");
      return;
    }
    DTSC::Packet nextPack;
    nextPack.genericFill(msTime, 0, trackId, pl + 4, plSize - 4, 0, false);
    outPacket(nextPack);
  }

  void toDTSC::handleMPEG2(uint64_t msTime, char *pl, uint32_t plSize){
    if (plSize < 5){
      WARN_MSG("Empty packet ignored!");
      return;
    }
    ///\TODO Merge packets with same timestamp together
    HIGH_MSG("Received MPEG2 packet: %s", RTP::MPEGVideoHeader(pl).toString().c_str());
    DTSC::Packet nextPack;
    nextPack.genericFill(msTime, 0, trackId, pl + 4, plSize - 4, 0, false);
    outPacket(nextPack);
  }

  void toDTSC::handleHEVC(uint64_t msTime, char *pl, uint32_t plSize, bool missed){
    if (plSize < 2){
      WARN_MSG("Empty packet ignored!");
      return;
    }
    uint8_t nalType = (pl[0] & 0x7E) >> 1;
    if (nalType == 48){
      unsigned int pos = 2;
      while (pos + 2 < plSize){
        unsigned int pLen = Bit::btohs(pl + pos);
        VERYHIGH_MSG("AP Packet of %ub and type %s", pLen, h265::typeToStr((pl[pos + 2] & 0x7E) >> 1));
        if (packBuffer.allocate(4 + pLen)){
          Bit::htobl(packBuffer, pLen); // size-prepend
          memcpy(packBuffer + 4, pl + pos + 2, pLen);
          handleHEVCSingle(msTime, packBuffer, pLen + 4, h265::isKeyframe(pl + pos + 2, pLen));
        }
        pos += 2 + pLen;
      }
      return;
    }else if (nalType == 49){
      DONTEVEN_MSG("H265 Fragmentation Unit");
      // No length yet? Check for start bit. Ignore rest.
      if (!fuaBuffer.size() && (pl[2] & 0x80) == 0){
        HIGH_MSG("Not start of a new FU - throwing away");
        return;
      }
      if (fuaBuffer.size() && ((pl[2] & 0x80) || missed)){
        WARN_MSG("H265 FU packet incompleted: %zu", fuaBuffer.size());
        Bit::htobl(fuaBuffer, fuaBuffer.size() - 4); // size-prepend
        fuaBuffer[4] |= 0x80;                        // set error bit
        handleHEVCSingle(msTime, fuaBuffer, fuaBuffer.size(),
                         h265::isKeyframe(fuaBuffer + 4, fuaBuffer.size() - 4));
        fuaBuffer.truncate(0);
        return;
      }

      unsigned long len = plSize - 3;      // ignore the three FU bytes in front
      if (!fuaBuffer.size()){len += 6;}// six extra bytes for the first packet
      if (!fuaBuffer.allocate(fuaBuffer.size() + len)){return;}
      if (!fuaBuffer.size()){
        fuaBuffer.append("\000\000\000\000\000\000", 6);
        fuaBuffer.append(pl + 3, plSize - 3);
        // reconstruct first byte
        fuaBuffer[4] = ((pl[2] & 0x3F) << 1) | (pl[0] & 0x81);
        fuaBuffer[5] = pl[1];
      }else{
        fuaBuffer.append(pl+3, plSize-3);
      }

      if (pl[2] & 0x40){// last packet
        VERYHIGH_MSG("H265 FU packet type %s (%u) completed: %zu",
                     h265::typeToStr((fuaBuffer[4] & 0x7E) >> 1),
                     (uint8_t)((fuaBuffer[4] & 0x7E) >> 1), fuaBuffer.size());
        Bit::htobl(fuaBuffer, fuaBuffer.size() - 4); // size-prepend
        handleHEVCSingle(msTime, fuaBuffer, fuaBuffer.size(),
                         h265::isKeyframe(fuaBuffer + 4, fuaBuffer.size() - 4));
        fuaBuffer.truncate(0);
      }
    }else if (nalType == 50){
      ERROR_MSG("PACI/TSCI not supported yet");
    }else{
      DONTEVEN_MSG("%s NAL unit (%u)", h265::typeToStr(nalType), nalType);
      if (!packBuffer.allocate(plSize + 4)){return;}
      Bit::htobl(packBuffer, plSize); // size-prepend
      memcpy(packBuffer + 4, pl, plSize);
      handleHEVCSingle(msTime, packBuffer, plSize + 4, h265::isKeyframe(packBuffer + 4, plSize));
    }
  }

  void toDTSC::handleHEVCSingle(uint64_t ts, const char *buffer, const uint32_t len, bool isKey){
    MEDIUM_MSG("H265: %" PRIu64 "@%" PRIu64 ", %" PRIu32 "b%s", trackId, ts, len, isKey ? " (key)" : "");
    // Ignore zero-length packets (e.g. only contained init data and nothing else)
    if (!len){return;}

    // Header data? Compare to init, set if needed, and throw away
    uint8_t nalType = (buffer[4] & 0x7E) >> 1;
    switch (nalType){
    case 32: // VPS
    case 33: // SPS
    case 34: // PPS
      hevcInfo.addUnit(buffer);
      if (hevcInfo.haveRequired()){
        std::string newInit = hevcInfo.generateHVCC();
        if (newInit != init){
          init = newInit;
          outInit(trackId, init);
          h265::metaInfo MI = hevcInfo.getMeta();
          fps = MI.fps;
        }
      }
      return;
    default: // others, continue parsing
      break;
    }

    uint32_t offset = 0;
    uint64_t newTs = ts;
    if (fps > 1){
      // Assume a steady frame rate, clip the timestamp based on frame number.
      uint64_t frameNo = (ts / (1000.0 / fps)) + 0.5;
      if (frameNo < packCount){
        packCount = frameNo;
      }
      // More than 32 frames behind? We probably skipped something, somewhere...
      if ((frameNo - packCount) > 32){
        packCount = frameNo;
      }
      // After some experimentation, we found that the time offset is the difference between the
      // frame number and the packet counter, times the frame rate in ms
      offset = (frameNo - packCount) * (1000.0 / fps);
      //... and the timestamp is the packet counter times the frame rate in ms.
      newTs = packCount * (1000.0 / fps);
      VERYHIGH_MSG("Packing time %" PRIu64 " = %sframe %" PRIu64 " (%.2f FPS). Expected %" PRIu64
                   " -> +%" PRIu64 "/%" PRIu32,
                   ts, (isKey ? "key" : "i"), frameNo, fps, packCount, (frameNo - packCount), offset);
    }else{
      // For non-steady frame rate, assume no offsets are used and the timestamp is already correct
      VERYHIGH_MSG("Packing time %" PRIu64 " = %sframe %" PRIu64 " (variable rate)", ts,
                   isKey ? "key" : "i", packCount);
    }
    // Fill the new DTSC packet, buffer it.
    DTSC::Packet nextPack;
    nextPack.genericFill(newTs, offset, trackId, buffer, len, 0, isKey);
    packCount++;
    outPacket(nextPack);
  }

  /// Handles common H264 packets types, but not all.
  /// Generalizes and converts them all to a data format ready for DTSC, then calls handleH264Single
  /// for that data.
  /// Prints a WARN-level message if packet type is unsupported.
  /// \todo Support other H264 packets types?
  void toDTSC::handleH264(uint64_t msTime, char *pl, uint32_t plSize, bool missed, bool hasPadding){
    if (!plSize){
      WARN_MSG("Empty packet ignored!");
      return;
    }

    uint8_t num_padding_bytes = 0;
    if (hasPadding){
      num_padding_bytes = pl[plSize - 1];
      if (num_padding_bytes >= plSize){
        WARN_MSG("Only padding data (%u / %u).", num_padding_bytes, plSize);
        return;
      }
    }

    if ((pl[0] & 0x1F) == 0){
      WARN_MSG("H264 packet type null ignored");
      return;
    }
    if ((pl[0] & 0x1F) < 24){
      DONTEVEN_MSG("H264 single packet, type %u", (unsigned int)(pl[0] & 0x1F));
      if (!packBuffer.allocate(plSize + 4)){return;}
      Bit::htobl(packBuffer, plSize); // size-prepend
      memcpy(packBuffer + 4, pl, plSize);
      handleH264Single(msTime, packBuffer, plSize + 4, h264::isKeyframe(packBuffer + 4, plSize));
      return;
    }
    if ((pl[0] & 0x1F) == 24){
      DONTEVEN_MSG("H264 STAP-A packet");
      unsigned int pos = 1;
      while (pos + 1 < plSize){
        unsigned int pLen = Bit::btohs(pl + pos);
        INSANE_MSG("Packet of %ub and type %u", pLen, (unsigned int)(pl[pos + 2] & 0x1F));
        if (packBuffer.allocate(4 + pLen)){
          Bit::htobl(packBuffer, pLen); // size-prepend
          memcpy(packBuffer + 4, pl + pos + 2, pLen);
          handleH264Single(msTime, packBuffer, pLen + 4, h264::isKeyframe(pl + pos + 2, pLen));
        }
        pos += 2 + pLen;
      }
      return;
    }
    if ((pl[0] & 0x1F) == 28){
      DONTEVEN_MSG("H264 FU-A packet");
      // No length yet? Check for start bit. Ignore rest.
      if (!fuaBuffer.size() && (pl[1] & 0x80) == 0){
        HIGH_MSG("Not start of a new FU-A - throwing away");
        return;
      }
      if (fuaBuffer.size() && ((pl[1] & 0x80) || missed)){
        WARN_MSG("Ending unfinished FU-A");
        INSANE_MSG("H264 FU-A packet incompleted: %zu", fuaBuffer.size());
        fuaBuffer.truncate(0);
        return;
      }

      unsigned long len = plSize - 2;      // ignore the two FU-A bytes in front
      if (!fuaBuffer.size()){len += 5;}// five extra bytes for the first packet
      if (!fuaBuffer.allocate(fuaBuffer.size() + len)){return;}
      if (!fuaBuffer.size()){
        fuaBuffer.append("\000\000\000\000", 4);
        fuaBuffer.append(pl + 1, plSize - 1);
        // reconstruct first byte
        fuaBuffer[4] = (fuaBuffer[4] & 0x1F) | (pl[0] & 0xE0);
      }else{
        fuaBuffer.append(pl+2, plSize-2);
      }

      if (pl[1] & 0x40){// last packet
        INSANE_MSG("H264 FU-A packet type %u completed: %zu", (unsigned int)(fuaBuffer[4] & 0x1F),
                   fuaBuffer.size());
        uint8_t nalType = (fuaBuffer[4] & 0x1F);
        if (nalType == 7 || nalType == 8){
          // attempt to detect multiple H264 packets, even though specs disallow it
          handleH264Multi(msTime, fuaBuffer, fuaBuffer.size());
        }else{
          Bit::htobl(fuaBuffer, fuaBuffer.size() - 4); // size-prepend
          handleH264Single(msTime, fuaBuffer, fuaBuffer.size(),
                           h264::isKeyframe(fuaBuffer + 4, fuaBuffer.size() - 4));
        }
        fuaBuffer.truncate(0);
      }
      return;
    }
    WARN_MSG("H264 packet type %u unsupported", (unsigned int)(pl[0] & 0x1F));
  }

  void toDTSC::handleH264Single(uint64_t ts, const char *buffer, const uint32_t len, bool isKey){
    DONTEVEN_MSG("H264: %" PRIu64 "@%" PRIu64 ", %" PRIu32 "b%s", trackId, ts, len, isKey ? " (key)" : "");
    // Ignore zero-length packets (e.g. only contained init data and nothing else)
    if (!len){return;}

    // Header data? Compare to init, set if needed, and throw away
    uint8_t nalType = (buffer[4] & 0x1F);
    if (nalType == 9 && len < 20){return;}// ignore delimiter-only packets
    if (!h264OutBuffer.size()){
      currH264Time = ts;
      h264BufferWasKey = isKey;
    }

    //Send an outPacket every time the timestamp updates
    if (currH264Time != ts){
      //calculate the "packet" (which might be more than one actual packet) timestamp
      uint32_t offset = 0;
      uint64_t newTs = currH264Time;

      if (fps > 1){
        // Assume a steady frame rate, clip the timestamp based on frame number.
        uint64_t frameNo = (currH264Time / (1000.0 / fps)) + 0.5;
        if (frameNo < packCount){
          packCount = frameNo;
        }
        // More than 32 frames behind? We probably skipped something, somewhere...
        if ((frameNo - packCount) > 32){
          packCount = frameNo;
        }
        // After some experimentation, we found that the time offset is the difference between the
        // frame number and the packet counter, times the frame rate in ms
        offset = (frameNo - packCount) * (1000.0 / fps);
        //... and the timestamp is the packet counter times the frame rate in ms.
        newTs = packCount * (1000.0 / fps);
        VERYHIGH_MSG("Packing time %" PRIu64 " = %sframe %" PRIu64 " (%.2f FPS). Expected %" PRIu64
                     " -> +%" PRIu64 "/%" PRIu32,
                     ts, isKey ? "key" : "i", frameNo, fps, packCount, (frameNo - packCount), offset);
      }else{
        // For non-steady frame rate, assume no offsets are used and the timestamp is already
        // correct
        VERYHIGH_MSG("Packing time %" PRIu64 " = %sframe %" PRIu64 " (variable rate)", currH264Time,
                     isKey ? "key" : "i", packCount);
      }
      // Fill the new DTSC packet, buffer it.
      DTSC::Packet nextPack;
      nextPack.genericFill(newTs, offset, trackId, h264OutBuffer, h264OutBuffer.size(), 0, h264BufferWasKey);
      packCount++;
      outPacket(nextPack);

      //Clear the buffers, reset the time to current
      h264OutBuffer.assign(0, 0);
      currH264Time = ts;
      h264BufferWasKey = isKey;
    }
    h264BufferWasKey |= isKey;


    switch (nalType){
    case 6: // SEI
      return;
    case 7: // SPS
      if (spsData.size() != len - 4 || memcmp(buffer + 4, spsData.data(), len - 4) != 0) {
        h264::sequenceParameterSet sps(buffer + 4, len - 4);
        if (!sps) {
          WARN_MSG("Ignoring invalid SPS packet! (%" PRIu32 "b)", len - 4);
          return;
        }
        HIGH_MSG("Updated SPS from RTP data: %" PRIu32 "b", len - 4);
        spsData.assign(buffer + 4, len - 4);
        fps = sps.chars.fps;
      }
      return;
    case 8: // PPS
      // Determine pic_parameter_set_id and check whether the PPS is new or updated
      {
        h264::ppsUnit PPS(buffer + 4, len - 4);
        if (ppsData[PPS.picParameterSetId].size() != len - 4 || memcmp(buffer + 4, ppsData[PPS.picParameterSetId].data(), len - 4) != 0){
          if (!h264::ppsValidate(buffer+4, len-4)){
            WARN_MSG("Ignoring invalid PPS packet! (%" PRIu32 "b)", len-4);
            return;
          }
          HIGH_MSG("Updated PPS with ID %" PRIu64 " from RTP data", PPS.picParameterSetId);
          ppsData[PPS.picParameterSetId].assign(buffer + 4, len - 4);
        }
      }
      return;
    case 5:{
      // We have a keyframe: prepend SPS/PPS if the pic_parameter_set_id changed or if this is the first keyframe
      h264::codedSliceUnit keyPiece(buffer + 4, len - 4);
      if (spsData.size() && ppsData.size() && (!h264OutBuffer.size() || keyPiece.picParameterSetId != curPicParameterSetId)) {
        curPicParameterSetId = keyPiece.picParameterSetId;
        // Update meta init data if needed
        MP4::AVCC avccBox;
        avccBox.setVersion(1);
        avccBox.setProfile(spsData[1]);
        avccBox.setCompatibleProfiles(spsData[2]);
        avccBox.setLevel(spsData[3]);
        avccBox.setSPSCount(1);
        avccBox.setSPS(spsData);
        avccBox.setPPSCount(1);
        avccBox.setPPS(ppsData[curPicParameterSetId]);
        std::string newInit = std::string(avccBox.payload(), avccBox.payloadSize());
        if (newInit != init){
          init = newInit;
          outInit(trackId, init);
        }
        // Prepend SPS/PPS
        char sizeBuffer[4];
        Bit::htobl(sizeBuffer, spsData.size());
        h264OutBuffer.append(sizeBuffer, 4);
        h264OutBuffer.append(spsData.data(), spsData.size());
        Bit::htobl(sizeBuffer, ppsData[curPicParameterSetId].size());
        h264OutBuffer.append(sizeBuffer, 4);
        h264OutBuffer.append(ppsData[curPicParameterSetId].data(), ppsData[curPicParameterSetId].size());
      }
      //Note: no return, we still want to buffer the packet itself, below!
    }
    default: // others, continue parsing
      break;
    }

    //Buffer the packet
    h264OutBuffer.append(buffer, len);

  }

  /// Handles a single H264 packet, checking if others are appended at the end in Annex B format.
  /// If so, splits them up and calls handleH264Single for each. If not, calls it only once for the
  /// whole payload.
  void toDTSC::handleH264Multi(uint64_t ts, char *buffer, const uint32_t len){
    uint32_t lastStart = 0;
    for (uint32_t i = 0; i < len - 4; ++i){
      // search for start code
      if (buffer[i] == 0 && buffer[i + 1] == 0 && buffer[i + 2] == 0 && buffer[i + 3] == 1){
        // if found, handle a packet from the last start code up to this start code
        Bit::htobl(buffer + lastStart, (i - lastStart - 1) - 4); // size-prepend
        handleH264Single(ts, buffer + lastStart, (i - lastStart - 1),
                         h264::isKeyframe(buffer + lastStart + 4, i - lastStart - 5));
        lastStart = i;
      }
    }
    // Last packet (might be first, if no start codes found)
    Bit::htobl(buffer + lastStart, (len - lastStart) - 4); // size-prepend
    handleH264Single(ts, buffer + lastStart, (len - lastStart),
                     h264::isKeyframe(buffer + lastStart + 4, len - lastStart - 4));
  }

  void toDTSC::handleVP8(uint64_t msTime, const char *buffer, const uint32_t len, bool missed, bool hasPadding){

    // 1 byte is required but we assume that there some payload
    // data too :P
    if (len < 3){
      FAIL_MSG("Received a VP8 RTP packet with invalid size.");
      return;
    }

    // it may happen that we receive a packet with only padding
    // data. (against the spec I think) Although `drno` from
    // Mozilla told me these are probing packets and should be
    // ignored.
    uint8_t num_padding_bytes = 0;
    if (hasPadding){
      num_padding_bytes = buffer[len - 1];
      if (num_padding_bytes >= len){
        WARN_MSG("Only padding data (%u/%u)", num_padding_bytes, len);
        return;
      }
    }

    // parse the vp8 payload descriptor, https://tools.ietf.org/html/rfc7741#section-4.2
    uint8_t extended_control_bits = (buffer[0] & 0x80) >> 7;
    uint8_t start_of_partition = (buffer[0] & 0x10) >> 4;
    uint8_t partition_index = (buffer[0] & 0x07);

    uint32_t vp8_header_size = 1;
    vp8_header_size += extended_control_bits;

    if (extended_control_bits == 1){

      uint8_t pictureid_present = (buffer[1] & 0x80) >> 7;
      uint8_t tl0picidx_present = (buffer[1] & 0x40) >> 6;
      uint8_t tid_present = (buffer[1] & 0x20) >> 5;
      uint8_t keyidx_present = (buffer[1] & 0x10) >> 4;

      uint8_t has_extended_pictureid = 0;
      if (pictureid_present == 1){has_extended_pictureid = (buffer[2] & 0x80) > 7;}

      vp8_header_size += pictureid_present;
      vp8_header_size += tl0picidx_present;
      vp8_header_size += ((tid_present == 1 || keyidx_present == 1)) ? 1 : 0;
      vp8_header_size += has_extended_pictureid;
    }

    if (vp8_header_size > len){
      FAIL_MSG("The vp8 header size exceeds the RTP packet size. Invalid size.");
      return;
    }

    const char *vp8_payload_buffer = buffer + vp8_header_size;
    uint32_t vp8_payload_size = len - vp8_header_size;
    bool start_of_frame = (start_of_partition == 1) && (partition_index == 0);

    if (hasPadding){
      if (num_padding_bytes > vp8_payload_size){
        FAIL_MSG("More padding bytes than payload bytes. Invalid.");
        return;
      }

      vp8_payload_size -= num_padding_bytes;
      if (vp8_payload_size == 0){
        WARN_MSG("No payload data at all, only required VP8 header.");
        return;
      }
    }

    // when we have data in our buffer and the current packet is
    // for a new frame started or we missed some data
    // (e.g. only received the first partition of a frame) we will
    // flush a new DTSC packet.
    if (vp8FrameBuffer.size()){
      // new frame and nothing missed? Send.
      if (start_of_frame && !missed){
        DTSC::Packet nextPack;
        nextPack.genericFill(msTime, 0, trackId, vp8FrameBuffer, vp8FrameBuffer.size(), 0, vp8BufferHasKeyframe);
        packCount++;
        outPacket(nextPack);
      }
      // Wipe the buffer clean if missed packets or we just sent data out.
      if (start_of_frame || missed){
        vp8FrameBuffer.assign(0, 0);
        vp8BufferHasKeyframe = false;
      }
    }

    // copy the data into the buffer. assign() will write the
    // buffer from the start, append() appends the data to the
    // end of the previous buffer.
    if (vp8FrameBuffer.size() == 0){
      if (!start_of_frame){
        FAIL_MSG("Skipping packet; not start of partition (%u).", partition_index);
        return;
      }
      if (!vp8FrameBuffer.assign(vp8_payload_buffer, vp8_payload_size)){
        FAIL_MSG("Failed to assign vp8 buffer data.");
      }
    }else{
      vp8FrameBuffer.append(vp8_payload_buffer, vp8_payload_size);
    }

    bool is_keyframe = (vp8_payload_buffer[0] & 0x01) == 0;
    if (start_of_frame && is_keyframe){vp8BufferHasKeyframe = true;}
  }
}// namespace RTP
