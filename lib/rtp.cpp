#include <arpa/inet.h>
#include "rtp.h"
#include "timing.h"
#include "defines.h"

#define MAX_SEND 1024*4

namespace RTP {
  double Packet::startRTCP = 0;

  unsigned int Packet::getHsize() const {
    return 12 + 4 * getContribCount();
  }

  unsigned int Packet::getVersion() const {
    return (data[0] >> 6) & 0x3;
  }

  unsigned int Packet::getPadding() const {
    return (data[0] >> 5) & 0x1;
  }

  unsigned int Packet::getExtension() const {
    return (data[0] >> 4) & 0x1;
  }

  unsigned int Packet::getContribCount() const {
    return (data[0]) & 0xE;
  }

  unsigned int Packet::getMarker() const {
    return (data[1] >> 7) & 0x1;
  }

  unsigned int Packet::getPayloadType() const {
    return (data[1]) & 0x7F;
  }

  unsigned int Packet::getSequence() const {
    return (((((unsigned int)data[2]) << 8) + data[3]));
  }

  unsigned int Packet::getTimeStamp() const {
    return ntohl(*((unsigned int *)(data + 4)));
  }

  unsigned int Packet::getSSRC() const {
    return ntohl(*((unsigned int *)(data + 8)));
  }

  char * Packet::getData() {
    return data + 8 + 4 * getContribCount() + getExtension();
  }

  void Packet::setTimestamp(unsigned int t) {
    *((unsigned int *)(data + 4)) = htonl(t);
  }

  void Packet::setSequence(unsigned int seq) {
    *((short *)(data + 2)) = htons(seq);
  }

  void Packet::setSSRC(unsigned long ssrc) {
    *((int *)(data + 8)) = htonl(ssrc);
  }

  void Packet::increaseSequence() {
    *((short *)(data + 2)) = htons(getSequence() + 1);
  }

  void Packet::sendH264(void * socket, void callBack(void *, char *, unsigned int, unsigned int), const char * payload, unsigned int payloadlen, unsigned int channel) {
    /// \todo This function probably belongs in DMS somewhere.
    if (payloadlen <= MAX_SEND) {
      data[1] |= 0x80;//setting the RTP marker bit to 1
      memcpy(data + getHsize(), payload, payloadlen);
      callBack(socket, data, getHsize() + payloadlen, channel);
      sentPackets++;
      sentBytes += payloadlen;
      increaseSequence();
    } else {
      data[1] &= 0x7F;//setting the RTP marker bit to 0
      unsigned int sent = 0;
      unsigned int sending = MAX_SEND;//packages are of size MAX_SEND, except for the final one
      char initByte = (payload[0] & 0xE0) | 0x1C;
      char serByte = payload[0] & 0x1F; //ser is now 000
      data[getHsize()] = initByte;
      while (sent < payloadlen) {
        if (sent == 0) {
          serByte |= 0x80;//set first bit to 1
        } else {
          serByte &= 0x7F;//set first bit to 0
        }
        if (sent + MAX_SEND >= payloadlen) {
          //last package
          serByte |= 0x40;
          sending = payloadlen - sent;
          data[1] |= 0x80;//setting the RTP marker bit to 1
        }
        data[getHsize() + 1] = serByte;
        memcpy(data + getHsize() + 2, payload + 1 + sent, sending); //+1 because
        callBack(socket, data, getHsize() + 2 + sending, channel);
        sentPackets++;
        sentBytes += sending;
        sent += sending;
        increaseSequence();
      }
    }
  }

  void Packet::sendData(void * socket, void callBack(void *, char *, unsigned int, unsigned int), const char * payload, unsigned int payloadlen, unsigned int channel, std::string codec) {
    /// \todo This function probably belongs in DMS somewhere.
    data[1] |= 0x80;//setting the RTP marker bit to 1
    long offsetLen = 0;
    if (codec == "AAC"){
      INFO_MSG("send AAC codec");
      *((long *)(data + getHsize())) = htonl(((payloadlen << 3) & 0x0010fff8) | 0x00100000);
      offsetLen = 4;
    }else if (codec == "MP3"){
      INFO_MSG("send MP3 codec");
      *((long *)(data + getHsize())) = 0;//this is MBZ and Frag_Offset, which is always 0
      offsetLen = 4;
    }else if (codec == "AC3"){
      INFO_MSG("send AC3 codec");
      *((short *)(data + getHsize())) = htons(0x0001) ;//this is 6 bits MBZ, 2 bits FT = 0 = full frames and 8 bits saying we send 1 frame
      offsetLen = 2;
    }else{
      INFO_MSG("send Raw");
    }
    memcpy(data + getHsize() + offsetLen, payload, payloadlen);
    callBack(socket, data, getHsize() + offsetLen + payloadlen, channel);
    sentPackets++;
    sentBytes += payloadlen;
    increaseSequence();
  }
  
/// Stores a long long (64 bits) value of val in network order to the pointer p.
  inline void Packet::htobll(char * p, long long val) {
    p[0] = (val >> 56) & 0xFF;
    p[1] = (val >> 48) & 0xFF;
    p[2] = (val >> 40) & 0xFF;
    p[3] = (val >> 32) & 0xFF;
    p[4] = (val >> 24) & 0xFF;
    p[5] = (val >> 16) & 0xFF;
    p[6] = (val >> 8) & 0xFF;
    p[7] = val & 0xFF;
  }



  void Packet::sendRTCP(long long & connectedAt, void * socket, unsigned int tid , DTSC::Meta & metadata, void callBack(void *, char *, unsigned int, unsigned int)) {
    void * rtcpData = malloc(32);
    if (!rtcpData){
      FAIL_MSG("Could not allocate 32 bytes. Something is seriously messed up.");
      return;
    }
    ((int *)rtcpData)[0] = htonl(0x80C80006);
    ((int *)rtcpData)[1] = htonl(getSSRC());
    // unsigned int tid = packet["trackid"].asInt();
    //timestamp in ms
    double ntpTime = 2208988800UL + Util::epoch() + (Util::getMS() % 1000) / 1000.0;
    if (startRTCP < 1 && startRTCP > -1) {
      startRTCP = ntpTime;
    }
    ntpTime -= startRTCP;
    
    ((int *)rtcpData)[2] = htonl(2208988800UL + Util::epoch()); //epoch is in seconds
    ((int *)rtcpData)[3]  = htonl((Util::getMS() % 1000) * 4294967.295);
    if (metadata.tracks[tid].codec == "H264" || metadata.tracks[tid].codec == "MP3") {
      ((int *)rtcpData)[4] = htonl((ntpTime - 0) * 90000); //rtpts
    } else if (metadata.tracks[tid].codec == "AAC" || metadata.tracks[tid].codec == "AC3") {
      ((int *)rtcpData)[4] = htonl((ntpTime - 0) * metadata.tracks[tid].rate); //rtpts
    } else {
      DEBUG_MSG(DLVL_FAIL, "Unsupported codec: %s", metadata.tracks[tid].codec.c_str());
      return;
    }
    //it should be the time packet was sent maybe, after all?
    //*((int *)(rtcpData+16) ) = htonl(getTimeStamp());//rtpts
    ((int *)rtcpData)[5] = htonl(sentPackets);//packet
    ((int *)rtcpData)[6] = htonl(sentBytes);//octet
    callBack(socket, (char*)rtcpData , 28 , 0);
    free(rtcpData);
  }

  Packet::Packet() {
    managed = false;
    data = 0;
  }

  Packet::Packet(unsigned int payloadType, unsigned int sequence, unsigned int timestamp, unsigned int ssrc, unsigned int csrcCount) {
    managed = true;
    data = new char[12 + 4 * csrcCount + 2 + MAX_SEND]; //headerSize, 2 for FU-A, MAX_SEND for maximum sent size
    data[0] = ((2) << 6) | ((0 & 1) << 5) | ((0 & 1) << 4) | (csrcCount & 15); //version, padding, extension, csrc count
    data[1] = payloadType & 0x7F; //marker and payload type
    setSequence(sequence - 1); //we automatically increase the sequence each time when p
    setTimestamp(timestamp);
    setSSRC(ssrc);
    sentBytes = 0;
    sentPackets = 0;
  }

  Packet::Packet(const Packet & o) {
    managed = true;
    if (o.data) {
      data = new char[o.getHsize() + 2 + MAX_SEND]; //headerSize, 2 for FU-A, MAX_SEND for maximum sent size
      if (data) {
        memcpy(data, o.data, o.getHsize() + 2 + MAX_SEND);
      }
    } else {
      data = new char[14 + MAX_SEND];//headerSize, 2 for FU-A, MAX_SEND for maximum sent size
      if (data) {
        memset(data, 0, 14 + MAX_SEND);
      }
    }
    sentBytes = o.sentBytes;
    sentPackets = o.sentPackets;
  }

  void Packet::operator=(const Packet & o) {
    managed = true;
    if (data) {
      delete[] data;
    }
    data = new char[o.getHsize() + 2 + MAX_SEND];
    if (data) {
      memcpy(data, o.data, o.getHsize() + 2 + MAX_SEND);
    }
    sentBytes = o.sentBytes;
    sentPackets = o.sentPackets;
  }

  Packet::~Packet() {
    if (managed) {
      delete [] data;
    }
  }
  Packet::Packet(const char * dat, unsigned int len) {
    managed = false;
    datalen = len;
    data = (char *) dat;
  }

}
