#include "checksum.h" // for crc32
#include "defines.h"
#include "stun.h"
#include <socket.h>
#include <bitfields.h>

/* --------------------------------------- */

std::string stun_family_type_to_string(uint8_t type){
  switch (type){
  case STUN_IP4:{ return "STUN_IP4"; }
  case STUN_IP6:{ return "STUN_IP6"; }
  default:{ return "UNKNOWN"; }
  }
}

std::string stun_message_type_to_string(uint16_t type){
  switch (type){
  case STUN_MSG_TYPE_NONE:{ return "STUN_MSG_TYPE_NONE"; }
  case STUN_MSG_TYPE_BINDING_REQUEST:{ return "STUN_MSG_TYPE_BINDING_REQUEST"; }
  case STUN_MSG_TYPE_BINDING_RESPONSE_SUCCESS:{ return "STUN_MSG_TYPE_BINDING_RESPONSE_SUCCESS"; }
  case STUN_MSG_TYPE_BINDING_RESPONSE_ERROR:{ return "STUN_MSG_TYPE_BINDING_RESPONSE_ERROR"; }
  case STUN_MSG_TYPE_BINDING_INDICATION:{ return "STUN_MSG_TYPE_BINDING_INDICATION"; }
  default:{ return "UNKNOWN"; }
  }
}

std::string stun_attribute_type_to_string(uint16_t type){
  switch (type){
  case STUN_ATTR_TYPE_NONE:{ return "STUN_ATTR_TYPE_NONE"; }
  case STUN_ATTR_TYPE_MAPPED_ADDR:{ return "STUN_ATTR_TYPE_MAPPED_ADDR"; }
  case STUN_ATTR_TYPE_CHANGE_REQ:{ return "STUN_ATTR_TYPE_CHANGE_REQ"; }
  case STUN_ATTR_TYPE_USERNAME:{ return "STUN_ATTR_TYPE_USERNAME"; }
  case STUN_ATTR_TYPE_MESSAGE_INTEGRITY:{ return "STUN_ATTR_TYPE_MESSAGE_INTEGRITY"; }
  case STUN_ATTR_TYPE_ERR_CODE:{ return "STUN_ATTR_TYPE_ERR_CODE"; }
  case STUN_ATTR_TYPE_UNKNOWN_ATTRIBUTES:{ return "STUN_ATTR_TYPE_UNKNOWN_ATTRIBUTES"; }
  case STUN_ATTR_TYPE_CHANNEL_NUMBER:{ return "STUN_ATTR_TYPE_CHANNEL_NUMBER"; }
  case STUN_ATTR_TYPE_LIFETIME:{ return "STUN_ATTR_TYPE_LIFETIME"; }
  case STUN_ATTR_TYPE_XOR_PEER_ADDR:{ return "STUN_ATTR_TYPE_XOR_PEER_ADDR"; }
  case STUN_ATTR_TYPE_DATA:{ return "STUN_ATTR_TYPE_DATA"; }
  case STUN_ATTR_TYPE_REALM:{ return "STUN_ATTR_TYPE_REALM"; }
  case STUN_ATTR_TYPE_NONCE:{ return "STUN_ATTR_TYPE_NONCE"; }
  case STUN_ATTR_TYPE_XOR_RELAY_ADDRESS:{ return "STUN_ATTR_TYPE_XOR_RELAY_ADDRESS"; }
  case STUN_ATTR_TYPE_REQ_ADDRESS_FAMILY:{ return "STUN_ATTR_TYPE_REQ_ADDRESS_FAMILY"; }
  case STUN_ATTR_TYPE_EVEN_PORT:{ return "STUN_ATTR_TYPE_EVEN_PORT"; }
  case STUN_ATTR_TYPE_REQUESTED_TRANSPORT:{ return "STUN_ATTR_TYPE_REQUESTED_TRANSPORT"; }
  case STUN_ATTR_TYPE_DONT_FRAGMENT:{ return "STUN_ATTR_TYPE_DONT_FRAGMENT"; }
  case STUN_ATTR_TYPE_XOR_MAPPED_ADDRESS:{ return "STUN_ATTR_TYPE_XOR_MAPPED_ADDRESS"; }
  case STUN_ATTR_TYPE_RESERVATION_TOKEN:{ return "STUN_ATTR_TYPE_RESERVATION_TOKEN"; }
  case STUN_ATTR_TYPE_PRIORITY:{ return "STUN_ATTR_TYPE_PRIORITY"; }
  case STUN_ATTR_TYPE_USE_CANDIDATE:{ return "STUN_ATTR_TYPE_USE_CANDIDATE"; }
  case STUN_ATTR_TYPE_PADDING:{ return "STUN_ATTR_TYPE_PADDING"; }
  case STUN_ATTR_TYPE_RESPONSE_PORT:{ return "STUN_ATTR_TYPE_RESPONSE_PORT"; }
  case STUN_ATTR_TYPE_SOFTWARE:{ return "STUN_ATTR_TYPE_SOFTWARE"; }
  case STUN_ATTR_TYPE_ALTERNATE_SERVER:{ return "STUN_ATTR_TYPE_ALTERNATE_SERVER"; }
  case STUN_ATTR_TYPE_FINGERPRINT:{ return "STUN_ATTR_TYPE_FINGERPRINT"; }
  case STUN_ATTR_TYPE_ICE_CONTROLLED:{ return "STUN_ATTR_TYPE_ICE_CONTROLLED"; }
  case STUN_ATTR_TYPE_ICE_CONTROLLING:{ return "STUN_ATTR_TYPE_ICE_CONTROLLING"; }
  case STUN_ATTR_TYPE_RESPONSE_ORIGIN:{ return "STUN_ATTR_TYPE_RESPONSE_ORIGIN"; }
  case STUN_ATTR_TYPE_OTHER_ADDRESS:{ return "STUN_ATTR_TYPE_OTHER_ADDRESS"; }
  default:{ return "UNKNOWN"; }
  }
}

int stun_compute_hmac_sha1(uint8_t *message, uint32_t nbytes, std::string key, uint8_t *output){

  int r = 0;
  mbedtls_md_context_t md_ctx ={0};
  const mbedtls_md_info_t *md_info = NULL;

  if (NULL == message){
    FAIL_MSG("Can't compute hmac_sha1 as the input message is empty.");
    return -1;
  }

  if (nbytes == 0){
    FAIL_MSG("Can't compute hmac_sha1 as the input length is invalid.");
    return -2;
  }

  if (key.size() == 0){
    FAIL_MSG("Can't compute the hmac_sha1 as the key size is 0.");
    return -3;
  }

  if (NULL == output){
    FAIL_MSG("Can't compute the hmac_sha as the output buffer is NULL.");
    return -4;
  }

  md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  if (!md_info){
    FAIL_MSG("Failed to find the MBEDTLS_MD_SHA1");
    r = -5;
    goto error;
  }

  r = mbedtls_md_setup(&md_ctx, md_info, 1);
  if (r != 0){
    FAIL_MSG("Failed to setup the md context.");
    r = -6;
    goto error;
  }

  DONTEVEN_MSG("Calculating hmac-sha1 with key `%s` with size %zu over %" PRIu32 " bytes of data.",
               key.c_str(), key.size(), nbytes);

  r = mbedtls_md_hmac_starts(&md_ctx, (const unsigned char *)key.c_str(), key.size());
  if (r != 0){
    FAIL_MSG("Failed to start the hmac.");
    r = -7;
    goto error;
  }

  r = mbedtls_md_hmac_update(&md_ctx, (const unsigned char *)message, nbytes);
  if (r != 0){
    FAIL_MSG("Failed to update the hmac.");
    r = -8;
    goto error;
  }

  r = mbedtls_md_hmac_finish(&md_ctx, output);
  if (r != 0){
    FAIL_MSG("Failed to finish the hmac.");
    r = -9;
    goto error;
  }

error:
  mbedtls_md_free(&md_ctx);
  return r;
}

int stun_compute_message_integrity(std::vector<uint8_t> &buffer, std::string key, uint8_t *output){

  uint16_t dx = 20;
  uint16_t offset = 0;
  uint16_t len = 0;
  uint16_t type = 0;
  uint8_t curr_size[2];

  if (0 == buffer.size()){
    FAIL_MSG("Cannot compute message integrity; buffer empty.");
    return -1;
  }

  if (0 == key.size()){
    FAIL_MSG("Error: cannot compute message inegrity, key empty.");
    return -2;
  }

  curr_size[0] = buffer[2];
  curr_size[1] = buffer[3];

  while (dx < buffer.size()){

    type |= buffer[dx + 1] & 0x00FF;
    type |= (buffer[dx + 0] << 8) & 0xFF00;
    dx += 2;

    len |= (buffer[dx + 1] & 0x00FF);
    len |= (buffer[dx + 0] << 8) & 0xFF00;
    dx += 2;

    offset = dx;
    dx += len;

    /* skip padding. */
    while ((dx & 0x03) != 0 && dx < buffer.size()){dx++;}

    if (type == STUN_ATTR_TYPE_MESSAGE_INTEGRITY){break;}

    type = 0;
    len = 0;
  }

  /* rewrite Message-Length header field */
  buffer[2] = (offset >> 8) & 0xFF;
  buffer[3] = offset & 0xFF;

  /*
    and compute the sha1
    we subtract the last 4 bytes, which are the attribute-type and
    attribute-length of the Message-Integrity field which are not
    used.
  */
  if (0 != stun_compute_hmac_sha1(&buffer[0], offset - 4, key, output)){
    buffer[2] = curr_size[0];
    buffer[3] = curr_size[1];
    return -3;
  }

  /* rewrite message-length. */
  buffer[2] = curr_size[0];
  buffer[3] = curr_size[1];

  return 0;
}

int stun_compute_fingerprint(std::vector<uint8_t> &buffer, uint32_t &result){

  uint32_t dx = 20;
  uint16_t offset = 0;
  uint16_t len = 0; /* messsage-length */
  uint16_t type = 0;
  uint8_t curr_size[2];

  if (0 == buffer.size()){
    FAIL_MSG("Cannot compute fingerprint because the buffer is empty.");
    return -1;
  }

  /* copy current message-length */
  curr_size[0] = buffer[2];
  curr_size[1] = buffer[3];

  /* compute the size that should be used as Message-Length when computing the CRC32 */
  while (dx < buffer.size()){

    type |= buffer[dx + 1] & 0x00FF;
    type |= (buffer[dx + 0] << 8) & 0xFF00;
    dx += 2;

    len |= buffer[dx + 1] & 0x00FF;
    len |= (buffer[dx + 0] << 8) & 0xFF00;
    dx += 2;

    offset = dx;
    dx += len;

    /* skip padding. */
    while ((dx & 0x03) != 0 && dx < buffer.size()){dx++;}

    if (type == STUN_ATTR_TYPE_FINGERPRINT){break;}

    type = 0;
    len = 0;
  }

  /* rewrite message-length */
  offset -= 16;
  buffer[2] = (offset >> 8) & 0xFF;
  buffer[3] = offset & 0xFF;

  result = (checksum::crc32LE(0 ^ 0xFFFFFFFF, (const char*)&buffer[0], offset + 12) ^ 0xFFFFFFFF) ^ 0x5354554e;
  //result = poly_crc32(0L, &buffer[0], offset + 12) ^ 0x5354554e;

  /* and reset the size */
  buffer[2] = curr_size[0];
  buffer[3] = curr_size[1];

  return 0;
}

/* --------------------------------------- */

StunAttribute::StunAttribute() : type(STUN_ATTR_TYPE_NONE), length(0){}

StunMessage::StunMessage() : type(STUN_MSG_TYPE_NONE), length(0), cookie(0x2112a442){
  transaction_id[0] = 0;
  transaction_id[1] = 0;
  transaction_id[2] = 0;
}

void StunMessage::setType(uint16_t messageType){
  type = messageType;
}

void StunMessage::setTransactionId(uint32_t a, uint32_t b, uint32_t c){
  transaction_id[0] = a;
  transaction_id[1] = b;
  transaction_id[2] = c;
}

void StunMessage::removeAttributes(){
  attributes.clear();
}

void StunMessage::addAttribute(StunAttribute &attr){
  attributes.push_back(attr);
}

void StunMessage::print(){
  DONTEVEN_MSG("StunMessage.type: %s", stun_message_type_to_string(type).c_str());
  DONTEVEN_MSG("StunMessage.length: %u", length);
  DONTEVEN_MSG("StunMessage.cookie: 0x%08X", cookie);
  DONTEVEN_MSG("StunMessage.transaction_id: 0x%08X, 0x%08X, 0x%08X", transaction_id[0],
               transaction_id[1], transaction_id[2]);
}

StunAttribute *StunMessage::getAttributeByType(uint16_t type){
  size_t nattribs = attributes.size();
  for (size_t i = 0; i < nattribs; ++i){
    if (attributes[i].type == type){return &attributes[i];}
  }
  return NULL;
}
/* --------------------------------------- */

bool STUN::parse(const char *data, size_t nbytes, StunMessage &msg){
  if (!data){
    FAIL_MSG("STUN parser: data is null");
    return false;
  }
  if (nbytes < 20){
    FAIL_MSG("Cannot parse STUN message smaller than 20 bytes");
    return false;
  }

  // Read 20-byte STUN header
  msg.type = Bit::btohs(data);
  msg.length = Bit::btohs(data+2);
  msg.cookie = Bit::btohl(data+4);
  msg.transaction_id[0] = Bit::btohl(data+8);
  msg.transaction_id[1] = Bit::btohl(data+12);
  msg.transaction_id[2] = Bit::btohl(data+16);

  if (msg.length + 20 > nbytes){
    FAIL_MSG("Cannot parse partial STUN message");
    return false;
  }

  // Read attributes, if any
  size_t offset = 20;
  while ((offset + 4) < nbytes){
    StunAttribute attr;
    attr.type = Bit::btohs(data+offset);
    attr.length = Bit::btohs(data+offset+2);

    // Abort if we're about to go outside of the packet size
    if (offset + 4 + attr.length > nbytes){break;}

    attr.data.assign(data + offset + 4, attr.length);
    msg.attributes.push_back(attr);
    offset += 4 + attr.length;
    // Skip padding bytes, if any
    while ((offset & 0x03) != 0 && (offset < nbytes)){++offset;}
  }

  return true;
}

StunWriter::StunWriter() : padding_byte(0){}

int StunWriter::begin(StunMessage &msg, uint8_t paddingByte){

  /* set the byte that we use when adding padding. */
  padding_byte = paddingByte;

  /* make sure we start with an empty buffer. */
  buffer.clear();

  /* writer header */
  writeU16(msg.type);              /* type */
  writeU16(0);                     /* length */
  writeU32(msg.cookie);            /* magic cookie */
  writeU32(msg.transaction_id[0]); /* transaction id */
  writeU32(msg.transaction_id[1]); /* transaction id */
  writeU32(msg.transaction_id[2]); /* transaction id */

  return 0;
}

int StunWriter::end(){

  if (buffer.size() < 20){
    FAIL_MSG("Cannot finalize the stun message because the header wasn't written.");
    return -1;
  }

  rewriteU16(2, buffer.size() - 20);

  return 0;
}

/* --------------------------------------- */

int StunWriter::writeXorMappedAddress(const sockaddr * in_addr){
  if (in_addr->sa_family == AF_INET){
    sockaddr_in * addr = (sockaddr_in*)in_addr;
    uint32_t ip_int = ntohl(addr->sin_addr.s_addr);
    return writeXorMappedAddress(STUN_IP4, ntohs(addr->sin_port), (char*)&ip_int);
  }
  if (in_addr->sa_family == AF_INET6){
    sockaddr_in6 * addr = (sockaddr_in6*)in_addr;
    char result[16];
    result[ 0] = addr->sin6_addr.s6_addr[15];
    result[ 1] = addr->sin6_addr.s6_addr[14];
    result[ 2] = addr->sin6_addr.s6_addr[13];
    result[ 3] = addr->sin6_addr.s6_addr[12];
    result[ 4] = addr->sin6_addr.s6_addr[11];
    result[ 5] = addr->sin6_addr.s6_addr[10];
    result[ 6] = addr->sin6_addr.s6_addr[ 9];
    result[ 7] = addr->sin6_addr.s6_addr[ 8];
    result[ 8] = addr->sin6_addr.s6_addr[ 7];
    result[ 9] = addr->sin6_addr.s6_addr[ 6];
    result[10] = addr->sin6_addr.s6_addr[ 5];
    result[11] = addr->sin6_addr.s6_addr[ 4];
    result[12] = addr->sin6_addr.s6_addr[ 3];
    result[13] = addr->sin6_addr.s6_addr[ 2];
    result[14] = addr->sin6_addr.s6_addr[ 1];
    result[15] = addr->sin6_addr.s6_addr[ 0];
    return writeXorMappedAddress(STUN_IP6, ntohs(addr->sin6_port), result);
  }
  FAIL_MSG("Currently we only support IPv4 or IPv6 addresses in STUN packets");
  return -1;
}

int StunWriter::writeXorMappedAddress(uint8_t family, uint16_t port, const std::string &ip){
  if (!ip.size()){
    FAIL_MSG("Given ip string is empty.");
    return -1;
  }

  if (family == STUN_IP4){
    in_addr addr;
    if (ip.find("::ffff:") == 0){
      if (inet_pton(AF_INET, ip.c_str() + 7, &addr) != 1){
        FAIL_MSG("inet_pton() failed, cannot convert IPv4 string '%s' into uint32_t.", ip.c_str() + 7);
        return -2;
      }
    }else{
      if (inet_pton(AF_INET, ip.c_str(), &addr) != 1){
        FAIL_MSG("inet_pton() failed, cannot convert IPv4 string '%s' into uint32_t.", ip.c_str());
        return -2;
      }
    }
    uint32_t ip_int = ntohl(addr.s_addr);
    return writeXorMappedAddress(family, port, (char*)&ip_int);
  }
  if (family == STUN_IP6){
    in_addr addr;
    if (inet_pton(AF_INET6, ip.c_str(), &addr) != 1){
      FAIL_MSG("inet_pton() failed, cannot convert IPv6 string '%s' into integer.", ip.c_str());
      return -2;
    }
    char result[16];
    result[ 0] = (&addr.s_addr)[15];
    result[ 1] = (&addr.s_addr)[14];
    result[ 2] = (&addr.s_addr)[13];
    result[ 3] = (&addr.s_addr)[12];
    result[ 4] = (&addr.s_addr)[11];
    result[ 5] = (&addr.s_addr)[10];
    result[ 6] = (&addr.s_addr)[ 9];
    result[ 7] = (&addr.s_addr)[ 8];
    result[ 8] = (&addr.s_addr)[ 7];
    result[ 9] = (&addr.s_addr)[ 6];
    result[10] = (&addr.s_addr)[ 5];
    result[11] = (&addr.s_addr)[ 4];
    result[12] = (&addr.s_addr)[ 3];
    result[13] = (&addr.s_addr)[ 2];
    result[14] = (&addr.s_addr)[ 1];
    result[15] = (&addr.s_addr)[ 0];
    return writeXorMappedAddress(family, port, result);
  }
  FAIL_MSG("Unknown address family for STUN packet!");
  return -1;
}

/// Write a XorMappedAddress entry, using a host-order raw IP address
int StunWriter::writeXorMappedAddress(uint8_t family, uint16_t port, char * ip_ptr){
  if (buffer.size() < 20){
    FAIL_MSG("Cannot write the xor-mapped-address. Make sure you wrote the header first.");
    return -1;
  }

  if (family != STUN_IP4 && family != STUN_IP6){
    FAIL_MSG("Cannot write the xor-mapped-address, we only support ip4 for now.");
    return -2;
  }

  // Consider IPv6-mapped IPv4 addresses to be simply IPv4
  if (family == STUN_IP6 && !memcmp(ip_ptr + 4, "\377\377\000\000\000\000\000\000\000\000\000\000", 12)){
    family = STUN_IP4;
  }

  // write header
  writeU16(STUN_ATTR_TYPE_XOR_MAPPED_ADDRESS);
  writeU16((family == STUN_IP4)?8:20); //length
  writeU8(0);
  writeU8(family);

  // xor and write the port
  uint8_t *port_ptr = (uint8_t *)&port;
  port_ptr[0] ^= buffer[4+1];
  port_ptr[1] ^= buffer[4+0];
  writeU16(port);

  // xor and write the ip
  if (family == STUN_IP4){
    for (size_t i = 0; i < 4; ++i){
      buffer.push_back(ip_ptr[3-i] ^ buffer[i+4]);
    }
  }else{
    for (size_t i = 0; i < 16; ++i){
      buffer.push_back(ip_ptr[15-i] ^ buffer[i+4]);
    }
  }
  writePadding();
  return 0;
}

int StunWriter::writeUsername(const std::string &username){

  if (buffer.size() < 20){
    FAIL_MSG("Cannot write username because you didn't call `begin()` and the STUN header hasn't "
             "been written yet..");
    return -1;
  }

  writeU16(STUN_ATTR_TYPE_USERNAME);
  writeU16(username.size());
  writeString(username);
  writePadding();

  return 0;
}

int StunWriter::writeSoftware(const std::string &software){

  if (buffer.size() < 20){
    FAIL_MSG("Cannot write software because it seems that you didn't call `begin()` which writes "
             "the stun header.");
    return -1;
  }

  if (software.size() > 763){
    FAIL_MSG("Given software length is too big. ");
    return -2;
  }

  writeU16(STUN_ATTR_TYPE_SOFTWARE);
  writeU16(software.size());
  writeString(software);
  writePadding();

  return 0;
}

int StunWriter::writeMessageIntegrity(const std::string &password){

  if (buffer.size() < 20){
    FAIL_MSG("Cannot write the message integrity because it seems that you didn't call `begin()` "
             "which writes the stun header.");
    return -1;
  }

  if (0 == password.size()){
    FAIL_MSG("The password is empty, cannot write the message integrity.");
    return -2;
  }

  writeU16(STUN_ATTR_TYPE_MESSAGE_INTEGRITY);
  writeU16(20);

  /* calculate the sha1 over the current buffer. */
  uint8_t sha1[20] ={};
  if (0 != stun_compute_message_integrity(buffer, password, sha1)){
    FAIL_MSG("Failed to write the message integrity.");
    return -3;
  }

  /* store the message-integrity */
  std::copy(sha1, sha1 + 20, std::back_inserter(buffer));

  writePadding();

  return 0;
}

/* https://tools.ietf.org/html/rfc5389#section-15.5 */
int StunWriter::writeFingerprint(){

  if (buffer.size() < 20){
    FAIL_MSG("Cannot write the fingerprint because it seems that you didn't write the header, call "
             "`begin()` first.");
    return -1;
  }

  writeU16(STUN_ATTR_TYPE_FINGERPRINT);
  writeU16(4);

  uint32_t fingerprint = 0;
  if (0 != stun_compute_fingerprint(buffer, fingerprint)){
    FAIL_MSG("Failed to compute the fingerprint.");
    return -2;
  }

  writeU32(fingerprint);
  writePadding();

  return 0;
}

/* --------------------------------------- */

void StunWriter::writeU8(uint8_t v){

  buffer.push_back(v);
}

void StunWriter::writeU16(uint16_t v){

  uint8_t *p = (uint8_t *)&v;
  buffer.push_back(p[1]);
  buffer.push_back(p[0]);
}

void StunWriter::writeU32(uint32_t v){

  uint8_t *p = (uint8_t *)&v;
  buffer.push_back(p[3]);
  buffer.push_back(p[2]);
  buffer.push_back(p[1]);
  buffer.push_back(p[0]);
}

void StunWriter::writeU64(uint64_t v){

  uint8_t *p = (uint8_t *)&v;
  buffer.push_back(p[7]);
  buffer.push_back(p[6]);
  buffer.push_back(p[5]);
  buffer.push_back(p[4]);
  buffer.push_back(p[3]);
  buffer.push_back(p[2]);
  buffer.push_back(p[1]);
  buffer.push_back(p[0]);
}

void StunWriter::rewriteU16(size_t dx, uint16_t v){

  if ((dx + 2) > buffer.size()){
    FAIL_MSG("Trying to rewriteU16, but our buffer is too small to contain a u16.");
    return;
  }

  uint8_t *p = (uint8_t *)&v;
  buffer[dx + 0] = p[1];
  buffer[dx + 1] = p[0];
}

void StunWriter::rewriteU32(size_t dx, uint32_t v){

  if ((dx + 4) > buffer.size()){
    FAIL_MSG(
        "Trying to rewrite U32 in Stun::StunWriter::rewriteU32() but index is out of bounds.\n");
    return;
  }

  uint8_t *p = (uint8_t *)&v;
  buffer[dx + 0] = p[3];
  buffer[dx + 1] = p[2];
  buffer[dx + 2] = p[1];
  buffer[dx + 3] = p[0];
}

void StunWriter::writeString(const std::string &str){
  std::copy(str.begin(), str.end(), std::back_inserter(buffer));
}

void StunWriter::writePadding(){

  while ((buffer.size() & 0x03) != 0){buffer.push_back(padding_byte);}
}

/* --------------------------------------- */
