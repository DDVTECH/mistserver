/// \file rtmpchunks.cpp
/// Holds all code for the RTMPStream namespace.

#include "rtmpchunks.h"
#include "defines.h"
#include "flv_tag.h"
#include "timing.h"
#include "auth.h"

std::string RTMPStream::handshake_in; ///< Input for the handshake.
std::string RTMPStream::handshake_out; ///< Output for the handshake.

unsigned int RTMPStream::chunk_rec_max = 128;
unsigned int RTMPStream::chunk_snd_max = 128;
unsigned int RTMPStream::rec_window_size = 2500000;
unsigned int RTMPStream::snd_window_size = 2500000;
unsigned int RTMPStream::rec_window_at = 0;
unsigned int RTMPStream::snd_window_at = 0;
unsigned int RTMPStream::rec_cnt = 0;
unsigned int RTMPStream::snd_cnt = 0;

timeval RTMPStream::lastrec;

/// Holds the last sent chunk for every msg_id.
std::map<unsigned int, RTMPStream::Chunk> RTMPStream::lastsend;
/// Holds the last received chunk for every msg_id.
std::map<unsigned int, RTMPStream::Chunk> RTMPStream::lastrecv;

#define P1024 \
  "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DD" \
  "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED" \
  "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381FFFFFFFFFFFFFFFF"

char genuineFMSKey[] = {0x47, 0x65, 0x6e, 0x75, 0x69, 0x6e, 0x65, 0x20, 0x41, 0x64, 0x6f, 0x62, 0x65, 0x20, 0x46, 0x6c, 0x61, 0x73, 0x68, 0x20,
                           0x4d, 0x65, 0x64, 0x69, 0x61, 0x20, 0x53, 0x65, 0x72,
                           0x76, // Genuine Adobe Flash Media Server 001
                           0x65, 0x72, 0x20, 0x30, 0x30, 0x31, 0xf0, 0xee, 0xc2, 0x4a, 0x80, 0x68, 0xbe, 0xe8, 0x2e, 0x00, 0xd0, 0xd1, 0x02, 0x9e, 0x7e, 0x57, 0x6e, 0xec,
                           0x5d, 0x2d, 0x29, 0x80, 0x6f, 0xab, 0x93, 0xb8, 0xe6, 0x36, 0xcf, 0xeb, 0x31, 0xae
                          }; // 68

char genuineFPKey[] = {0x47, 0x65, 0x6e, 0x75, 0x69, 0x6e, 0x65, 0x20, 0x41, 0x64, 0x6f, 0x62, 0x65, 0x20, 0x46, 0x6c, 0x61, 0x73, 0x68, 0x20,
                          0x50, 0x6c, 0x61,
                          0x79, // Genuine Adobe Flash Player 001
                          0x65, 0x72, 0x20, 0x30, 0x30, 0x31, 0xf0, 0xee, 0xc2, 0x4a, 0x80, 0x68, 0xbe, 0xe8, 0x2e, 0x00, 0xd0, 0xd1, 0x02, 0x9e, 0x7e, 0x57, 0x6e, 0xec,
                          0x5d, 0x2d, 0x29, 0x80, 0x6f, 0xab, 0x93, 0xb8, 0xe6, 0x36, 0xcf, 0xeb, 0x31, 0xae
                         }; // 62

inline uint32_t GetDigestOffset(uint8_t * pBuffer, uint8_t scheme) {
  if (scheme == 0) {
    return ((pBuffer[8] + pBuffer[9] + pBuffer[10] + pBuffer[11]) % 728) + 12;
  } else {
    return ((pBuffer[772] + pBuffer[773] + pBuffer[774] + pBuffer[775]) % 728) + 776;
  }
}

bool ValidateClientScheme(uint8_t * pBuffer, uint8_t scheme) {
  uint32_t clientDigestOffset = GetDigestOffset(pBuffer, scheme);
  uint8_t pTempBuffer[1536-32];
  memcpy(pTempBuffer, pBuffer, clientDigestOffset);
  memcpy(pTempBuffer + clientDigestOffset, pBuffer + clientDigestOffset + 32, 1536 - clientDigestOffset - 32);
  char pTempHash[32];
  Secure::hmac_sha256bin((char*)pTempBuffer, 1536 - 32, genuineFPKey, 30, pTempHash);
  bool result = (memcmp(pBuffer + clientDigestOffset, pTempHash, 32) == 0);
  DEBUG_MSG(DLVL_MEDIUM, "Client scheme validation %hhi %s", scheme, result ? "success" : "failed");
  return result;
}

/// Packs up the chunk for sending over the network.
/// \warning Do not call if you are not actually sending the resulting data!
/// \returns A std::string ready to be sent.
std::string & RTMPStream::Chunk::Pack() {
  static std::string output;
  output.clear();
  bool allow_short = lastsend.count(cs_id);
  RTMPStream::Chunk prev = lastsend[cs_id];
  unsigned int tmpi;
  unsigned char chtype = 0x00;
  if (allow_short && (prev.cs_id == cs_id)) {
    if (msg_stream_id == prev.msg_stream_id) {
      chtype = 0x40; //do not send msg_stream_id
      if (len == prev.len) {
        if (msg_type_id == prev.msg_type_id) {
          chtype = 0x80; //do not send len and msg_type_id
          if (timestamp - prev.timestamp == prev.ts_delta) {
            chtype = 0xC0; //do not send timestamp
          }
        }
      }
    }
    //override - we always sent type 0x00 if the timestamp has decreased since last chunk in this channel
    if (timestamp < prev.timestamp) {
      chtype = 0x00;
    }
  }
  if (cs_id <= 63) {
    output += (unsigned char)(chtype | cs_id);
  } else {
    if (cs_id <= 255 + 64) {
      output += (unsigned char)(chtype | 0);
      output += (unsigned char)(cs_id - 64);
    } else {
      output += (unsigned char)(chtype | 1);
      output += (unsigned char)((cs_id - 64) % 256);
      output += (unsigned char)((cs_id - 64) / 256);
    }
  }
  unsigned int ntime = 0;
  if (chtype != 0xC0) {
    //timestamp or timestamp diff
    if (chtype == 0x00) {
      tmpi = timestamp;
    } else {
      tmpi = timestamp - prev.timestamp;
    }
    ts_delta = tmpi;
    if (tmpi >= 0x00ffffff) {
      ntime = tmpi;
      tmpi = 0x00ffffff;
    }
    ts_header = tmpi;
    output += (unsigned char)((tmpi >> 16) & 0xff);
    output += (unsigned char)((tmpi >> 8) & 0xff);
    output += (unsigned char)(tmpi & 0xff);
    if (chtype != 0x80) {
      //len
      tmpi = len;
      output += (unsigned char)((tmpi >> 16) & 0xff);
      output += (unsigned char)((tmpi >> 8) & 0xff);
      output += (unsigned char)(tmpi & 0xff);
      //msg type id
      output += (unsigned char)msg_type_id;
      if (chtype != 0x40) {
        //msg stream id
        output += (unsigned char)(msg_stream_id % 256);
        output += (unsigned char)(msg_stream_id / 256);
        output += (unsigned char)(msg_stream_id / (256 * 256));
        output += (unsigned char)(msg_stream_id / (256 * 256 * 256));
      }
    }
  }else{
    ts_header = prev.ts_header;
    if (ts_header == 0xffffff){
      ntime = timestamp;
    }
  }
  //support for 0x00ffffff timestamps
  if (ntime) {
    output += (unsigned char)(ntime & 0xff);
    output += (unsigned char)((ntime >> 8) & 0xff);
    output += (unsigned char)((ntime >> 16) & 0xff);
    output += (unsigned char)((ntime >> 24) & 0xff);
  }
  len_left = 0;
  while (len_left < len) {
    tmpi = len - len_left;
    if (tmpi > RTMPStream::chunk_snd_max) {
      tmpi = RTMPStream::chunk_snd_max;
    }
    output.append(data, len_left, tmpi);
    len_left += tmpi;
    if (len_left < len) {
      if (cs_id <= 63) {
        output += (unsigned char)(0xC0 + cs_id);
      } else {
        if (cs_id <= 255 + 64) {
          output += (unsigned char)(0xC0);
          output += (unsigned char)(cs_id - 64);
        } else {
          output += (unsigned char)(0xC1);
          output += (unsigned char)((cs_id - 64) % 256);
          output += (unsigned char)((cs_id - 64) / 256);
        }
      }
      if (ntime) {
        output += (unsigned char)(ntime & 0xff);
        output += (unsigned char)((ntime >> 8) & 0xff);
        output += (unsigned char)((ntime >> 16) & 0xff);
        output += (unsigned char)((ntime >> 24) & 0xff);
      }
    }
  }
  lastsend[cs_id] = *this;
  RTMPStream::snd_cnt += output.size();
  return output;
} //SendChunk

/// Default constructor, creates an empty chunk with all values initialized to zero.
RTMPStream::Chunk::Chunk() {
  headertype = 0;
  cs_id = 0;
  timestamp = 0;
  len = 0;
  real_len = 0;
  len_left = 0;
  msg_type_id = 0;
  msg_stream_id = 0;
  data = "";
} //constructor

/// Packs up a chunk with the given arguments as properties.
std::string & RTMPStream::SendChunk(unsigned int cs_id, unsigned char msg_type_id, unsigned int msg_stream_id, std::string data) {
  static RTMPStream::Chunk ch;
  ch.cs_id = cs_id;
  ch.timestamp = 0;
  ch.len = data.size();
  ch.real_len = data.size();
  ch.len_left = 0;
  ch.msg_type_id = msg_type_id;
  ch.msg_stream_id = msg_stream_id;
  ch.data = data;
  return ch.Pack();
} //constructor

/// Packs up a chunk with media contents.
/// \param msg_type_id Type number of the media, as per FLV standard.
/// \param data Contents of the media data.
/// \param len Length of the media data, in bytes.
/// \param ts Timestamp of the media data, relative to current system time.
std::string & RTMPStream::SendMedia(unsigned char msg_type_id, unsigned char * data, int len, unsigned int ts) {
  static RTMPStream::Chunk ch;
  ch.cs_id = msg_type_id + 42;
  ch.timestamp = ts;
  ch.len = len;
  ch.real_len = len;
  ch.len_left = 0;
  ch.msg_type_id = msg_type_id;
  ch.msg_stream_id = 1;
  ch.data = std::string((char *)data, (size_t)len);
  return ch.Pack();
} //SendMedia

/// Packs up a chunk with media contents.
/// \param tag FLV::Tag with media to send.
std::string & RTMPStream::SendMedia(FLV::Tag & tag) {
  static RTMPStream::Chunk ch;
  //Commented bit is more efficient and correct according to RTMP spec.
  //Simply passing "4" is the only thing that actually plays correctly, though.
  //Adobe, if you're ever reading this... wtf? Seriously.
  ch.cs_id = 4;//((unsigned char)tag.data[0]);
  ch.timestamp = tag.tagTime();
  ch.len_left = 0;
  ch.msg_type_id = (unsigned char)tag.data[0];
  ch.msg_stream_id = 1;
  ch.data = std::string(tag.data + 11, (size_t)(tag.len - 15));
  ch.len = ch.data.size();
  ch.real_len = ch.len;
  return ch.Pack();
} //SendMedia

/// Packs up a chunk for a control message with 1 argument.
std::string & RTMPStream::SendCTL(unsigned char type, unsigned int data) {
  static RTMPStream::Chunk ch;
  ch.cs_id = 2;
  ch.timestamp = Util::getMS();
  ch.msg_type_id = type;
  ch.msg_stream_id = 0;
  ch.len_left = 0;
  if (type == 6){
    ch.len = 5;
    ch.real_len = 5;
    ch.data.resize(5);
    ((char*)ch.data.data())[4] = 0;
  }else{
    ch.len = 4;
    ch.real_len = 4;
    ch.data.resize(4);
  }
  *(int *)((char *)ch.data.data()) = htonl(data);
  return ch.Pack();
} //SendCTL

/// Packs up a chunk for a control message with 2 arguments.
std::string & RTMPStream::SendCTL(unsigned char type, unsigned int data, unsigned char data2) {
  static RTMPStream::Chunk ch;
  ch.cs_id = 2;
  ch.timestamp = Util::getMS();
  ch.len = 5;
  ch.real_len = 5;
  ch.len_left = 0;
  ch.msg_type_id = type;
  ch.msg_stream_id = 0;
  ch.data.resize(5);
  *(unsigned int *)((char *)ch.data.c_str()) = htonl(data);
  ch.data[4] = data2;
  return ch.Pack();
} //SendCTL

/// Packs up a chunk for a user control message with 1 argument.
std::string & RTMPStream::SendUSR(unsigned char type, unsigned int data) {
  static RTMPStream::Chunk ch;
  ch.cs_id = 2;
  ch.timestamp = Util::getMS();
  ch.len = 6;
  ch.real_len = 6;
  ch.len_left = 0;
  ch.msg_type_id = 4;
  ch.msg_stream_id = 0;
  ch.data.resize(6);
  *(unsigned int *)(((char *)ch.data.c_str()) + 2) = htonl(data);
  ch.data[0] = 0;
  ch.data[1] = type;
  return ch.Pack();
} //SendUSR

/// Packs up a chunk for a user control message with 2 arguments.
std::string & RTMPStream::SendUSR(unsigned char type, unsigned int data, unsigned int data2) {
  static RTMPStream::Chunk ch;
  ch.cs_id = 2;
  ch.timestamp = Util::getMS();
  ch.len = 10;
  ch.real_len = 10;
  ch.len_left = 0;
  ch.msg_type_id = 4;
  ch.msg_stream_id = 0;
  ch.data.resize(10);
  *(unsigned int *)(((char *)ch.data.c_str()) + 2) = htonl(data);
  *(unsigned int *)(((char *)ch.data.c_str()) + 6) = htonl(data2);
  ch.data[0] = 0;
  ch.data[1] = type;
  return ch.Pack();
} //SendUSR

/// Parses the argument Socket::Buffer into the current chunk.
/// Tries to read a whole chunk, removing data from the Buffer as it reads.
/// If a single packet contains a partial chunk, it will remove the packet and
/// call itself again. This has the effect of only causing a "true" reponse in
/// the case a *whole* chunk is read, not just part of a chunk.
/// \param buffer The input to parse and update.
/// \warning This function will destroy the current data in this chunk!
/// \returns True if a whole chunk could be read, false otherwise.
bool RTMPStream::Chunk::Parse(Socket::Buffer & buffer) {
  gettimeofday(&RTMPStream::lastrec, 0);
  unsigned int i = 0;
  if (!buffer.available(3)) {
    return false;
  } //we want at least 3 bytes
  std::string indata = buffer.copy(3);

  unsigned char chunktype = indata[i++ ];
  //read the chunkstream ID properly
  switch (chunktype & 0x3F) {
    case 0:
      cs_id = indata[i++ ] + 64;
      break;
    case 1:
      cs_id = indata[i++ ] + 64;
      cs_id += indata[i++ ] * 256;
      break;
    default:
      cs_id = chunktype & 0x3F;
      break;
  }

  bool allow_short = lastrecv.count(cs_id);
  RTMPStream::Chunk prev = lastrecv[cs_id];

  //process the rest of the header, for each chunk type
  headertype = chunktype & 0xC0;
  
  DEBUG_MSG(DLVL_DONTEVEN, "Parsing RTMP chunk header (%#.2hhX) at offset %#X", chunktype, RTMPStream::rec_cnt);
  
  switch (headertype) {
    case 0x00:
      if (!buffer.available(i + 11)) {
        return false;
      } //can't read whole header
      indata = buffer.copy(i + 11);
      timestamp = indata[i++ ] * 256 * 256;
      timestamp += indata[i++ ] * 256;
      timestamp += indata[i++ ];
      ts_delta = timestamp;
      ts_header = timestamp;
      len = indata[i++ ] * 256 * 256;
      len += indata[i++ ] * 256;
      len += indata[i++ ];
      len_left = 0;
      msg_type_id = indata[i++ ];
      msg_stream_id = indata[i++ ];
      msg_stream_id += indata[i++ ] * 256;
      msg_stream_id += indata[i++ ] * 256 * 256;
      msg_stream_id += indata[i++ ] * 256 * 256 * 256;
      break;
    case 0x40:
      if (!buffer.available(i + 7)) {
        return false;
      } //can't read whole header
      indata = buffer.copy(i + 7);
      if (!allow_short) {
        DEBUG_MSG(DLVL_WARN, "Warning: Header type 0x40 with no valid previous chunk!");
      }
      timestamp = indata[i++ ] * 256 * 256;
      timestamp += indata[i++ ] * 256;
      timestamp += indata[i++ ];
      ts_header = timestamp;
      if (timestamp != 0x00ffffff) {
        ts_delta = timestamp;
        timestamp = prev.timestamp + ts_delta;
      }
      len = indata[i++ ] * 256 * 256;
      len += indata[i++ ] * 256;
      len += indata[i++ ];
      len_left = 0;
      msg_type_id = indata[i++ ];
      msg_stream_id = prev.msg_stream_id;
      break;
    case 0x80:
      if (!buffer.available(i + 3)) {
        return false;
      } //can't read whole header
      indata = buffer.copy(i + 3);
      if (!allow_short) {
        DEBUG_MSG(DLVL_WARN, "Warning: Header type 0x80 with no valid previous chunk!");
      }
      timestamp = indata[i++ ] * 256 * 256;
      timestamp += indata[i++ ] * 256;
      timestamp += indata[i++ ];
      ts_header = timestamp;
      if (timestamp != 0x00ffffff) {
        ts_delta = timestamp;
        timestamp = prev.timestamp + ts_delta;
      }
      len = prev.len;
      len_left = prev.len_left;
      msg_type_id = prev.msg_type_id;
      msg_stream_id = prev.msg_stream_id;
      break;
    case 0xC0:
      if (!allow_short) {
        DEBUG_MSG(DLVL_WARN, "Warning: Header type 0xC0 with no valid previous chunk!");
      }
      timestamp = prev.timestamp + prev.ts_delta;
      ts_header = prev.ts_header;
      ts_delta = prev.ts_delta;
      len = prev.len;
      len_left = prev.len_left;
      if (len_left > 0){
        timestamp = prev.timestamp;
      }
      msg_type_id = prev.msg_type_id;
      msg_stream_id = prev.msg_stream_id;
      break;
  }
  //calculate chunk length, real length, and length left till complete
  if (len_left > 0) {
    real_len = len_left;
    len_left -= real_len;
  } else {
    real_len = len;
  }
  if (real_len > RTMPStream::chunk_rec_max) {
    len_left += real_len - RTMPStream::chunk_rec_max;
    real_len = RTMPStream::chunk_rec_max;
  }
  
  DEBUG_MSG(DLVL_DONTEVEN, "Parsing RTMP chunk result: len_left=%d, real_len=%d", len_left, real_len);
  
  //read extended timestamp, if necessary
  if (ts_header == 0x00ffffff && headertype != 0xC0) {
    if (!buffer.available(i + 4)) {
      return false;
    } //can't read timestamp
    indata = buffer.copy(i + 4);
    timestamp = indata[i++ ];
    timestamp += indata[i++ ] * 256;
    timestamp += indata[i++ ] * 256 * 256;
    timestamp += indata[i++ ] * 256 * 256 * 256;
    ts_delta = timestamp;
    DEBUG_MSG(DLVL_DONTEVEN, "Extended timestamp: %u", timestamp);
  }

  //read data if length > 0, and allocate it
  if (real_len > 0) {
    if (!buffer.available(i + real_len)) {
      return false;
    } //can't read all data (yet)
    buffer.remove(i); //remove the header
    if (prev.len_left > 0) {
      data = prev.data + buffer.remove(real_len); //append the data and remove from buffer
    } else {
      data = buffer.remove(real_len); //append the data and remove from buffer
    }
    lastrecv[cs_id] = *this;
    RTMPStream::rec_cnt += i + real_len;
    if (len_left == 0) {
      return true;
    } else {
      return Parse(buffer);
    }
  } else {
    buffer.remove(i); //remove the header
    data = "";
    indata = indata.substr(i + real_len);
    lastrecv[cs_id] = *this;
    RTMPStream::rec_cnt += i + real_len;
    return true;
  }
} //Parse

/// Does the handshake. Expects handshake_in to be filled, and fills handshake_out.
/// After calling this function, don't forget to read and ignore 1536 extra bytes,
/// these are the handshake response and not interesting for us because we don't do client
/// verification.
bool RTMPStream::doHandshake() {
  char Version;
  //Read C0
  if (handshake_in.size() < 1537) {
    DEBUG_MSG(DLVL_FAIL, "Handshake wasn't filled properly (%lu/1537) - aborting!", handshake_in.size());
    return false;
  }
  Version = RTMPStream::handshake_in[0];
  uint8_t * Client = (uint8_t *)RTMPStream::handshake_in.data() + 1;
  RTMPStream::handshake_out.resize(3073);
  uint8_t * Server = (uint8_t *)RTMPStream::handshake_out.data() + 1;
  RTMPStream::rec_cnt += 1537;

  //Build S1 Packet
  *((uint32_t *)Server) = 0; //time zero
  *(((uint32_t *)(Server + 4))) = htonl(0x01020304); //version 1 2 3 4
  for (int i = 8; i < 3072; ++i) {
    Server[i] = FILLER_DATA[i % sizeof(FILLER_DATA)];
  } //"random" data

  bool encrypted = (Version == 6);
  DEBUG_MSG(DLVL_HIGH, "Handshake version is %hhi", Version);
  uint8_t _validationScheme = 5;
  if (ValidateClientScheme(Client, 0)) _validationScheme = 0;
  if (ValidateClientScheme(Client, 1)) _validationScheme = 1;

  DEBUG_MSG(DLVL_HIGH, "Handshake type is %hhi, encryption is %s", _validationScheme, encrypted ? "on" : "off");
  uint32_t serverDigestOffset = GetDigestOffset(Server, _validationScheme);
  uint32_t keyChallengeIndex = GetDigestOffset(Client, _validationScheme);

  //FIRST 1536 bytes for server response
  char pTempBuffer[1504];
  memcpy(pTempBuffer, Server, serverDigestOffset);
  memcpy(pTempBuffer + serverDigestOffset, Server + serverDigestOffset + 32, 1504 - serverDigestOffset);
  Secure::hmac_sha256bin(pTempBuffer, 1504, genuineFMSKey, 36, (char*)Server + serverDigestOffset);

  //SECOND 1536 bytes for server response
  char pTempHash[32];
  Secure::hmac_sha256bin((char*)Client + keyChallengeIndex, 32, genuineFMSKey, 68, pTempHash);
  Secure::hmac_sha256bin((char*)Server + 1536, 1536 - 32, pTempHash, 32, (char*)Server + 1536 * 2 - 32);

  Server[ -1] = Version;
  RTMPStream::snd_cnt += 3073;
  return true;
}

