#pragma once
#include "socket.h"
#include "util.h"

#include <stdint.h>
#include <string>

#define STUN_IP4 0x01
#define STUN_IP6 0x02

#define STUN_MSG_NONE 0x0000
#define STUN_MSG_BINDING_REQUEST 0x0001
#define STUN_MSG_BINDING_RESPONSE_SUCCESS 0x0101
#define STUN_MSG_BINDING_RESPONSE_ERROR 0x0111
#define STUN_MSG_BINDING_INDICATION 0x0011

#define STUN_ATTR_NONE 0x0000
#define STUN_ATTR_MAPPED_ADDR 0x0001
#define STUN_ATTR_CHANGE_REQ 0x0003
#define STUN_ATTR_USERNAME 0x0006
#define STUN_ATTR_MESSAGE_INTEGRITY 0x0008
#define STUN_ATTR_ERR_CODE 0x0009
#define STUN_ATTR_UNKNOWN_ATTRIBUTES 0x000a
#define STUN_ATTR_CHANNEL_NUMBER 0x000c
#define STUN_ATTR_LIFETIME 0x000d
#define STUN_ATTR_XOR_PEER_ADDR 0x0012
#define STUN_ATTR_DATA 0x0013
#define STUN_ATTR_REALM 0x0014
#define STUN_ATTR_NONCE 0x0015
#define STUN_ATTR_XOR_RELAY_ADDRESS 0x0016
#define STUN_ATTR_REQ_ADDRESS_FAMILY 0x0017
#define STUN_ATTR_EVEN_PORT 0x0018
#define STUN_ATTR_REQUESTED_TRANSPORT 0x0019
#define STUN_ATTR_DONT_FRAGMENT 0x001a
#define STUN_ATTR_XOR_MAPPED_ADDRESS 0x0020
#define STUN_ATTR_RESERVATION_TOKEN 0x0022
#define STUN_ATTR_PRIORITY 0x0024
#define STUN_ATTR_USE_CANDIDATE 0x0025
#define STUN_ATTR_PADDING 0x0026
#define STUN_ATTR_RESPONSE_PORT 0x0027
#define STUN_ATTR_GOOG_NETINFO 0xC057
#define STUN_ATTR_SOFTWARE 0x8022
#define STUN_ATTR_ALTERNATE_SERVER 0x8023
#define STUN_ATTR_FINGERPRINT 0x8028
#define STUN_ATTR_ICE_CONTROLLED 0x8029
#define STUN_ATTR_ICE_CONTROLLING 0x802a
#define STUN_ATTR_RESPONSE_ORIGIN 0x802b
#define STUN_ATTR_OTHER_ADDRESS 0x802c

namespace STUN {

  class Packet {
    public:
      Packet(uint16_t type = 0);
      Packet(const char *data, const size_t len);

      // Basic STUN header getters/setters
      uint16_t getType() const;
      void setType(uint16_t type);
      void genTID();
      void copyTID(const Packet & p);

      // Attribute getters
      std::string getAttributeByType(uint16_t type) const;
      bool hasAttribute(uint16_t type) const;

      // Attribute adders
      void addXorMappedAddress(const Socket::Address & addr);
      void addIntegrity(const std::string & pwd);
      void addFingerprint();
      void addUsername(const std::string & usr);

      // Packet info
      std::string toString() const;
      operator bool() const;

      // Holds actual packet data
      Util::ResizeablePointer data;
  };

} // namespace STUN
