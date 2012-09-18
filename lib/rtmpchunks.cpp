/// \file rtmpchunks.cpp
/// Holds all code for the RTMPStream namespace.

#include "rtmpchunks.h"
#include "flv_tag.h"
#include "crypto.h"
#include "timing.h"

char versionstring[] = "WWW.DDVTECH.COM "; ///< String that is repeated in the RTMP handshake
std::string RTMPStream::handshake_in; ///< Input for the handshake.
std::string RTMPStream::handshake_out;///< Output for the handshake.

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
std::map<unsigned int, RTMPStream::Chunk> RTMPStream::Chunk::lastsend;
/// Holds the last received chunk for every msg_id.
std::map<unsigned int, RTMPStream::Chunk> RTMPStream::Chunk::lastrecv;

/// Packs up the chunk for sending over the network.
/// \warning Do not call if you are not actually sending the resulting data!
/// \returns A std::string ready to be sent.
std::string & RTMPStream::Chunk::Pack(){
  static std::string output;
  output.clear();
  RTMPStream::Chunk prev = lastsend[cs_id];
  unsigned int tmpi;
  unsigned char chtype = 0x00;
  if ((prev.msg_type_id > 0) && (prev.cs_id == cs_id)){
    if (msg_stream_id == prev.msg_stream_id){
      chtype = 0x40;//do not send msg_stream_id
      if (len == prev.len){
        if (msg_type_id == prev.msg_type_id){
          chtype = 0x80;//do not send len and msg_type_id
          if (timestamp == prev.timestamp){
            chtype = 0xC0;//do not send timestamp
          }
        }
      }
    }
    //override - we always sent type 0x00 if the timestamp has decreased since last chunk in this channel
    if (timestamp < prev.timestamp){chtype = 0x00;}
  }
  if (cs_id <= 63){
    output += (unsigned char)(chtype | cs_id);
  }else{
    if (cs_id <= 255+64){
      output += (unsigned char)(chtype | 0);
      output += (unsigned char)(cs_id - 64);
    }else{
      output += (unsigned char)(chtype | 1);
      output += (unsigned char)((cs_id - 64) % 256);
      output += (unsigned char)((cs_id - 64) / 256);
    }
  }
  unsigned int ntime = 0;
  if (chtype != 0xC0){
    //timestamp or timestamp diff
    if (chtype == 0x00){
      tmpi = timestamp;
    }else{
      tmpi = timestamp - prev.timestamp;
    }
    if (tmpi >= 0x00ffffff){ntime = tmpi; tmpi = 0x00ffffff;}
    output += (unsigned char)((tmpi >> 16) & 0xff);
    output += (unsigned char)((tmpi >> 8) & 0xff);
    output += (unsigned char)(tmpi & 0xff);
    if (chtype != 0x80){
      //len
      tmpi = len;
      output += (unsigned char)((tmpi >> 16) & 0xff);
      output += (unsigned char)((tmpi >> 8) & 0xff);
      output += (unsigned char)(tmpi & 0xff);
      //msg type id
      output += (unsigned char)msg_type_id;
      if (chtype != 0x40){
        //msg stream id
        output += (unsigned char)(msg_stream_id % 256);
        output += (unsigned char)(msg_stream_id / 256);
        output += (unsigned char)(msg_stream_id / (256*256));
        output += (unsigned char)(msg_stream_id / (256*256*256));
      }
    }
  }
  //support for 0x00ffffff timestamps
  if (ntime){
    output += (unsigned char)(ntime & 0xff);
    output += (unsigned char)((ntime >> 8) & 0xff);
    output += (unsigned char)((ntime >> 16) & 0xff);
    output += (unsigned char)((ntime >> 24) & 0xff);
  }
  len_left = 0;
  while (len_left < len){
    tmpi = len - len_left;
    if (tmpi > RTMPStream::chunk_snd_max){tmpi = RTMPStream::chunk_snd_max;}
    output.append(data, len_left, tmpi);
    len_left += tmpi;
    if (len_left < len){
      if (cs_id <= 63){
        output += (unsigned char)(0xC0 + cs_id);
      }else{
        if (cs_id <= 255+64){
          output += (unsigned char)(0xC0);
          output += (unsigned char)(cs_id - 64);
        }else{
          output += (unsigned char)(0xC1);
          output += (unsigned char)((cs_id - 64) % 256);
          output += (unsigned char)((cs_id - 64) / 256);
        }
      }
    }
  }
  lastsend[cs_id] = *this;
  RTMPStream::snd_cnt += output.size();
  return output;
}//SendChunk

/// Default contructor, creates an empty chunk with all values initialized to zero.
RTMPStream::Chunk::Chunk(){
  cs_id = 0;
  timestamp = 0;
  len = 0;
  real_len = 0;
  len_left = 0;
  msg_type_id = 0;
  msg_stream_id = 0;
  data = "";
}//constructor

/// Packs up a chunk with the given arguments as properties.
std::string & RTMPStream::SendChunk(unsigned int cs_id, unsigned char msg_type_id, unsigned int msg_stream_id, std::string data){
  static RTMPStream::Chunk ch;
  ch.cs_id = cs_id;
  ch.timestamp = Util::epoch();
  ch.len = data.size();
  ch.real_len = data.size();
  ch.len_left = 0;
  ch.msg_type_id = msg_type_id;
  ch.msg_stream_id = msg_stream_id;
  ch.data = data;
  return ch.Pack();
}//constructor

/// Packs up a chunk with media contents.
/// \param msg_type_id Type number of the media, as per FLV standard.
/// \param data Contents of the media data.
/// \param len Length of the media data, in bytes.
/// \param ts Timestamp of the media data, relative to current system time.
std::string & RTMPStream::SendMedia(unsigned char msg_type_id, unsigned char * data, int len, unsigned int ts){
  static RTMPStream::Chunk ch;
  ch.cs_id = msg_type_id+42;
  ch.timestamp = ts;
  ch.len = len;
  ch.real_len = len;
  ch.len_left = 0;
  ch.msg_type_id = msg_type_id;
  ch.msg_stream_id = 1;
  ch.data = std::string((char*)data, (size_t)len);
  return ch.Pack();
}//SendMedia

/// Packs up a chunk with media contents.
/// \param tag FLV::Tag with media to send.
std::string & RTMPStream::SendMedia(FLV::Tag & tag){
  static RTMPStream::Chunk ch;
  ch.cs_id = ((unsigned char)tag.data[0]);
  ch.timestamp = tag.tagTime();
  ch.len = tag.len-15;
  ch.real_len = tag.len-15;
  ch.len_left = 0;
  ch.msg_type_id = (unsigned char)tag.data[0];
  ch.msg_stream_id = 1;
  ch.data = std::string(tag.data+11, (size_t)(tag.len-15));
  return ch.Pack();
}//SendMedia

/// Packs up a chunk for a control message with 1 argument.
std::string & RTMPStream::SendCTL(unsigned char type, unsigned int data){
  static RTMPStream::Chunk ch;
  ch.cs_id = 2;
  ch.timestamp = Util::epoch();
  ch.len = 4;
  ch.real_len = 4;
  ch.len_left = 0;
  ch.msg_type_id = type;
  ch.msg_stream_id = 0;
  ch.data.resize(4);
  *(int*)((char*)ch.data.c_str()) = htonl(data);
  return ch.Pack();
}//SendCTL

/// Packs up a chunk for a control message with 2 arguments.
std::string & RTMPStream::SendCTL(unsigned char type, unsigned int data, unsigned char data2){
  static RTMPStream::Chunk ch;
  ch.cs_id = 2;
  ch.timestamp = Util::epoch();
  ch.len = 5;
  ch.real_len = 5;
  ch.len_left = 0;
  ch.msg_type_id = type;
  ch.msg_stream_id = 0;
  ch.data.resize(5);
  *(unsigned int*)((char*)ch.data.c_str()) = htonl(data);
  ch.data[4] = data2;
  return ch.Pack();
}//SendCTL

/// Packs up a chunk for a user control message with 1 argument.
std::string & RTMPStream::SendUSR(unsigned char type, unsigned int data){
  static RTMPStream::Chunk ch;
  ch.cs_id = 2;
  ch.timestamp = Util::epoch();
  ch.len = 6;
  ch.real_len = 6;
  ch.len_left = 0;
  ch.msg_type_id = 4;
  ch.msg_stream_id = 0;
  ch.data.resize(6);
  *(unsigned int*)(((char*)ch.data.c_str())+2) = htonl(data);
  ch.data[0] = 0;
  ch.data[1] = type;
  return ch.Pack();
}//SendUSR

/// Packs up a chunk for a user control message with 2 arguments.
std::string & RTMPStream::SendUSR(unsigned char type, unsigned int data, unsigned int data2){
  static RTMPStream::Chunk ch;
  ch.cs_id = 2;
  ch.timestamp = Util::epoch();
  ch.len = 10;
  ch.real_len = 10;
  ch.len_left = 0;
  ch.msg_type_id = 4;
  ch.msg_stream_id = 0;
  ch.data.resize(10);
  *(unsigned int*)(((char*)ch.data.c_str())+2) = htonl(data);
  *(unsigned int*)(((char*)ch.data.c_str())+6) = htonl(data2);
  ch.data[0] = 0;
  ch.data[1] = type;
  return ch.Pack();
}//SendUSR


/// Parses the argument string into the current chunk.
/// Tries to read a whole chunk, removing data from the input string as it reads.
/// If only part of a chunk is read, it will remove the part and call itself again.
/// This has the effect of only causing a "true" reponse in the case a *whole* chunk
/// is read, not just part of a chunk.
/// \param indata The input string to parse and update.
/// \warning This function will destroy the current data in this chunk!
/// \returns True if a whole chunk could be read, false otherwise.
bool RTMPStream::Chunk::Parse(std::string & indata){
  gettimeofday(&RTMPStream::lastrec, 0);
  unsigned int i = 0;
  if (indata.size() < 1) return false;//need at least a byte

  unsigned char chunktype = indata[i++];
  //read the chunkstream ID properly
  switch (chunktype & 0x3F){
    case 0:
      if (indata.size() < 2) return false;//need at least 2 bytes to continue
      cs_id = indata[i++] + 64;
      break;
    case 1:
      if (indata.size() < 3) return false;//need at least 3 bytes to continue
      cs_id = indata[i++] + 64;
      cs_id += indata[i++] * 256;
      break;
    default:
      cs_id = chunktype & 0x3F;
      break;
  }

  RTMPStream::Chunk prev = lastrecv[cs_id];

  //process the rest of the header, for each chunk type
  headertype = chunktype & 0xC0;
  switch (headertype){
    case 0x00:
      if (indata.size() < i+11) return false; //can't read whole header
      timestamp = indata[i++]*256*256;
      timestamp += indata[i++]*256;
      timestamp += indata[i++];
      len = indata[i++]*256*256;
      len += indata[i++]*256;
      len += indata[i++];
      len_left = 0;
      msg_type_id = indata[i++];
      msg_stream_id = indata[i++];
      msg_stream_id += indata[i++]*256;
      msg_stream_id += indata[i++]*256*256;
      msg_stream_id += indata[i++]*256*256*256;
      break;
    case 0x40:
      if (indata.size() < i+7) return false; //can't read whole header
      if (prev.msg_type_id == 0){fprintf(stderr, "Warning: Header type 0x40 with no valid previous chunk!\n");}
      timestamp = indata[i++]*256*256;
      timestamp += indata[i++]*256;
      timestamp += indata[i++];
      if (timestamp != 0x00ffffff){timestamp += prev.timestamp;}
      len = indata[i++]*256*256;
      len += indata[i++]*256;
      len += indata[i++];
      len_left = 0;
      msg_type_id = indata[i++];
      msg_stream_id = prev.msg_stream_id;
      break;
    case 0x80:
      if (indata.size() < i+3) return false; //can't read whole header
      if (prev.msg_type_id == 0){fprintf(stderr, "Warning: Header type 0x80 with no valid previous chunk!\n");}
      timestamp = indata[i++]*256*256;
      timestamp += indata[i++]*256;
      timestamp += indata[i++];
      if (timestamp != 0x00ffffff){timestamp += prev.timestamp;}
      len = prev.len;
      len_left = prev.len_left;
      msg_type_id = prev.msg_type_id;
      msg_stream_id = prev.msg_stream_id;
      break;
    case 0xC0:
      if (prev.msg_type_id == 0){fprintf(stderr, "Warning: Header type 0xC0 with no valid previous chunk!\n");}
      timestamp = prev.timestamp;
      len = prev.len;
      len_left = prev.len_left;
      msg_type_id = prev.msg_type_id;
      msg_stream_id = prev.msg_stream_id;
      break;
  }
  //calculate chunk length, real length, and length left till complete
  if (len_left > 0){
    real_len = len_left;
    len_left -= real_len;
  }else{
    real_len = len;
  }
  if (real_len > RTMPStream::chunk_rec_max){
    len_left += real_len - RTMPStream::chunk_rec_max;
    real_len = RTMPStream::chunk_rec_max;
  }
  //read extended timestamp, if neccesary
  if (timestamp == 0x00ffffff){
    if (indata.size() < i+4) return false; //can't read whole header
    timestamp = indata[i++]*256*256*256;
    timestamp += indata[i++]*256*256;
    timestamp += indata[i++]*256;
    timestamp += indata[i++];
  }

  //read data if length > 0, and allocate it
  if (real_len > 0){
    if (prev.len_left > 0){
      data = prev.data;
    }else{
      data = "";
    }
    if (indata.size() < i+real_len) return false;//can't read all data (yet)
    data.append(indata, i, real_len);
    indata = indata.substr(i+real_len);
    lastrecv[cs_id] = *this;
    RTMPStream::rec_cnt += i+real_len;
    if (len_left == 0){
      return true;
    }else{
      return Parse(indata);
    }
  }else{
    data = "";
    indata = indata.substr(i+real_len);
    lastrecv[cs_id] = *this;
    RTMPStream::rec_cnt += i+real_len;
    return true;
  }
}//Parse

/// Parses the argument string into the current chunk.
/// Tries to read a whole chunk, removing data from the input as it reads.
/// If only part of a chunk is read, it will remove the part and call itself again.
/// This has the effect of only causing a "true" reponse in the case a *whole* chunk
/// is read, not just part of a chunk.
/// \param indata The input string to parse and update.
/// \warning This function will destroy the current data in this chunk!
/// \returns True if a whole chunk could be read, false otherwise.
bool RTMPStream::Chunk::Parse(Socket::Buffer & buffer){
  gettimeofday(&RTMPStream::lastrec, 0);
  unsigned int i = 0;
  if (!buffer.available(3)){return false;}//we want at least 3 bytes
  std::string indata = buffer.copy(3);
  
  unsigned char chunktype = indata[i++];
  //read the chunkstream ID properly
  switch (chunktype & 0x3F){
    case 0:
      cs_id = indata[i++] + 64;
      break;
    case 1:
      cs_id = indata[i++] + 64;
      cs_id += indata[i++] * 256;
      break;
    default:
      cs_id = chunktype & 0x3F;
      break;
  }
  
  RTMPStream::Chunk prev = lastrecv[cs_id];
  
  //process the rest of the header, for each chunk type
  headertype = chunktype & 0xC0;
  switch (headertype){
    case 0x00:
      if (!buffer.available(i+11)){return false;} //can't read whole header
      indata = buffer.copy(i+11);
      timestamp = indata[i++]*256*256;
      timestamp += indata[i++]*256;
      timestamp += indata[i++];
      len = indata[i++]*256*256;
      len += indata[i++]*256;
      len += indata[i++];
      len_left = 0;
      msg_type_id = indata[i++];
      msg_stream_id = indata[i++];
      msg_stream_id += indata[i++]*256;
      msg_stream_id += indata[i++]*256*256;
      msg_stream_id += indata[i++]*256*256*256;
      break;
    case 0x40:
      if (!buffer.available(i+7)){return false;} //can't read whole header
      indata = buffer.copy(i+7);
      if (prev.msg_type_id == 0){fprintf(stderr, "Warning: Header type 0x40 with no valid previous chunk!\n");}
      timestamp = indata[i++]*256*256;
      timestamp += indata[i++]*256;
      timestamp += indata[i++];
      if (timestamp != 0x00ffffff){timestamp += prev.timestamp;}
      len = indata[i++]*256*256;
      len += indata[i++]*256;
      len += indata[i++];
      len_left = 0;
      msg_type_id = indata[i++];
      msg_stream_id = prev.msg_stream_id;
      break;
    case 0x80:
      if (!buffer.available(i+3)){return false;} //can't read whole header
      indata = buffer.copy(i+3);
      if (prev.msg_type_id == 0){fprintf(stderr, "Warning: Header type 0x80 with no valid previous chunk!\n");}
      timestamp = indata[i++]*256*256;
      timestamp += indata[i++]*256;
      timestamp += indata[i++];
      if (timestamp != 0x00ffffff){timestamp += prev.timestamp;}
      len = prev.len;
      len_left = prev.len_left;
      msg_type_id = prev.msg_type_id;
      msg_stream_id = prev.msg_stream_id;
      break;
    case 0xC0:
      if (prev.msg_type_id == 0){fprintf(stderr, "Warning: Header type 0xC0 with no valid previous chunk!\n");}
      timestamp = prev.timestamp;
      len = prev.len;
      len_left = prev.len_left;
      msg_type_id = prev.msg_type_id;
      msg_stream_id = prev.msg_stream_id;
      break;
  }
  //calculate chunk length, real length, and length left till complete
  if (len_left > 0){
    real_len = len_left;
    len_left -= real_len;
  }else{
    real_len = len;
  }
  if (real_len > RTMPStream::chunk_rec_max){
    len_left += real_len - RTMPStream::chunk_rec_max;
    real_len = RTMPStream::chunk_rec_max;
  }
  //read extended timestamp, if neccesary
  if (timestamp == 0x00ffffff){
    if (!buffer.available(i+4)){return false;} //can't read timestamp
    indata = buffer.copy(i+4);
    timestamp = indata[i++]*256*256*256;
    timestamp += indata[i++]*256*256;
    timestamp += indata[i++]*256;
    timestamp += indata[i++];
  }
  
  //read data if length > 0, and allocate it
  if (real_len > 0){
    if (!buffer.available(i+real_len)){return false;}//can't read all data (yet)
    buffer.remove(i);//remove the header
    if (prev.len_left > 0){
      data = prev.data + buffer.remove(real_len);//append the data and remove from buffer
    }else{
      data = buffer.remove(real_len);//append the data and remove from buffer
    }
    lastrecv[cs_id] = *this;
    RTMPStream::rec_cnt += i+real_len;
    if (len_left == 0){
      return true;
    }else{
      return Parse(buffer);
    }
  }else{
    buffer.remove(i);//remove the header
    data = "";
    indata = indata.substr(i+real_len);
    lastrecv[cs_id] = *this;
    RTMPStream::rec_cnt += i+real_len;
    return true;
  }
}//Parse

/// Does the handshake. Expects handshake_in to be filled, and fills handshake_out.
/// After calling this function, don't forget to read and ignore 1536 extra bytes,
/// these are the handshake response and not interesting for us because we don't do client
/// verification.
bool RTMPStream::doHandshake(){
  char Version;
  //Read C0
  Version = RTMPStream::handshake_in[0];
  uint8_t * Client = (uint8_t *)RTMPStream::handshake_in.c_str() + 1;
  RTMPStream::handshake_out.resize(3073);
  uint8_t * Server = (uint8_t *)RTMPStream::handshake_out.c_str() + 1;
  RTMPStream::rec_cnt += 1537;

  //Build S1 Packet
  *((uint32_t*)Server) = 0;//time zero
  *(((uint32_t*)(Server+4))) = htonl(0x01020304);//version 1 2 3 4
  for (int i = 8; i < 3072; ++i){Server[i] = versionstring[i%16];}//"random" data

  bool encrypted = (Version == 6);
  #if DEBUG >= 4
  fprintf(stderr, "Handshake version is %hhi\n", Version);
  #endif
  uint8_t _validationScheme = 5;
  if (ValidateClientScheme(Client, 0)) _validationScheme = 0;
  if (ValidateClientScheme(Client, 1)) _validationScheme = 1;

  #if DEBUG >= 4
  fprintf(stderr, "Handshake type is %hhi, encryption is %s\n", _validationScheme, encrypted?"on":"off");
  #endif

  //FIRST 1536 bytes from server response
  //compute DH key position
  uint32_t serverDHOffset = GetDHOffset(Server, _validationScheme);
  uint32_t clientDHOffset = GetDHOffset(Client, _validationScheme);

  //generate DH key
  DHWrapper dhWrapper(1024);
  if (!dhWrapper.Initialize()) return false;
  if (!dhWrapper.CreateSharedKey(Client + clientDHOffset, 128)) return false;
  if (!dhWrapper.CopyPublicKey(Server + serverDHOffset, 128)) return false;

  if (encrypted) {
    uint8_t secretKey[128];
    if (!dhWrapper.CopySharedKey(secretKey, sizeof (secretKey))) return false;
    RC4_KEY _pKeyIn;
    RC4_KEY _pKeyOut;
    InitRC4Encryption(secretKey, (uint8_t*) & Client[clientDHOffset], (uint8_t*) & Server[serverDHOffset], &_pKeyIn, &_pKeyOut);
    uint8_t data[1536];
    RC4(&_pKeyIn, 1536, data, data);
    RC4(&_pKeyOut, 1536, data, data);
  }
  //generate the digest
  uint32_t serverDigestOffset = GetDigestOffset(Server, _validationScheme);
  uint8_t *pTempBuffer = new uint8_t[1536 - 32];
  memcpy(pTempBuffer, Server, serverDigestOffset);
  memcpy(pTempBuffer + serverDigestOffset, Server + serverDigestOffset + 32, 1536 - serverDigestOffset - 32);
  uint8_t *pTempHash = new uint8_t[512];
  HMACsha256(pTempBuffer, 1536 - 32, genuineFMSKey, 36, pTempHash);
  memcpy(Server + serverDigestOffset, pTempHash, 32);
  delete[] pTempBuffer;
  delete[] pTempHash;

  //SECOND 1536 bytes from server response
  uint32_t keyChallengeIndex = GetDigestOffset(Client, _validationScheme);
  pTempHash = new uint8_t[512];
  HMACsha256(Client + keyChallengeIndex, 32, genuineFMSKey, 68, pTempHash);
  uint8_t *pLastHash = new uint8_t[512];
  HMACsha256(Server + 1536, 1536 - 32, pTempHash, 32, pLastHash);
  memcpy(Server + 1536 * 2 - 32, pLastHash, 32);
  delete[] pTempHash;
  delete[] pLastHash;
  //DONE BUILDING THE RESPONSE ***//
  Server[-1] = Version;
  RTMPStream::snd_cnt += 3073;
  return true;
}
