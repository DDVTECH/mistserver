#include "stun.h"

#include "checksum.h" // for crc32
#include "defines.h"

#include <bitfields.h>
#include <socket.h>
#include <sstream>

STUN::Packet::Packet(uint16_t type) {
  if (!data.allocate(20)) { return; }
  data.append(0, 20);
  Bit::htobl(data + 4, 0x2112a442);
  setType(type);
}

STUN::Packet::Packet(const char *_data, const size_t _len) : data(_data, _len) {}

uint16_t STUN::Packet::getType() const {
  if (data.size() < 2) { return 0; }
  return Bit::btohs(data);
}

void STUN::Packet::setType(uint16_t type) {
  if (data.size() < 2) { return; }
  Bit::htobs(data, type);
}

void STUN::Packet::genTID() {
  if (data.size() < 20) { return; }
  Util::getRandomBytes(data + 8, 12);
}

void STUN::Packet::copyTID(const STUN::Packet & p) {
  if (data.size() < 20) { return; }
  if (p.data.size() < 20) { return; }
  memcpy(data + 8, p.data + 8, 12);
}

std::string STUN::Packet::getAttributeByType(uint16_t type) const {
  size_t offset = 20;
  while ((offset + 4) < data.size()) {
    uint16_t t = Bit::btohs(data + offset);
    uint16_t l = Bit::btohs(data + offset + 2);

    // Abort if we're about to read outside of the packet size
    if (offset + 4 + l > data.size()) { return ""; }

    if (t == type) { return std::string(data + offset + 4, l); }

    // Skip payload
    offset += 4 + l;

    // Skip padding bytes, if any
    while ((offset & 0x03) != 0) { ++offset; }
  }
  // Not found
  return "";
}

bool STUN::Packet::hasAttribute(uint16_t type) const {
  size_t offset = 20;
  while ((offset + 4) < data.size()) {
    uint16_t t = Bit::btohs(data + offset);
    uint16_t l = Bit::btohs(data + offset + 2);

    // Abort if we're about to read outside of the packet size
    if (offset + 4 + l > data.size()) { return false; }

    if (t == type) { return true; }

    // Skip payload
    offset += 4 + l;

    // Skip padding bytes, if any
    while ((offset & 0x03) != 0) { ++offset; }
  }
  // Not found
  return false;
}

/// Adds STUN_ATTR_XOR_MAPPED_ADDRESS to the packet with the given address set
void STUN::Packet::addXorMappedAddress(const Socket::Address & addr) {
  auto fam = addr.family();
  bool four = addr.is4in6();
  if (fam != AF_INET && fam != AF_INET6) {
    FAIL_MSG("Cannot write non-IPv4/IPv6 address into STUN message: %s", addr.toString().c_str());
    return;
  }

  size_t offset = data.size();
  if (!data.allocate(offset + ((fam == AF_INET || four) ? 12 : 24))) {
    FAIL_MSG("Cannot write address into STUN message: out of memory");
    return;
  }
  data.append(0, (fam == AF_INET || four) ? 12 : 24);

  // Update length field
  Bit::htobs(data + 2, data.size() - 20);

  // Write attribute data:
  // Type (2b)
  Bit::htobs(data + offset, STUN_ATTR_XOR_MAPPED_ADDRESS);
  // Size (2b)
  Bit::htobs(data + offset + 2, (fam == AF_INET || four) ? 8 : 20);
  // Payload: 1b zero, 1b family, 2b port
  data[offset + 4] = 0;
  data[offset + 5] = (fam == AF_INET || four) ? STUN_IP4 : STUN_IP6;
  // 2b Port
  uint16_t port = addr.port();
  data[offset + 6] = (port >> 8) ^ data[4];
  data[offset + 7] = (port & 0xFF) ^ data[5];
  // 4b (IPv4) or 16b (IPv6) address
  const uint8_t *const p = addr.ipPtr() + (four ? 12 : 0);
  size_t addrLen = (fam == AF_INET || four) ? 4 : 16;
  for (size_t i = 0; i < addrLen; ++i) { data[offset + 8 + i] = p[i] ^ data[4 + i]; }
  // No padding needed; this attribute is always a 4-multiple in size
}

void STUN::Packet::addIntegrity(const std::string & pwd) {
  size_t offset = data.size();
  if (!data.allocate(offset + 24)) {
    FAIL_MSG("Cannot write integrity into STUN message: out of memory");
    return;
  }
  data.append(0, 24);

  // Update length field
  Bit::htobs(data + 2, data.size() - 20);

  // Write attribute data:
  // Type (2b)
  Bit::htobs(data + offset, STUN_ATTR_MESSAGE_INTEGRITY);
  // Size (2b)
  Bit::htobs(data + offset + 2, 20);

  // Payload: 20b HMAC, see https://datatracker.ietf.org/doc/html/rfc5389#section-15.4
  mbedtls_md_context_t md_ctx;
  mbedtls_md_init(&md_ctx);
  const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  if (!md_info) {
    FAIL_MSG("Failed to find mbedtls SHA1 info");
  } else if (mbedtls_md_setup(&md_ctx, md_info, 1)) {
    FAIL_MSG("Failed to setup the md context.");
  } else if (mbedtls_md_hmac_starts(&md_ctx, (const unsigned char *)pwd.c_str(), pwd.size())) {
    FAIL_MSG("Failed to start the hmac.");
  } else if (mbedtls_md_hmac_update(&md_ctx, (const unsigned char *)(char *)data, offset)) {
    FAIL_MSG("Failed to update the hmac.");
  } else if (mbedtls_md_hmac_finish(&md_ctx, (unsigned char *)((char *)data) + offset + 4)) {
    FAIL_MSG("Failed to finish the hmac.");
  }
  mbedtls_md_free(&md_ctx);

  // No padding needed; this attribute is always a 4-multiple in size
}

void STUN::Packet::addFingerprint() {
  size_t offset = data.size();
  if (!data.allocate(offset + 8)) {
    FAIL_MSG("Cannot write fingerprint into STUN message: out of memory");
    return;
  }
  data.append(0, 8);

  // Update length field
  Bit::htobs(data + 2, data.size() - 20);

  // Write attribute data:
  // Type (2b)
  Bit::htobs(data + offset, STUN_ATTR_FINGERPRINT);
  // Size (2b)
  Bit::htobs(data + offset + 2, 4);
  // Payload: 4b CRC32, see https://tools.ietf.org/html/rfc5389#section-15.5
  uint32_t calced = (checksum::crc32LE(0 ^ 0xFFFFFFFF, data, offset) ^ 0xFFFFFFFF) ^ 0x5354554e;
  Bit::htobl(data + offset + 4, calced);
  // No padding needed; this attribute is always a 4-multiple in size
}

void STUN::Packet::addUsername(const std::string & usr) {
  size_t offset = data.size();
  size_t padding = (4 - (usr.size() % 4)) % 4;

  if (!data.allocate(offset + 4 + usr.size() + padding)) {
    FAIL_MSG("Cannot write username into STUN message: out of memory");
    return;
  }
  data.append(0, 4 + usr.size() + padding);

  // Update length field
  Bit::htobs(data + 2, data.size() - 20);

  // Write attribute data:
  // Type (2b)
  Bit::htobs(data + offset, STUN_ATTR_USERNAME);
  // Size (2b)
  Bit::htobs(data + offset + 2, usr.size());
  // Payload: username string
  memcpy(data + offset + 4, usr.data(), usr.size());
  // Padding zeroes
  memset(data + offset + 4 + usr.size(), 0, padding);
}

std::string STUN::Packet::toString() const {
  std::stringstream ret;
  if (data.size() < 20) {
    ret << "Packet invalid: length " << data.size() << " < 20";
    return ret.str();
  }
  if (Bit::btohl(data + 4) != 0x2112a442) {
    ret << "Packet invalid: bad cookie " << Bit::btohl(data + 4);
    return ret.str();
  }
  switch (Bit::btohs(data)) {
    case STUN_MSG_BINDING_REQUEST: ret << "BIND REQ"; break;
    case STUN_MSG_BINDING_RESPONSE_SUCCESS: ret << "BIND OK"; break;
    case STUN_MSG_BINDING_RESPONSE_ERROR: ret << "BIND ERR"; break;
    case STUN_MSG_BINDING_INDICATION: ret << "BIND IND"; break;
    default: ret << "UNKNOWN";
  }
  if (Bit::btohs(data + 2) + 20 != data.size()) {
    ret << " [size " << data.size() << " != " << Bit::btohs(data + 2) + 20 << "]";
  }

  size_t offset = 20;
  std::string pwd;
  while ((offset + 4) < data.size()) {
    uint16_t t = Bit::btohs(data + offset);
    uint16_t l = Bit::btohs(data + offset + 2);

    // Abort if we're about to read outside of the packet size
    if (offset + 4 + l > data.size()) { return ""; }

    switch (t) {
      case STUN_ATTR_XOR_MAPPED_ADDRESS:
        if (l == 8) {
          sockaddr_in sa;
          sa.sin_family = AF_INET;
          uint8_t *p = (uint8_t *)&(sa.sin_port);
          uint8_t *a = (uint8_t *)&(sa.sin_addr);
          p[0] = data[offset + 6] ^ data[4];
          p[1] = data[offset + 7] ^ data[5];
          for (size_t i = 0; i < 4; ++i) { a[i] = data[offset + 8 + i] ^ data[4 + i]; }
          Socket::Address addr((const char *)&sa);
          ret << " <XorMappedAddr " << addr << ">";
        } else if (l == 20) {
          sockaddr_in6 sa;
          sa.sin6_family = AF_INET6;
          uint8_t *p = (uint8_t *)&(sa.sin6_port);
          uint8_t *a = (uint8_t *)&(sa.sin6_addr);
          p[0] = data[offset + 6] ^ data[4];
          p[1] = data[offset + 7] ^ data[5];
          for (size_t i = 0; i < 16; ++i) { a[i] = data[offset + 8 + i] ^ data[4 + i]; }
          Socket::Address addr((const char *)&sa);
          ret << " <XorMappedAddr " << addr << ">";
        } else {
          ret << " <XorMappedAddr (invalid length)>";
        }
        break;
      case STUN_ATTR_PRIORITY:
        if (l != 4) {
          ret << " <Priority (invalid length)>";
        } else {
          ret << " <Priority " << Bit::btohl(data + offset + 4) << ">";
        }
        break;
      case STUN_ATTR_USERNAME: {
        std::string uname(data + offset + 4, l);
        ret << " <Username '" << uname << "'>";
        size_t col = uname.find(':');
        if (col != std::string::npos) { pwd = uname.substr(col + 1); }
      } break;
      case STUN_ATTR_MESSAGE_INTEGRITY:
        if (l != 20) {
          ret << " <MsgIntegrity (invalid length)>";
        } else {
          if (pwd.size()) {
            unsigned char computed[20];
            Util::ResizeablePointer tmpPkt;
            tmpPkt.assign(data, offset);
            Bit::htobs(tmpPkt + 2, tmpPkt.size() - 20);
            mbedtls_md_context_t md_ctx;
            mbedtls_md_init(&md_ctx);
            const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
            if (!md_info) {
              FAIL_MSG("Failed to find mbedtls SHA1 info");
            } else if (mbedtls_md_setup(&md_ctx, md_info, 1)) {
              FAIL_MSG("Failed to setup the md context.");
            } else if (mbedtls_md_hmac_starts(&md_ctx, (const unsigned char *)pwd.c_str(), pwd.size())) {
              FAIL_MSG("Failed to start the hmac.");
            } else if (mbedtls_md_hmac_update(&md_ctx, (const unsigned char *)(char *)tmpPkt, tmpPkt.size())) {
              FAIL_MSG("Failed to update the hmac.");
            } else if (mbedtls_md_hmac_finish(&md_ctx, computed)) {
              FAIL_MSG("Failed to finish the hmac.");
            }
            mbedtls_md_free(&md_ctx);
            if (memcmp(computed, data + offset + 4, l)) {
              ret << " <MsgIntegrity BAD>";
            } else {
              ret << " <MsgIntegrity ok>";
            }
          } else {
            ret << " <MsgIntegrity (unknown validity)>";
          }
        }
        break;
      case STUN_ATTR_FINGERPRINT:
        if (l != 4) {
          ret << " <Fingerprint (invalid length)>";
        } else {
          uint32_t calced = (checksum::crc32LE(0 ^ 0xFFFFFFFF, data, offset) ^ 0xFFFFFFFF) ^ 0x5354554e;
          uint32_t stored = Bit::btohl(data + offset + 4);
          ret << " <Fingerprint " << (stored == calced ? "ok" : "BAD") << ">";
        }
        break;
      case STUN_ATTR_GOOG_NETINFO:
        if (l != 4) {
          ret << " <GoogleNetinfo (invalid length)>";
        } else {
          ret << " <GoogleNetinfo ID=" << Bit::btohs(data + offset + 4)
              << " cost=" << Bit::btohs(data + offset + 6) << ">";
        }
        break;
      case STUN_ATTR_ICE_CONTROLLING:
        if (l != 8) {
          ret << " <IceControlling (invalid length)>";
        } else {
          ret << " <IceControlling " << Bit::btohll(data + offset + 4) << ">";
        }
        break;
      default: ret << " <Attr " << t << " (" << l << "b)>"; break;
    }

    // Skip payload
    offset += 4 + l;

    // Skip padding bytes, if any
    while ((offset & 0x03) != 0) { ++offset; }
  }

  return ret.str();
}

STUN::Packet::operator bool() const {
  if (data.size() < 20) { return false; }
  if (Bit::btohl(data + 4) != 0x2112a442) { return false; }
  return true;
}
