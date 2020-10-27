#include "checksum.h" // for crc32
#include "defines.h"
#include "stun.h"
#include "socket.h"

/* --------------------------------------- */

std::string stun_family_type_to_string(uint8_t type){
  switch (type){
  case STUN_IP4:{
    return "STUN_IP4";
  }
  case STUN_IP6:{
    return "STUN_IP6";
  }
  default:{
    return "UNKNOWN";
  }
  }
}

std::string stun_message_type_to_string(uint16_t type){
  switch (type){
  case STUN_MSG_TYPE_NONE:{
    return "STUN_MSG_TYPE_NONE";
  }
  case STUN_MSG_TYPE_BINDING_REQUEST:{
    return "STUN_MSG_TYPE_BINDING_REQUEST";
  }
  case STUN_MSG_TYPE_BINDING_RESPONSE_SUCCESS:{
    return "STUN_MSG_TYPE_BINDING_RESPONSE_SUCCESS";
  }
  case STUN_MSG_TYPE_BINDING_RESPONSE_ERROR:{
    return "STUN_MSG_TYPE_BINDING_RESPONSE_ERROR";
  }
  case STUN_MSG_TYPE_BINDING_INDICATION:{
    return "STUN_MSG_TYPE_BINDING_INDICATION";
  }
  default:{
    return "UNKNOWN";
  }
  }
}

std::string stun_attribute_type_to_string(uint16_t type){
  switch (type){
  case STUN_ATTR_TYPE_NONE:{
    return "STUN_ATTR_TYPE_NONE";
  }
  case STUN_ATTR_TYPE_MAPPED_ADDR:{
    return "STUN_ATTR_TYPE_MAPPED_ADDR";
  }
  case STUN_ATTR_TYPE_CHANGE_REQ:{
    return "STUN_ATTR_TYPE_CHANGE_REQ";
  }
  case STUN_ATTR_TYPE_USERNAME:{
    return "STUN_ATTR_TYPE_USERNAME";
  }
  case STUN_ATTR_TYPE_MESSAGE_INTEGRITY:{
    return "STUN_ATTR_TYPE_MESSAGE_INTEGRITY";
  }
  case STUN_ATTR_TYPE_ERR_CODE:{
    return "STUN_ATTR_TYPE_ERR_CODE";
  }
  case STUN_ATTR_TYPE_UNKNOWN_ATTRIBUTES:{
    return "STUN_ATTR_TYPE_UNKNOWN_ATTRIBUTES";
  }
  case STUN_ATTR_TYPE_CHANNEL_NUMBER:{
    return "STUN_ATTR_TYPE_CHANNEL_NUMBER";
  }
  case STUN_ATTR_TYPE_LIFETIME:{
    return "STUN_ATTR_TYPE_LIFETIME";
  }
  case STUN_ATTR_TYPE_XOR_PEER_ADDR:{
    return "STUN_ATTR_TYPE_XOR_PEER_ADDR";
  }
  case STUN_ATTR_TYPE_DATA:{
    return "STUN_ATTR_TYPE_DATA";
  }
  case STUN_ATTR_TYPE_REALM:{
    return "STUN_ATTR_TYPE_REALM";
  }
  case STUN_ATTR_TYPE_NONCE:{
    return "STUN_ATTR_TYPE_NONCE";
  }
  case STUN_ATTR_TYPE_XOR_RELAY_ADDRESS:{
    return "STUN_ATTR_TYPE_XOR_RELAY_ADDRESS";
  }
  case STUN_ATTR_TYPE_REQ_ADDRESS_FAMILY:{
    return "STUN_ATTR_TYPE_REQ_ADDRESS_FAMILY";
  }
  case STUN_ATTR_TYPE_EVEN_PORT:{
    return "STUN_ATTR_TYPE_EVEN_PORT";
  }
  case STUN_ATTR_TYPE_REQUESTED_TRANSPORT:{
    return "STUN_ATTR_TYPE_REQUESTED_TRANSPORT";
  }
  case STUN_ATTR_TYPE_DONT_FRAGMENT:{
    return "STUN_ATTR_TYPE_DONT_FRAGMENT";
  }
  case STUN_ATTR_TYPE_XOR_MAPPED_ADDRESS:{
    return "STUN_ATTR_TYPE_XOR_MAPPED_ADDRESS";
  }
  case STUN_ATTR_TYPE_RESERVATION_TOKEN:{
    return "STUN_ATTR_TYPE_RESERVATION_TOKEN";
  }
  case STUN_ATTR_TYPE_PRIORITY:{
    return "STUN_ATTR_TYPE_PRIORITY";
  }
  case STUN_ATTR_TYPE_USE_CANDIDATE:{
    return "STUN_ATTR_TYPE_USE_CANDIDATE";
  }
  case STUN_ATTR_TYPE_PADDING:{
    return "STUN_ATTR_TYPE_PADDING";
  }
  case STUN_ATTR_TYPE_RESPONSE_PORT:{
    return "STUN_ATTR_TYPE_RESPONSE_PORT";
  }
  case STUN_ATTR_TYPE_SOFTWARE:{
    return "STUN_ATTR_TYPE_SOFTWARE";
  }
  case STUN_ATTR_TYPE_ALTERNATE_SERVER:{
    return "STUN_ATTR_TYPE_ALTERNATE_SERVER";
  }
  case STUN_ATTR_TYPE_FINGERPRINT:{
    return "STUN_ATTR_TYPE_FINGERPRINT";
  }
  case STUN_ATTR_TYPE_ICE_CONTROLLED:{
    return "STUN_ATTR_TYPE_ICE_CONTROLLED";
  }
  case STUN_ATTR_TYPE_ICE_CONTROLLING:{
    return "STUN_ATTR_TYPE_ICE_CONTROLLING";
  }
  case STUN_ATTR_TYPE_RESPONSE_ORIGIN:{
    return "STUN_ATTR_TYPE_RESPONSE_ORIGIN";
  }
  case STUN_ATTR_TYPE_OTHER_ADDRESS:{
    return "STUN_ATTR_TYPE_OTHER_ADDRESS";
  }
  default:{
    return "UNKNOWN";
  }
  }
}

static uint32_t poly_crc32(uint32_t inCrc, const uint8_t *data, size_t nbytes){

  static const unsigned long crc_table[256] ={
      0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535,
      0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD,
      0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D,
      0x6DDDE4EB, 0xF4D4B551, 0x83D385C7, 0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
      0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4,
      0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
      0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59, 0x26D930AC,
      0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
      0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB,
      0xB6662D3D, 0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F,
      0x9FBFE4A5, 0xE8B8D433, 0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB,
      0x086D3D2D, 0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
      0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA,
      0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65, 0x4DB26158, 0x3AB551CE,
      0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A,
      0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
      0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409,
      0xCE61E49F, 0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
      0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739,
      0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
      0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1, 0xF00F9344, 0x8708A3D2, 0x1E01F268,
      0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0,
      0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8,
      0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
      0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF,
      0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703,
      0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7,
      0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D, 0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
      0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE,
      0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
      0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777, 0x88085AE6,
      0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
      0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D,
      0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5,
      0x47B2CF7F, 0x30B5FFE9, 0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605,
      0xCDD70693, 0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
      0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D};

  uint32_t crc32 = inCrc ^ 0xFFFFFFFF;
  size_t i;

  for (i = 0; i < nbytes; i++){crc32 = (crc32 >> 8) ^ crc_table[(crc32 ^ data[i]) & 0xFF];}

  return (crc32 ^ 0xFFFFFFFF);
}

/* --------------------------------------- */

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

#if 0
  printf("stun::compute_hmac_sha1 - verbose: computing hash over %u bytes, using key `%s`:\n", nbytes, key.c_str());
  printf("-----------------------------------\n\t0: ");
  int nl = 0, lines = 0;
  for (int i = 0; i < nbytes; ++i, ++nl){
    if (nl == 4){
      printf("\n\t");
      nl = 0;
      lines++;
      printf("%d: ", lines);
    }
    printf("%02X ", message[i]);
  }
  printf("\n-----------------------------------\n");
#endif

#if 0
  
  printf("stun::compute_hmac_sha1 - verbose: computed hash: ");
  int len = 20;
  for(unsigned int i = 0; i < len; ++i){
    printf("%02X ", output[i]);
  }
  printf("\n");
#endif

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

  // result = (checksum::crc32LE(0 ^ 0xFFFFFFFF, (const char*)&buffer[0], offset + 12) ^ 0xFFFFFFFF) ^ 0x5354554e;
  result = poly_crc32(0L, &buffer[0], offset + 12) ^ 0x5354554e;

  /* and reset the size */
  buffer[2] = curr_size[0];
  buffer[3] = curr_size[1];

  return 0;
}

/* --------------------------------------- */

StunAttribute::StunAttribute() : type(STUN_ATTR_TYPE_NONE), length(0){}

void StunAttribute::print(){

  DONTEVEN_MSG("StunAttribute.type: %s", stun_attribute_type_to_string(type).c_str());
  DONTEVEN_MSG("StunAttribute.length: %u", length);

  switch (type){

  case STUN_ATTR_TYPE_XOR_MAPPED_ADDRESS:{
    DONTEVEN_MSG("StunAttribute.xor_address.family: %s",
                 stun_family_type_to_string(xor_address.family).c_str());
    DONTEVEN_MSG("StunAttribute.xor_address.port: %u", xor_address.port);
    DONTEVEN_MSG("StunAttribute.xor_address.ip: %s", (char *)xor_address.ip);
    break;
  }

  case STUN_ATTR_TYPE_USERNAME:{
    DONTEVEN_MSG("StunAttribute.username.value: `%.*s`", length, username.value);
    break;
  }

  case STUN_ATTR_TYPE_SOFTWARE:{
    DONTEVEN_MSG("StunAttribute.software.value: `%.*s`", length, software.value);
    break;
  }

  case STUN_ATTR_TYPE_ICE_CONTROLLING:{
    uint8_t *p = (uint8_t *)&ice_controlling.tie_breaker;
    DONTEVEN_MSG("StunAttribute.ice_controlling.tie_breaker: 0x%04x%04x", *(uint32_t *)(p + 4),
                 *(uint32_t *)(p));
    break;
  }

  case STUN_ATTR_TYPE_PRIORITY:{
    DONTEVEN_MSG("StunAttribute.priority.value: %u", priority.value);
    break;
  }

  case STUN_ATTR_TYPE_MESSAGE_INTEGRITY:{
    std::stringstream ss;
    for (int i = 0; i < 20; ++i){ss << std::hex << (int)message_integrity.sha1[i];}
    std::string str = ss.str();
    DONTEVEN_MSG("StunAttribute.message_integrity.sha1: %s", str.c_str());
    break;
  }

  case STUN_ATTR_TYPE_FINGERPRINT:{
    DONTEVEN_MSG("StunAttribute.fingerprint.value: 0x%08x", fingerprint.value);
    break;
  }
  }
}

/* --------------------------------------- */

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
StunReader::StunReader() : buffer_data(NULL), buffer_size(0), read_dx(0){}

int StunReader::parse(uint8_t *data, size_t nbytes, size_t &nparsed, StunMessage &msg){

  StunAttribute attr;
  size_t attr_offset = 0;
  nparsed = 0;

  if (NULL == data){
    FAIL_MSG("Cannot parse stun message because given data ptr is a NULL.");
    return -1;
  }

  if (nbytes < 20){
    FAIL_MSG("Cannot parse stun message because given nbytes is < 20.");
    return -2;
  }

  buffer_data = data;
  buffer_size = nbytes;
  read_dx = 0;

  /* Read stun header. */
  msg.type = readU16();
  msg.length = readU16();
  msg.cookie = readU32();
  msg.transaction_id[0] = readU32();
  msg.transaction_id[1] = readU32();
  msg.transaction_id[2] = readU32();

  if ((nbytes - 20) < msg.length){
    FAIL_MSG("Buffer is too small to contain the full stun message.");
    return -3;
  }

  /* Read all the attributes. */
  while ((read_dx + 4) < buffer_size){

    attr.type = readU16();
    attr.length = readU16();
    attr_offset = read_dx;

    switch (attr.type){

    case STUN_ATTR_TYPE_USERNAME:{
      if (0 != parseUsername(attr)){
        FAIL_MSG("Failed to read the username.");
        return -4;
      }
      break;
    }

    case STUN_ATTR_TYPE_XOR_MAPPED_ADDRESS:{
      if (0 != parseXorMappedAddress(attr)){
        FAIL_MSG("Failed to read the xor-mapped-address.");
        return -4;
      }
      break;
    }

    case STUN_ATTR_TYPE_ICE_CONTROLLING:{
      if (0 != parseIceControlling(attr)){
        FAIL_MSG("Failed to read the ice-contontrolling attribute.");
        return -4;
      }
      break;
    }

    case STUN_ATTR_TYPE_PRIORITY:{
      if (0 != parsePriority(attr)){
        FAIL_MSG("Failed to read the priority attribute.");
        return -4;
      }
      break;
    }

    case STUN_ATTR_TYPE_MESSAGE_INTEGRITY:{
      if (0 != parseMessageIntegrity(attr)){
        FAIL_MSG("Failed to parse the message integrity.");
        return -4;
      }
      break;
    }

    case STUN_ATTR_TYPE_FINGERPRINT:{
      if (0 != parseFingerprint(attr)){
        FAIL_MSG("Failed to parse the fingerprint.");
        return -4;
      }
      break;
    }

    case STUN_ATTR_TYPE_SOFTWARE:{
      if (0 != parseSoftware(attr)){
        FAIL_MSG("Failed to parse the software attribute.");
        return -4;
      }
      break;
    }

    default:{
      DONTEVEN_MSG("Unhandled stun attribute: 0x%04X, %s", attr.type,
                   stun_attribute_type_to_string(attr.type).c_str());
      break;
    }
    }

    /* Move the read_dx so it's positioned after the currently parsed attribute */
    read_dx = attr_offset + attr.length;
    while ((read_dx & 0x03) != 0 && (read_dx < buffer_size)){read_dx++;}

    msg.attributes.push_back(attr);

    attr.print();
  }

  nparsed = read_dx;

  return 0;
}

/* --------------------------------------- */

int StunReader::parseFingerprint(StunAttribute &attr){

  if ((read_dx + 4) > buffer_size){
    FAIL_MSG("Cannot read FINGERPRINT because the buffer is too small.");
    return -1;
  }

  attr.fingerprint.value = readU32();

  return 0;
}

int StunReader::parseMessageIntegrity(StunAttribute &attr){

  if ((read_dx + 20) > buffer_size){
    FAIL_MSG("Cannot read the MESSAGE-INTEGRITY because the buffer is too small.");
    return -1;
  }

  attr.message_integrity.sha1 = buffer_data + read_dx;

  return 0;
}

int StunReader::parsePriority(StunAttribute &attr){

  if ((read_dx + 4) > buffer_size){
    FAIL_MSG("Cannot read the PRIORITY attribute because the buffer is too small.");
    return -1;
  }

  attr.priority.value = readU32();

  return 0;
}

int StunReader::parseSoftware(StunAttribute &attr){

  if ((read_dx + attr.length) > buffer_size){
    FAIL_MSG("Cannot read SOFTWARE attribute because the buffer is too small.");
    return -1;
  }

  attr.software.value = (char *)(buffer_data + read_dx);

  return 0;
}

int StunReader::parseIceControlling(StunAttribute &attr){

  if ((read_dx + 8) > buffer_size){
    FAIL_MSG("Cannot read the ICE-CONTROLLING attribute because the buffer is too small.");
    return -1;
  }

  attr.ice_controlling.tie_breaker = readU64();

  return 0;
}

int StunReader::parseUsername(StunAttribute &attr){

  if ((read_dx + attr.length) > buffer_size){
    FAIL_MSG("Cannot read USRENAME attribute because the buffer is too small.");
    return -1;
  }

  attr.username.value = (char *)(buffer_data + read_dx);

  return 0;
}

int StunReader::parseXorMappedAddress(StunAttribute &attr){

  if ((read_dx + 8) > buffer_size){
    FAIL_MSG("Cannot read XOR_MAPPED_ADDRESS because the buffer is too small.");
    return -1;
  }

  /* Skip the first byte, should be ignored by readers. */
  read_dx++;

  /* Read family */
  attr.xor_address.family = readU8();

  if (STUN_IP4 != attr.xor_address.family){
    FAIL_MSG("Currently we only implemented the IP4 XOR_MAPPED_ADDRESS");
    return -2;
  }

  uint8_t cookie[] ={0x42, 0xA4, 0x12, 0x21};
  uint32_t ip = 0;
  uint8_t *ip_ptr = (uint8_t *)&ip;
  uint8_t *port_ptr = (uint8_t *)&attr.xor_address.port;

  /* Read the port. */
  attr.xor_address.port = readU16();
  port_ptr[0] = port_ptr[0] ^ cookie[2];
  port_ptr[1] = port_ptr[1] ^ cookie[3];

  /* Read IP4. */
  ip = readU32();
  ip_ptr[0] = ip_ptr[0] ^ cookie[0];
  ip_ptr[1] = ip_ptr[1] ^ cookie[1];
  ip_ptr[2] = ip_ptr[2] ^ cookie[2];
  ip_ptr[3] = ip_ptr[3] ^ cookie[3];

  sprintf((char *)attr.xor_address.ip, "%u.%u.%u.%u", ip_ptr[3], ip_ptr[2], ip_ptr[1], ip_ptr[0]);

  return 0;
}

/* --------------------------------------- */

uint8_t StunReader::readU8(){

  if ((read_dx + 1) > buffer_size){
    FAIL_MSG("Cannot readU8(), out of bounds.");
    return 0;
  }

  uint8_t v = 0;
  v = buffer_data[read_dx];
  read_dx = read_dx + 1;

  return v;
}

uint16_t StunReader::readU16(){

  if ((read_dx + 2) > buffer_size){
    FAIL_MSG("Cannot readU16(), out of bounds.");
    return 0;
  }

  uint16_t v = 0;
  uint8_t *p = (uint8_t *)&v;
  p[0] = buffer_data[read_dx + 1];
  p[1] = buffer_data[read_dx + 0];
  read_dx = read_dx + 2;

  return v;
}

uint32_t StunReader::readU32(){

  if ((read_dx + 4) > buffer_size){
    FAIL_MSG("Cannot readU32(), out of bounds.");
    return 0;
  }

  uint32_t v = 0;
  uint8_t *p = (uint8_t *)&v;
  p[0] = buffer_data[read_dx + 3];
  p[1] = buffer_data[read_dx + 2];
  p[2] = buffer_data[read_dx + 1];
  p[3] = buffer_data[read_dx + 0];
  read_dx = read_dx + 4;

  return v;
}

uint64_t StunReader::readU64(){

  if ((read_dx + 8) > buffer_size){
    FAIL_MSG("Cannot readU64(), out of bounds.");
    return 0;
  }

  uint64_t v = 0;
  uint8_t *p = (uint8_t *)&v;

  p[0] = buffer_data[read_dx + 7];
  p[1] = buffer_data[read_dx + 6];
  p[2] = buffer_data[read_dx + 5];
  p[3] = buffer_data[read_dx + 4];
  p[4] = buffer_data[read_dx + 3];
  p[5] = buffer_data[read_dx + 2];
  p[6] = buffer_data[read_dx + 1];
  p[7] = buffer_data[read_dx + 0];

  read_dx = read_dx + 8;

  return v;
}

/* --------------------------------------- */

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

int StunWriter::writeXorMappedAddress(sockaddr_in addr){

  if (AF_INET != addr.sin_family){
    FAIL_MSG("Currently we only support ip4 xor-mapped-address attributes.");
    return -1;
  }

  return writeXorMappedAddress(STUN_IP4, ntohs(addr.sin_port), ntohl(addr.sin_addr.s_addr));
}

int StunWriter::writeXorMappedAddress(uint8_t family, uint16_t port, const std::string &ip){

  uint32_t ip_int = 0;
  if (0 != convertIp4StringToInt(ip, ip_int)){
    FAIL_MSG("Cannot write xor-mapped-address, because we failed to convert the given IP4 string "
             "into a uint32_t.");
    return -1;
  }

  return writeXorMappedAddress(family, port, ip_int);
}

/* `ip` is in host byte order. */
int StunWriter::writeXorMappedAddress(uint8_t family, uint16_t port, uint32_t ip){

  if (buffer.size() < 20){
    FAIL_MSG("Cannot write the xor-mapped-address. Make sure you wrote the header first.");
    return -1;
  }

  if (STUN_IP4 != family){
    FAIL_MSG("Cannot write the xor-mapped-address, we only support ip4 for now.");
    return -2;
  }

  /* xor the port  */
  uint8_t cookie[] ={0x42, 0xA4, 0x12, 0x21};
  uint8_t *port_ptr = (uint8_t *)&port;
  port_ptr[0] = port_ptr[0] ^ cookie[2];
  port_ptr[1] = port_ptr[1] ^ cookie[3];

  /* xor the ip */
  uint8_t *ip_ptr = (uint8_t *)&ip;
  ip_ptr[0] = ip_ptr[0] ^ cookie[0];
  ip_ptr[1] = ip_ptr[1] ^ cookie[1];
  ip_ptr[2] = ip_ptr[2] ^ cookie[2];
  ip_ptr[3] = ip_ptr[3] ^ cookie[3];

  /* write header */
  writeU16(STUN_ATTR_TYPE_XOR_MAPPED_ADDRESS);
  writeU16(8);
  writeU8(0);
  writeU8(family);

  /* port and ip */
  writeU16(port);
  writeU32(ip);

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

int StunWriter::convertIp4StringToInt(const std::string &ip, uint32_t &result){

  if (0 == ip.size()){
    FAIL_MSG("Given ip string is empty.");
    return -1;
  }

  in_addr addr;
  if (1 != inet_pton(AF_INET, ip.c_str(), &addr)){
    FAIL_MSG("inet_pton() failed, cannot convert IPv4 string '%s' into uint32_t.", ip.c_str());
    return -2;
  }

  result = ntohl(addr.s_addr);

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
