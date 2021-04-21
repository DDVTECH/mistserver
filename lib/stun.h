#pragma once
#include <netinet/in.h>
#include <stdint.h>
#include <string>
#include <vector>

/* --------------------------------------- */

#define STUN_IP4 0x01
#define STUN_IP6 0x02

#define STUN_MSG_TYPE_NONE 0x0000
#define STUN_MSG_TYPE_BINDING_REQUEST 0x0001
#define STUN_MSG_TYPE_BINDING_RESPONSE_SUCCESS 0x0101
#define STUN_MSG_TYPE_BINDING_RESPONSE_ERROR 0x0111
#define STUN_MSG_TYPE_BINDING_INDICATION 0x0011

#define STUN_ATTR_TYPE_NONE 0x0000
#define STUN_ATTR_TYPE_MAPPED_ADDR 0x0001
#define STUN_ATTR_TYPE_CHANGE_REQ 0x0003
#define STUN_ATTR_TYPE_USERNAME 0x0006
#define STUN_ATTR_TYPE_MESSAGE_INTEGRITY 0x0008
#define STUN_ATTR_TYPE_ERR_CODE 0x0009
#define STUN_ATTR_TYPE_UNKNOWN_ATTRIBUTES 0x000a
#define STUN_ATTR_TYPE_CHANNEL_NUMBER 0x000c
#define STUN_ATTR_TYPE_LIFETIME 0x000d
#define STUN_ATTR_TYPE_XOR_PEER_ADDR 0x0012
#define STUN_ATTR_TYPE_DATA 0x0013
#define STUN_ATTR_TYPE_REALM 0x0014
#define STUN_ATTR_TYPE_NONCE 0x0015
#define STUN_ATTR_TYPE_XOR_RELAY_ADDRESS 0x0016
#define STUN_ATTR_TYPE_REQ_ADDRESS_FAMILY 0x0017
#define STUN_ATTR_TYPE_EVEN_PORT 0x0018
#define STUN_ATTR_TYPE_REQUESTED_TRANSPORT 0x0019
#define STUN_ATTR_TYPE_DONT_FRAGMENT 0x001a
#define STUN_ATTR_TYPE_XOR_MAPPED_ADDRESS 0x0020
#define STUN_ATTR_TYPE_RESERVATION_TOKEN 0x0022
#define STUN_ATTR_TYPE_PRIORITY 0x0024
#define STUN_ATTR_TYPE_USE_CANDIDATE 0x0025
#define STUN_ATTR_TYPE_PADDING 0x0026
#define STUN_ATTR_TYPE_RESPONSE_PORT 0x0027
#define STUN_ATTR_TYPE_SOFTWARE 0x8022
#define STUN_ATTR_TYPE_ALTERNATE_SERVER 0x8023
#define STUN_ATTR_TYPE_FINGERPRINT 0x8028
#define STUN_ATTR_TYPE_ICE_CONTROLLED 0x8029
#define STUN_ATTR_TYPE_ICE_CONTROLLING 0x802a
#define STUN_ATTR_TYPE_RESPONSE_ORIGIN 0x802b
#define STUN_ATTR_TYPE_OTHER_ADDRESS 0x802c

/* --------------------------------------- */

std::string stun_message_type_to_string(uint16_t type);
std::string stun_attribute_type_to_string(uint16_t type);
std::string stun_family_type_to_string(uint8_t type);

/*
   Compute the hmac-sha1 over message.
   uint8_t* message:  the data over which we compute the hmac sha
   uint32_t nbytes:   the number of bytse in message
   std::string key:   key to use for hmac
   uint8_t* output:   we write the sha1 into this buffer.
*/
int stun_compute_hmac_sha1(uint8_t *message, uint32_t nbytes, std::string key, uint8_t *output);

/*
   Compute the Message-Integrity of a stun message.
   This will not change the given buffer.

   std::vector<uint8_t>& buffer: the buffer that contains a valid stun message
   std::string key:              key to use for hmac
   uint8_t* output:              will be filled with the correct hmac-sha1 of that represents the integrity message value.
*/
int stun_compute_message_integrity(std::vector<uint8_t> &buffer, std::string key, uint8_t *output);

/*
   Compute the fingerprint value for the stun message.
   This will not change the given buffer.
   std::vector<uint8_t>& buffer:    the buffer that contains a valid stun message.
   uint32_t& result:                will be set to the calculated crc value.
*/
int stun_compute_fingerprint(std::vector<uint8_t> &buffer, uint32_t &result);

/* --------------------------------------- */

/* https://tools.ietf.org/html/rfc5389#section-15.10 */
class StunAttribSoftware{
public:
  char *value;
};

class StunAttribFingerprint{
public:
  uint32_t value;
};

/* https://tools.ietf.org/html/rfc5389#section-15.4 */
class StunAttribMessageIntegrity{
public:
  uint8_t *sha1;
};

/* https://tools.ietf.org/html/rfc5245#section-19.1 */
class StunAttribPriority{
public:
  uint32_t value;
};

/* https://tools.ietf.org/html/rfc5245#section-19.1 */
class StunAttribIceControllling{
public:
  uint64_t tie_breaker;
};

/* https://tools.ietf.org/html/rfc3489#section-11.2.6 */
class StunAttribUsername{
public:
  char *value; /* Must use `length` member of attribute that indicates the number of valid bytes in the username. */
};

/* https://tools.ietf.org/html/rfc5389#section-15.2 */
class StunAttribXorMappedAddress{
public:
  uint8_t family;
  uint16_t port;
  uint8_t ip[16];
};

/* --------------------------------------- */

class StunAttribute{
public:
  StunAttribute();
  void print();

public:
  uint16_t type;
  uint16_t length;
  union{
    StunAttribXorMappedAddress xor_address;
    StunAttribUsername username;
    StunAttribIceControllling ice_controlling;
    StunAttribPriority priority;
    StunAttribSoftware software;
    StunAttribMessageIntegrity message_integrity;
    StunAttribFingerprint fingerprint;
  };
};

/* --------------------------------------- */

class StunMessage{
public:
  StunMessage();
  void setType(uint16_t type);
  void setTransactionId(uint32_t a, uint32_t b, uint32_t c);
  void print();
  void addAttribute(StunAttribute &attr);
  void removeAttributes();
  StunAttribute *getAttributeByType(uint16_t type);

public:
  uint16_t type;
  uint16_t length;
  uint32_t cookie;
  uint32_t transaction_id[3];
  std::vector<StunAttribute> attributes;
};

/* --------------------------------------- */

class StunReader{
public:
  StunReader();
  int parse(uint8_t *data, size_t nbytes, size_t &nparsed, StunMessage &msg); /* `nparsed` and `msg` are filled. */

private:
  int parseXorMappedAddress(StunAttribute &attr);
  int parseUsername(StunAttribute &attr);
  int parseIceControlling(StunAttribute &attr);
  int parsePriority(StunAttribute &attr);
  int parseSoftware(StunAttribute &attr);
  int parseMessageIntegrity(StunAttribute &attr);
  int parseFingerprint(StunAttribute &attr);

  uint8_t readU8();
  uint16_t readU16();
  uint32_t readU32();
  uint64_t readU64();

private:
  uint8_t *buffer_data;
  size_t buffer_size;
  size_t read_dx;
};

/* --------------------------------------- */

class StunWriter{
public:
  StunWriter();

  /* write header and finalize. call for each stun message */
  int begin(StunMessage &msg,
            uint8_t paddingByte = 0x00); /* I've added the padding byte here so that we can use the
                                            examples that can be found here
                                            https://tools.ietf.org/html/rfc5769#section-2.2 as they
                                            use 0x20 or 0x00 as the padding byte which is correct as
                                            you are free to use w/e padding byte you want. */
  int end();

  /* write attributes */
  int writeXorMappedAddress(sockaddr_in addr);
  int writeXorMappedAddress(uint8_t family, uint16_t port, uint32_t ip);
  int writeXorMappedAddress(uint8_t family, uint16_t port, const std::string &ip);
  int writeUsername(const std::string &username);
  int writeSoftware(const std::string &software);
  int writeMessageIntegrity(const std::string &password); /* When using WebRtc this is the ice-upwd of the other agent. */
  int writeFingerprint(); /* Must be the last attribute in the message. When adding a fingerprint,
                             make sure that it is added after the message-integrity (when you also
                             use a message-integrity). */

  /* get buffer */
  uint8_t *getBufferPtr();
  size_t getBufferSize();

private:
  void writeU8(uint8_t v);
  void writeU16(uint16_t v);
  void writeU32(uint32_t v);
  void writeU64(uint64_t v);
  void rewriteU16(size_t dx, uint16_t v);
  void rewriteU32(size_t dx, uint32_t v);
  void writeString(const std::string &str);
  void writePadding();
  int convertIp4StringToInt(const std::string &ip, uint32_t &result);

private:
  std::vector<uint8_t> buffer;
  uint8_t padding_byte;
};

/* --------------------------------------- */

inline uint8_t *StunWriter::getBufferPtr(){

  if (0 == buffer.size()){return NULL;}

  return &buffer[0];
}

inline size_t StunWriter::getBufferSize(){
  return buffer.size();
}

/* --------------------------------------- */
