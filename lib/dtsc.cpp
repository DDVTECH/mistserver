/// \file dtsc.cpp
/// Holds all code for DDVTECH Stream Container parsing/generation.

#include "bitfields.h"
#include "defines.h"
#include "dtsc.h"
#include "encode.h"
#include "lib/shared_memory.h"
#include "lib/util.h"
#include <arpa/inet.h> //for htonl/ntohl
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>

namespace DTSC{
  char Magic_Header[] = "DTSC";
  char Magic_Packet[] = "DTPD";
  char Magic_Packet2[] = "DTP2";
  char Magic_Command[] = "DTCM";

  /// If non-zero, this variable will override any live jitter value calculations with the set value
  uint64_t veryUglyJitterOverride = 0;

  /// The mask that the current process will use to check if a track is valid
  uint8_t trackValidMask = TRACK_VALID_ALL;
  /// The mask that will be set by the current process for new tracks
  uint8_t trackValidDefault = TRACK_VALID_ALL;

  /// Default constructor for packets - sets a null pointer and invalid packet.
  Packet::Packet(){
    data = NULL;
    bufferLen = 0;
    dataLen = 0;
    master = false;
    version = DTSC_INVALID;
    prevNalSize = 0;
  }

  /// Copy constructor for packets, copies an existing packet with same noCopy flag as original.
  Packet::Packet(const Packet &rhs, size_t idx){
    master = false;
    bufferLen = 0;
    data = NULL;
    if (rhs.data && rhs.dataLen){
      reInit(rhs.data, rhs.dataLen);
      if (idx != INVALID_TRACK_ID){Bit::htobl(data + 8, idx);}
    }else{
      null();
    }
  }

  /// Data constructor for packets, either references or copies a packet from raw data.
  Packet::Packet(const char *data_, unsigned int len, bool noCopy){
    master = false;
    bufferLen = 0;
    data = NULL;
    reInit(data_, len, noCopy);
  }

  /// This destructor clears frees the data pointer if the packet was not a reference.
  Packet::~Packet(){
    if (master && data){free(data);}
  }

  /// Copier for packets, copies an existing packet with same noCopy flag as original.
  /// If going from copy to noCopy, this will free the data pointer first.
  void Packet::operator=(const Packet &rhs){
    if (master && !rhs.master){null();}
    if (rhs && rhs.data && rhs.dataLen){
      reInit(rhs.data, rhs.dataLen, !rhs.master);
    }else{
      null();
    }
  }

  /// Returns true if the packet is deemed valid, false otherwise.
  /// Valid packets have a length of at least 8, known header type, and length equal to the length
  /// set in the header.
  Packet::operator bool() const{
    if (!data){
      DONTEVEN_MSG("No data");
      return false;
    }
    if (dataLen < 8){
      VERYHIGH_MSG("Datalen < 8");
      return false;
    }
    if (version == DTSC_INVALID){
      VERYHIGH_MSG("No valid version");
      return false;
    }
    if (ntohl(((int *)data)[1]) + 8 > dataLen){
      VERYHIGH_MSG("Length mismatch");
      return false;
    }
    return true;
  }

  /// Returns the recognized packet type.
  /// This type is set by reInit and all constructors, and then just referenced from there on.
  packType Packet::getVersion() const{return version;}

  /// Resets this packet back to the same state as if it had just been freshly constructed.
  /// If needed, this frees the data pointer.
  void Packet::null(){
    if (master && data){free(data);}
    master = false;
    data = NULL;
    bufferLen = 0;
    dataLen = 0;
    version = DTSC_INVALID;
  }

  /// Internally used resize function for when operating in copy mode and the internal buffer is too
  /// small. It will only resize up, never down.
  ///\param len The length th scale the buffer up to if necessary
  void Packet::resize(size_t len){
    if (master && len > bufferLen){
      char *tmp = (char *)realloc(data, len);
      if (tmp){
        data = tmp;
        bufferLen = len;
      }else{
        FAIL_MSG("Out of memory on parsing a packet");
      }
    }
  }

  void Packet::reInit(Socket::Connection &src){
    int sleepCount = 0;
    null();
    int toReceive = 0;
    while (src.connected()){
      if (!toReceive && src.Received().available(8)){
        if (src.Received().copy(2) != "DT"){
          WARN_MSG("Invalid DTSC Packet header encountered (%s)",
                   Encodings::Hex::encode(src.Received().copy(4)).c_str());
          break;
        }
        toReceive = Bit::btohl(src.Received().copy(8).data() + 4);
      }
      if (toReceive && src.Received().available(toReceive + 8)){
        std::string dataBuf = src.Received().remove(toReceive + 8);
        reInit(dataBuf.data(), dataBuf.size());
        return;
      }
      if (!src.spool()){
        if (sleepCount++ > 750){
          WARN_MSG("Waiting for packet on connection timed out");
          return;
        }
        Util::sleep(20);
      }
    }
  }

  ///\brief Initializes a packet with new data
  ///\param data_ The new data for the packet
  ///\param len The length of the data pointed to by data_
  ///\param noCopy Determines whether to make a copy or not
  void Packet::reInit(const char *data_, unsigned int len, bool noCopy){
    if (!data_){
      WARN_MSG("ReInit received a null pointer with len %d, nulling", len);
      null();
      return;
    }
    if (!data_[0] && !data_[1] && !data_[2] && !data_[3]){
      null();
      return;
    }
    if (data_[0] != 'D' || data_[1] != 'T'){
      unsigned int twlen = len;
      if (twlen > 20){twlen = 20;}
      HIGH_MSG("ReInit received a pointer that didn't start with 'DT' but with %s (%u) - data "
               "corruption?",
               JSON::Value(std::string(data_, twlen)).toString().c_str(), len);
      null();
      return;
    }
    if (len <= 0){len = ntohl(((int *)data_)[1]) + 8;}
    // clear any existing controlled contents
    if (master && noCopy){null();}
    // set control flag to !noCopy
    master = !noCopy;
    // either copy the data, or only the pointer, depending on flag
    if (noCopy){
      data = (char *)data_;
    }else{
      resize(len);
      memcpy(data, data_, len);
    }
    // check header type and store packet length
    dataLen = len;
    version = DTSC_INVALID;
    if (len < 4){
      FAIL_MSG("ReInit received a packet with size < 4");
      return;
    }
    if (!memcmp(data, Magic_Packet2, 4)){version = DTSC_V2;}
    if (!memcmp(data, Magic_Packet, 4)){version = DTSC_V1;}
    if (!memcmp(data, Magic_Header, 4)){version = DTSC_HEAD;}
    if (!memcmp(data, Magic_Command, 4)){version = DTCM;}
    if (version == DTSC_INVALID){FAIL_MSG("ReInit received a packet with invalid header");}
  }

  /// Re-initializes this Packet to contain a generic DTSC packet with the given data fields.
  /// When given a NULL pointer, the data is reserved and memset to 0
  /// If given a NULL pointer and a zero size, an empty packet is created.
  void Packet::genericFill(uint64_t packTime, int64_t packOffset, uint32_t packTrack, const char *packData,
                           size_t packDataSize, uint64_t packBytePos, bool isKeyframe){
    null();
    master = true;
    // time and trackID are part of the 20-byte header.
    // the container object adds 4 bytes (plus 2+namelen for each content, see below)
    // offset, if non-zero, adds 9 bytes (integer type) and 8 bytes (2+namelen)
    // bpos, if >= 0, adds 9 bytes (integer type) and 6 bytes (2+namelen)
    // keyframe, if true, adds 9 bytes (integer type) and 10 bytes (2+namelen)
    // data adds packDataSize+5 bytes (string type) and 6 bytes (2+namelen)
    if (packData && packDataSize < 1){
      FAIL_MSG("Attempted to fill a packet with %zu bytes for timestamp %" PRIu64 ", track %" PRIu32 "!",
               packDataSize, packTime, packTrack);
      return;
    }
    unsigned int sendLen =
        24 + (packOffset ? 17 : 0) + (packBytePos ? 15 : 0) + (isKeyframe ? 19 : 0) + packDataSize + 11;
    resize(sendLen);
    // set internal variables
    version = DTSC_V2;
    dataLen = sendLen;
    // write the first 20 bytes
    memcpy(data, "DTP2", 4);
    Bit::htobl(data + 4, sendLen - 8);
    Bit::htobl(data + 8, packTrack);
    Bit::htobll(data + 12, packTime);
    data[20] = 0xE0; // start container object
    unsigned int offset = 21;
    if (packOffset){
      memcpy(data + offset, "\000\006offset\001", 9);
      Bit::htobll(data + offset + 9, packOffset);
      offset += 17;
    }
    if (packBytePos){
      memcpy(data + offset, "\000\004bpos\001", 7);
      Bit::htobll(data + offset + 7, packBytePos);
      offset += 15;
    }
    if (isKeyframe){
      memcpy(data + offset, "\000\010keyframe\001\000\000\000\000\000\000\000\001", 19);
      offset += 19;
    }
    memcpy(data + offset, "\000\004data\002", 7);
    Bit::htobl(data + offset + 7, packDataSize);
    memcpy(data + offset + 11, packData ? packData : 0, packDataSize);
    // finish container with 0x0000EE
    memcpy(data + offset + 11 + packDataSize, "\000\000\356", 3);
  }

  /// sets the keyframe byte.
  void Packet::setKeyFrame(bool kf){
    uint32_t offset = 23;
    while (data[offset] != 'd' && data[offset] != 'k' && data[offset] != 'K'){
      switch (data[offset]){
      case 'o': offset += 17; break;
      case 'b': offset += 15; break;
      default: FAIL_MSG("Unknown field: %c", data[offset]);
      }
    }

    if (data[offset] == 'k' || data[offset] == 'K'){
      data[offset] = (kf ? 'k' : 'K');
      data[offset + 16] = (kf ? 1 : 0);
    }else{
      ERROR_MSG("Could not set keyframe - field not found!");
    }
  }

  void Packet::appendData(const char *appendData, uint32_t appendLen){
    resize(dataLen + appendLen);
    memcpy(data + dataLen - 3, appendData, appendLen);
    memcpy(data + dataLen - 3 + appendLen, "\000\000\356", 3); // end container
    dataLen += appendLen;
    Bit::htobl(data + 4, Bit::btohl(data + 4) + appendLen);
    uint32_t offset = getDataStringLenOffset();
    Bit::htobl(data + offset, Bit::btohl(data + offset) + appendLen);
  }

  void Packet::appendNal(const char *appendData, uint32_t appendLen){
    if (appendLen == 0){return;}

    resize(dataLen + appendLen + 4);
    Bit::htobl(data + dataLen - 3, appendLen);
    memcpy(data + dataLen - 3 + 4, appendData, appendLen);
    memcpy(data + dataLen - 3 + 4 + appendLen, "\000\000\356", 3); // end container
    dataLen += appendLen + 4;
    Bit::htobl(data + 4, Bit::btohl(data + 4) + appendLen + 4);
    uint32_t offset = getDataStringLenOffset();
    Bit::htobl(data + offset, Bit::btohl(data + offset) + appendLen + 4);

    prevNalSize = appendLen;
  }

  void Packet::upgradeNal(const char *appendData, uint32_t appendLen){
    if (appendLen == 0){return;}
    uint64_t sizeOffset = dataLen - 3 - 4 - prevNalSize;
    if (Bit::btohl(data + sizeOffset) != prevNalSize){
      FAIL_MSG("PrevNalSize state not correct");
      return;
    }
    resize(dataLen + appendLen); // Not + 4 as size bytes have already been written here.
    Bit::htobl(data + sizeOffset, prevNalSize + appendLen);
    prevNalSize += appendLen;
    memcpy(data + dataLen - 3, appendData, appendLen);
    memcpy(data + dataLen - 3 + appendLen, "\000\000\356", 3); // end container
    dataLen += appendLen;
    Bit::htobl(data + 4, Bit::btohl(data + 4) + appendLen);
    uint32_t offset = getDataStringLenOffset();
    Bit::htobl(data + offset, Bit::btohl(data + offset) + appendLen);
  }

  uint32_t Packet::getDataStringLen(){return Bit::btohl(data + getDataStringLenOffset());}

  /// Method can only be used when using internal functions to build the data.
  size_t Packet::getDataStringLenOffset(){
    size_t offset = 23;
    while (data[offset] != 'd'){
      switch (data[offset]){
      case 'o': offset += 17; break;
      case 'b': offset += 15; break;
      case 'k': offset += 19; break;
      case 'K': offset += 19; break;
      default: FAIL_MSG("Unknown field: %c", data[offset]); return -1;
      }
    }
    return offset + 5;
  }

  /// Helper function for skipping over whole DTSC parts
  static char *skipDTSC(char *p, char *max){
    if (p + 1 >= max || p[0] == 0x00){
      return 0; // out of packet! 1 == error
    }
    if (p[0] == DTSC_INT){
      // int, skip 9 bytes to next value
      return p + 9;
    }
    if (p[0] == DTSC_STR){
      if (p + 4 >= max){
        return 0; // out of packet!
      }
      return p + 5 + Bit::btohl(p + 1);
    }
    if (p[0] == DTSC_OBJ || p[0] == DTSC_CON){
      p++;
      // object, scan contents
      while (p < max && p[0] + p[1] != 0){// while not encountering 0x0000 (we assume 0x0000EE)
        if (p + 2 >= max){
          return 0; // out of packet!
        }
        p += 2 + Bit::btohs(p); // skip size
        // otherwise, search through the contents, if needed, and continue
        p = skipDTSC(p, max);
        if (!p){return 0;}
      }
      return p + 3;
    }
    if (p[0] == DTSC_ARR){
      p++;
      // array, scan contents
      while (p < max && p[0] + p[1] != 0){// while not encountering 0x0000 (we assume 0x0000EE)
        // search through contents...
        p = skipDTSC(p, max);
        if (!p){return 0;}
      }
      return p + 3; // skip end marker
    }
    return 0; // out of packet! 1 == error
  }

  ///\brief Retrieves a single parameter as a string
  ///\param identifier The name of the parameter
  ///\param result A location on which the string will be returned
  ///\param len An integer in which the length of the string will be returned
  void Packet::getString(const char *identifier, char *&result, size_t &len) const{
    getScan().getMember(identifier).getString(result, len);
  }

  ///\brief Retrieves a single parameter as a string
  ///\param identifier The name of the parameter
  ///\param result The string in which to store the result
  void Packet::getString(const char *identifier, std::string &result) const{
    result = getScan().getMember(identifier).asString();
  }

  ///\brief Retrieves a single parameter as an integer
  ///\param identifier The name of the parameter
  ///\param result The result is stored in this integer
  void Packet::getInt(const char *identifier, uint64_t &result) const{
    result = getScan().getMember(identifier).asInt();
  }

  ///\brief Retrieves a single parameter as an integer
  ///\param identifier The name of the parameter
  ///\result The requested parameter as an integer
  uint64_t Packet::getInt(const char *identifier) const{
    uint64_t result;
    getInt(identifier, result);
    return result;
  }

  ///\brief Retrieves a single parameter as a boolean
  ///\param identifier The name of the parameter
  ///\param result The result is stored in this boolean
  void Packet::getFlag(const char *identifier, bool &result) const{
    uint64_t result_;
    getInt(identifier, result_);
    result = result_;
  }

  ///\brief Retrieves a single parameter as a boolean
  ///\param identifier The name of the parameter
  ///\result The requested parameter as a boolean
  bool Packet::getFlag(const char *identifier) const{
    bool result;
    getFlag(identifier, result);
    return result;
  }

  ///\brief Checks whether a parameter exists
  ///\param identifier The name of the parameter
  ///\result Whether the parameter exists or not
  bool Packet::hasMember(const char *identifier) const{
    return getScan().getMember(identifier).getType() > 0;
  }

  ///\brief Returns the timestamp of the packet.
  ///\return The timestamp of this packet.
  uint64_t Packet::getTime() const{
    if (version != DTSC_V2){
      if (!data){return 0;}
      return getInt("time");
    }
    return Bit::btohll(data + 12);
  }

  void Packet::setTime(uint64_t _time){
    if (!master){
      INFO_MSG("Can't set the time for this packet, as it is not master.");
      return;
    }
    Bit::htobll(data + 12, _time);
  }

  void Packet::nullMember(const std::string & memb){
    if (!master){
      INFO_MSG("Can't null '%s' for this packet, as it is not master.", memb.c_str());
      return;
    }
    getScan().nullMember(memb);
  }

  ///\brief Returns the track id of the packet.
  ///\return The track id of this packet.
  size_t Packet::getTrackId() const{
    if (version != DTSC_V2){return getInt("trackid");}
    return Bit::btohl(data + 8);
  }

  ///\brief Returns a pointer to the payload of this packet.
  ///\return A pointer to the payload of this packet.
  char *Packet::getData() const{return data;}

  ///\brief Returns the size of this packet.
  ///\return The size of this packet.
  uint32_t Packet::getDataLen() const{return dataLen;}

  ///\brief Returns the size of the payload of this packet.
  ///\return The size of the payload of this packet.
  uint32_t Packet::getPayloadLen() const{
    if (version == DTSC_V2){
      return dataLen - 20;
    }else{
      return dataLen - 8;
    }
  }

  /// Returns a DTSC::Scan instance to the contents of this packet.
  /// May return an invalid instance if this packet is invalid.
  Scan Packet::getScan() const{
    if (!*this || !getDataLen() || !getPayloadLen() || getDataLen() <= getPayloadLen()){
      return Scan();
    }
    return Scan(data + (getDataLen() - getPayloadLen()), getPayloadLen());
  }

  ///\brief Converts the packet into a JSON value
  ///\return A JSON::Value representation of this packet.
  JSON::Value Packet::toJSON() const{
    JSON::Value result;
    uint32_t i = 8;
    if (getVersion() == DTSC_V1){JSON::fromDTMI(data, dataLen, i, result);}
    if (getVersion() == DTSC_V2){JSON::fromDTMI2(data, dataLen, i, result);}
    return result;
  }

  std::string Packet::toSummary() const{
    std::stringstream out;
    char *res = 0;
    size_t len = 0;
    getString("data", res, len);
    out << getTrackId() << "@" << getTime() << ": " << len << " bytes";
    if (hasMember("keyframe")){out << " (keyframe)";}
    return out.str();
  }

  /// Create an invalid DTSC::Scan object by default.
  Scan::Scan(){
    p = 0;
    len = 0;
  }

  /// Create a DTSC::Scan object from memory pointer.
  Scan::Scan(char *pointer, size_t length){
    p = pointer;
    len = length;
  }

  /// Returns whether the DTSC::Scan object contains valid data.
  Scan::operator bool() const{return (p && len);}

  /// Returns an object representing the named indice of this object.
  /// Returns an invalid object if this indice doesn't exist or this isn't an object type.
  Scan Scan::getMember(const std::string &indice) const{
    return getMember(indice.data(), indice.size());
  }

  /// Returns an object representing the named indice of this object.
  /// Returns an invalid object if this indice doesn't exist or this isn't an object type.
  Scan Scan::getMember(const char *indice, const size_t ind_len) const{
    if (getType() != DTSC_OBJ && getType() != DTSC_CON){return Scan();}
    char *i = p + 1;
    // object, scan contents
    while (i[0] + i[1] != 0 && i < p + len){// while not encountering 0x0000 (we assume 0x0000EE)
      if (i + 2 >= p + len){
        return Scan(); // out of packet!
      }
      uint16_t strlen = Bit::btohs(i);
      i += 2;
      if (ind_len == strlen && strncmp(indice, i, strlen) == 0){
        return Scan(i + strlen, len - (i - p));
      }
      i = skipDTSC(i + strlen, p + len);
      if (!i){return Scan();}
    }
    return Scan();
  }

  /// If this is an object type and contains the given indice/len, sets the indice name to all zeroes.
  void Scan::nullMember(const std::string & indice){
    nullMember(indice.data(), indice.size());
  }

  /// If this is an object type and contains the given indice/len, sets the indice name to all zeroes.
  void Scan::nullMember(const char * indice, const size_t ind_len){
    if (getType() != DTSC_OBJ && getType() != DTSC_CON){return;}
    char * i = p + 1;
    //object, scan contents
    while (i[0] + i[1] != 0 && i < p + len){//while not encountering 0x0000 (we assume 0x0000EE)
      if (i + 2 >= p + len){
        return;//out of packet!
      }
      uint16_t strlen = Bit::btohs(i);
      i += 2;
      if (ind_len == strlen && strncmp(indice, i, strlen) == 0){
        memset(i, 0, strlen);
        return;
      }
      i = skipDTSC(i + strlen, p + len);
      if (!i){return;}
    }
    return;
  }

  /// Returns an object representing the named indice of this object.
  /// Returns an invalid object if this indice doesn't exist or this isn't an object type.
  bool Scan::hasMember(const std::string &indice) const{
    return getMember(indice.data(), indice.size());
  }

  /// Returns whether an object representing the named indice of this object exists.
  /// Returns false if this indice doesn't exist or this isn't an object type.
  bool Scan::hasMember(const char *indice, const size_t ind_len) const{
    return getMember(indice, ind_len);
  }

  /// Returns an object representing the named indice of this object.
  /// Returns an invalid object if this indice doesn't exist or this isn't an object type.
  Scan Scan::getMember(const char *indice) const{return getMember(indice, strlen(indice));}

  /// Returns the amount of indices if an array, the amount of members if an object, or zero
  /// otherwise.
  size_t Scan::getSize() const{
    uint32_t arr_indice = 0;
    if (getType() == DTSC_ARR){
      char *i = p + 1;
      // array, scan contents
      while (i[0] + i[1] != 0 && i < p + len){// while not encountering 0x0000 (we assume
                                                // 0x0000EE)
        // search through contents...
        arr_indice++;
        i = skipDTSC(i, p + len);
        if (!i){break;}
      }
    }
    if (getType() == DTSC_OBJ || getType() == DTSC_CON){
      char *i = p + 1;
      // object, scan contents
      while (i[0] + i[1] != 0 && i < p + len){// while not encountering 0x0000 (we assume
                                                // 0x0000EE)
        if (i + 2 >= p + len){
          return Scan(); // out of packet!
        }
        uint16_t strlen = Bit::btohs(i);
        i += 2;
        arr_indice++;
        i = skipDTSC(i + strlen, p + len);
        if (!i){break;}
      }
    }
    return arr_indice;
  }

  /// Returns an object representing the num-th indice of this array.
  /// If not an array but an object, it returns the num-th member, instead.
  /// Returns an invalid object if this indice doesn't exist or this isn't an array or object type.
  Scan Scan::getIndice(size_t num) const{
    if (getType() == DTSC_ARR){
      char *i = p + 1;
      unsigned int arr_indice = 0;
      // array, scan contents
      while (i[0] + i[1] != 0 && i < p + len){// while not encountering 0x0000 (we assume
                                                // 0x0000EE)
        // search through contents...
        if (arr_indice == num){
          return Scan(i, len - (i - p));
        }else{
          arr_indice++;
          i = skipDTSC(i, p + len);
          if (!i){return Scan();}
        }
      }
    }
    if (getType() == DTSC_OBJ || getType() == DTSC_CON){
      char *i = p + 1;
      unsigned int arr_indice = 0;
      // object, scan contents
      while (i[0] + i[1] != 0 && i < p + len){// while not encountering 0x0000 (we assume
                                                // 0x0000EE)
        if (i + 2 >= p + len){
          return Scan(); // out of packet!
        }
        unsigned int strlen = Bit::btohs(i);
        i += 2;
        if (arr_indice == num){
          return Scan(i + strlen, len - (i - p));
        }else{
          arr_indice++;
          i = skipDTSC(i + strlen, p + len);
          if (!i){return Scan();}
        }
      }
    }
    return Scan();
  }

  /// Returns the name of the num-th member of this object.
  /// Returns an empty string on error or when not an object.
  std::string Scan::getIndiceName(size_t num) const{
    if (getType() == DTSC_OBJ || getType() == DTSC_CON){
      char *i = p + 1;
      unsigned int arr_indice = 0;
      // object, scan contents
      while (i[0] + i[1] != 0 && i < p + len){// while not encountering 0x0000 (we assume
                                                // 0x0000EE)
        if (i + 2 >= p + len){
          return ""; // out of packet!
        }
        unsigned int strlen = Bit::btohs(i);
        i += 2;
        if (arr_indice == num){
          return std::string(i, strlen);
        }else{
          arr_indice++;
          i = skipDTSC(i + strlen, p + len);
          if (!i){return "";}
        }
      }
    }
    return "";
  }

  /// Returns the first byte of this DTSC value, or 0 on error.
  char Scan::getType() const{
    if (!p){return 0;}
    return p[0];
  }

  /// Returns the boolean value of this DTSC value.
  /// Numbers are compared to 0.
  /// Strings are checked for non-zero length.
  /// Objects and arrays are checked for content.
  /// Returns false on error or in other cases.
  bool Scan::asBool() const{
    switch (getType()){
    case DTSC_STR: return (p[1] | p[2] | p[3] | p[4]);
    case DTSC_INT: return (asInt() != 0);
    case DTSC_OBJ:
    case DTSC_CON:
    case DTSC_ARR: return (p[1] | p[2]);
    default: return false;
    }
  }

  /// Returns the long long value of this DTSC number value.
  /// Will convert string values to numbers, taking octal and hexadecimal types into account.
  /// Illegal or invalid values return 0.
  int64_t Scan::asInt() const{
    switch (getType()){
    case DTSC_INT: return Bit::btohll(p + 1);
    case DTSC_STR:
      char *str;
      size_t strlen;
      getString(str, strlen);
      if (!strlen){return 0;}
      return strtoll(str, 0, 0);
    default: return 0;
    }
  }

  /// Returns the string value of this DTSC string value.
  /// Uses getString internally, if a string.
  /// Converts integer values to strings.
  /// Returns an empty string on error.
  std::string Scan::asString() const{
    switch (getType()){
    case DTSC_INT:{
      std::stringstream st;
      st << asInt();
      return st.str();
    }break;
    case DTSC_STR:{
      char *str;
      size_t strlen;
      getString(str, strlen);
      return std::string(str, strlen);
    }break;
    }
    return "";
  }

  /// Sets result to a pointer to the string, and strlen to the length of it.
  /// Sets both to zero if this isn't a DTSC string value.
  /// Attempts absolutely no conversion.
  void Scan::getString(char *&result, size_t &strlen) const{
    if (getType() == DTSC_STR){
      result = p + 5;
      strlen = Bit::btohl(p + 1);
      return;
    }
    result = 0;
    strlen = 0;
  }

  /// Returns the DTSC scan object as a JSON value
  /// Returns an empty object on error.
  JSON::Value Scan::asJSON() const{
    JSON::Value result;
    unsigned int i = 0;
    JSON::fromDTMI(p, len, i, result);
    return result;
  }

  /// \todo Move this function to some generic area. Duplicate from json.cpp
  static inline char hex2c(char c){
    if (c < 10){return '0' + c;}
    if (c < 16){return 'A' + (c - 10);}
    return '0';
  }

  /// \todo Move this function to some generic area. Duplicate from json.cpp
  static std::string string_escape(const std::string val){
    std::stringstream out;
    out << "\"";
    for (size_t i = 0; i < val.size(); ++i){
      switch (val.data()[i]){
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (val.data()[i] < 32 || val.data()[i] > 126){
          out << "\\u00";
          out << hex2c((val.data()[i] >> 4) & 0xf);
          out << hex2c(val.data()[i] & 0xf);
        }else{
          out << val.data()[i];
        }
        break;
      }
    }
    out << "\"";
    return out.str();
  }

  std::string Scan::toPrettyString(size_t indent) const{
    switch (getType()){
    case DTSC_STR:{
      uint32_t strlen = Bit::btohl(p + 1);
      if (strlen > 250){
        std::stringstream ret;
        ret << "\"" << strlen << " bytes of data\"";
        return ret.str();
      }
      return string_escape(asString());
    }
    case DTSC_INT:{
      std::stringstream ret;
      ret << asInt();
      return ret.str();
    }
    case DTSC_OBJ:
    case DTSC_CON:{
      std::stringstream ret;
      ret << "{" << std::endl;
      indent += 2;
      char *i = p + 1;
      bool first = true;
      // object, scan contents
      while (i[0] + i[1] != 0 && i < p + len){// while not encountering 0x0000 (we assume
                                                // 0x0000EE)
        if (i + 2 >= p + len){
          indent -= 2;
          ret << std::string(indent, ' ') << "}//walked out of object here";
          return ret.str();
        }
        if (!first){ret << "," << std::endl;}
        first = false;
        uint16_t strlen = Bit::btohs(i);
        i += 2;
        ret << std::string(indent, ' ') << "\"" << std::string(i, strlen)
            << "\": " << Scan(i + strlen, len - (i - p)).toPrettyString(indent);
        i = skipDTSC(i + strlen, p + len);
        if (!i){
          indent -= 2;
          ret << std::string(indent, ' ') << "}//could not locate next object";
          return ret.str();
        }
      }
      indent -= 2;
      ret << std::endl << std::string(indent, ' ') << "}";
      return ret.str();
    }
    case DTSC_ARR:{
      std::stringstream ret;
      ret << "[" << std::endl;
      indent += 2;
      Scan tmpScan;
      unsigned int i = 0;
      bool first = true;
      do{
        tmpScan = getIndice(i++);
        if (tmpScan.getType()){
          if (!first){ret << "," << std::endl;}
          first = false;
          ret << std::string(indent, ' ') << tmpScan.toPrettyString(indent);
        }
      }while (tmpScan.getType());
      indent -= 2;
      ret << std::endl << std::string(indent, ' ') << "]";
      return ret.str();
    }
    default: return "Error";
    }
  }

  /// Initialize metadata from referenced DTSC::Scan object in master mode.
  Meta::Meta(const std::string &_streamName, const DTSC::Scan &src){
    version = DTSH_VERSION;
    streamMemBuf = 0;
    isMemBuf = false;
    isMaster = true;
    reInit(_streamName, src);
  }

  /// Initialize empty metadata, in master or slave mode.
  /// If stream name is empty, slave mode is enforced.
  Meta::Meta(const std::string &_streamName, bool master){
    if (!_streamName.size()){master = false;}
    version = DTSH_VERSION;
    streamMemBuf = 0;
    isMemBuf = false;
    isMaster = master;
    reInit(_streamName, master);
  }

  /// Initialize metadata from given DTSH file in master mode.
  Meta::Meta(const std::string &_streamName, const std::string &fileName){
    version = DTSH_VERSION;
    streamMemBuf = 0;
    isMemBuf = false;
    isMaster = true;
    reInit(_streamName, fileName);
  }

  void Meta::setMaster(bool _master){isMaster = _master;}

  bool Meta::getMaster() const{return isMaster;}

  /// Calls clear(), then initializes freshly.
  /// If stream name is set, uses shared memory backing.
  /// If stream name is empty, uses non-shared memory backing.
  void Meta::reInit(const std::string &_streamName, bool master){
    clear();
    if (_streamName == ""){
      sBufMem();
    }else{
      sBufShm(_streamName, DEFAULT_TRACK_COUNT, master);
    }
    streamInit();
  }

  /// Calls clear(), then initializes from given DTSH file in master mode.
  /// Internally calls reInit(const std::string&, const DTSC::Scan&).
  /// If stream name is set, uses shared memory backing.
  /// If stream name is empty, uses non-shared memory backing.
  void Meta::reInit(const std::string &_streamName, const std::string &fileName){
    clear();

    ///\todo Implement absence of keysizes here instead of input::parseHeader
    std::ifstream inFile(fileName.c_str());
    if (!inFile.is_open()){return;}
    inFile.seekg(0, std::ios_base::end);
    size_t fileSize = inFile.tellg();
    inFile.seekg(0, std::ios_base::beg);

    char *scanBuf = (char *)malloc(fileSize);
    inFile.read(scanBuf, fileSize);

    inFile.close();

    size_t offset = 8;
    if (!memcmp(scanBuf, "DTP2", 4)){offset = 20;}

    DTSC::Scan src(scanBuf + offset, fileSize - offset);
    reInit(_streamName, src);
    free(scanBuf);
  }

  /// Calls clear(), then initializes from the given DTSC:Scan object in master mode.
  /// If stream name is set, uses shared memory backing.
  /// If stream name is empty, uses non-shared memory backing.
  void Meta::reInit(const std::string &_streamName, const DTSC::Scan &src){
    clear();

    if (_streamName == ""){
      sBufMem();
    }else{
      sBufShm(_streamName, DEFAULT_TRACK_COUNT, true);
    }
    streamInit();

    setVod(src.hasMember("vod") && src.getMember("vod").asInt());
    setLive(src.hasMember("live") && src.getMember("live").asInt());

    version = src.getMember("version").asInt();

    if (src.hasMember("inputLocalVars")){
      inputLocalVars = JSON::fromString(src.getMember("inputLocalVars").asString());
    }

    size_t tNum = src.getMember("tracks").getSize();
    for (int i = 0; i < tNum; i++){
      addTrackFrom(src.getMember("tracks").getIndice(i));
    }

    // Unix Time at zero point of a stream
    if (src.hasMember("unixzero")){
      setBootMsOffset(src.getMember("unixzero").asInt() - Util::unixMS() + Util::bootMS());
    }else{
      MEDIUM_MSG("No member \'unixzero\' found in DTSC::Scan. Calculating locally.");
      int64_t lastMs = 0;
      for (std::map<size_t, Track>::iterator it = tracks.begin(); it != tracks.end(); it++){
        if (it->second.track.getInt(it->second.trackLastmsField) > lastMs){
          lastMs = it->second.track.getInt(it->second.trackLastmsField);
        }
      }
      setBootMsOffset(Util::bootMS() - lastMs);
    }
  }

  void Meta::addTrackFrom(const DTSC::Scan &trak){
    char *fragStor = 0;
    char *keyStor = 0;
    char *partStor = 0;
    char *keySizeStor = 0;
    size_t fragLen = 0;
    size_t keyLen = 0;
    size_t partLen = 0;
    size_t keySizeLen = 0;
    uint32_t fragCount = DEFAULT_FRAGMENT_COUNT;
    uint32_t keyCount  = DEFAULT_KEY_COUNT;
    uint32_t partCount = DEFAULT_PART_COUNT;

    if (trak.hasMember("fragments") && trak.hasMember("keys") && trak.hasMember("parts") && trak.hasMember("keysizes")){
      trak.getMember("fragments").getString(fragStor, fragLen);
      trak.getMember("keys").getString(keyStor, keyLen);
      trak.getMember("parts").getString(partStor, partLen);
      trak.getMember("keysizes").getString(keySizeStor, keySizeLen);
      fragCount = fragLen / DTSH_FRAGMENT_SIZE;
      keyCount = keyLen / DTSH_KEY_SIZE;
      partCount = partLen / DTSH_PART_SIZE;
    }
    size_t tIdx = addTrack(fragCount, keyCount, partCount);

    setType(tIdx, trak.getMember("type").asString());
    setCodec(tIdx, trak.getMember("codec").asString());
    setInit(tIdx, trak.getMember("init").asString());
    setLang(tIdx, trak.getMember("lang").asString());
    setID(tIdx, trak.getMember("trackid").asInt());
    setFirstms(tIdx, trak.getMember("firstms").asInt());
    setLastms(tIdx, trak.getMember("lastms").asInt());
    setBps(tIdx, trak.getMember("bps").asInt());
    setMaxBps(tIdx, trak.getMember("maxbps").asInt());
    setSourceTrack(tIdx, INVALID_TRACK_ID);
    if (trak.getMember("type").asString() == "video"){
      setWidth(tIdx, trak.getMember("width").asInt());
      setHeight(tIdx, trak.getMember("height").asInt());
      setFpks(tIdx, trak.getMember("fpks").asInt());

    }else if (trak.getMember("type").asString() == "audio"){
      // rate channels size
      setRate(tIdx, trak.getMember("rate").asInt());
      setChannels(tIdx, trak.getMember("channels").asInt());
      setSize(tIdx, trak.getMember("size").asInt());
    }

    //Do not parse any of the more complex data, if any of it is missing.
    if (!fragLen || !keyLen || !partLen || !keySizeLen){return;}

    //Ok, we have data, let's parse it, too.
    Track &s = tracks[tIdx];
    uint64_t *vals = (uint64_t *)malloc(4 * fragCount * sizeof(uint64_t));
    for (int i = 0; i < fragCount; i++){
      char *ptr = fragStor + (i * DTSH_FRAGMENT_SIZE);
      vals[i] = Bit::btohl(ptr);
      vals[fragCount + i] = ptr[4];
      vals[(2 * fragCount) + i] = Bit::btohl(ptr + 5) - 1;
      vals[(3 * fragCount) + i] = Bit::btohl(ptr + 9);
    }
    s.fragments.setInts("duration", vals, fragCount);
    s.fragments.setInts("keys", vals + fragCount, fragCount);
    s.fragments.setInts("firstkey", vals + (2 * fragCount), fragCount);
    s.fragments.setInts("size", vals + (3 * fragCount), fragCount);
    s.fragments.addRecords(fragCount);

    vals = (uint64_t *)realloc(vals, 7 * keyCount * sizeof(uint64_t));
    uint64_t totalPartCount = 0;
    for (int i = 0; i < keyCount; i++){
      char *ptr = keyStor + (i * DTSH_KEY_SIZE);
      vals[i] = Bit::btohll(ptr);
      vals[keyCount + i] = Bit::btoh24(ptr + 8);
      vals[(2 * keyCount) + i] = Bit::btohl(ptr + 11);
      vals[(3 * keyCount) + i] = Bit::btohs(ptr + 15);
      vals[(4 * keyCount) + i] = Bit::btohll(ptr + 17);
      vals[(5 * keyCount) + i] = Bit::btohl(keySizeStor + (i * 4)); // NOT WITH ptr!!
      vals[(6 * keyCount) + i] = totalPartCount;
      totalPartCount += vals[(3 * keyCount) + i];
    }
    s.keys.setInts("bpos", vals, keyCount);
    s.keys.setInts("duration", vals + keyCount, keyCount);
    s.keys.setInts("number", vals + (2 * keyCount), keyCount);
    s.keys.setInts("parts", vals + (3 * keyCount), keyCount);
    s.keys.setInts("time", vals + (4 * keyCount), keyCount);
    s.keys.setInts("size", vals + (5 * keyCount), keyCount);
    s.keys.setInts("firstpart", vals + (6 * keyCount), keyCount);
    s.keys.addRecords(keyCount);

    vals = (uint64_t *)realloc(vals, 3 * partCount * sizeof(uint64_t));
    for (int i = 0; i < partCount; i++){
      char *ptr = partStor + (i * DTSH_PART_SIZE);
      vals[i] = Bit::btoh24(ptr);
      vals[partCount + i] = Bit::btoh24(ptr + 3);
      vals[(2 * partCount) + i] = Bit::btoh24(ptr + 6);
    }
    s.parts.setInts("size", vals, partCount);
    s.parts.setInts("duration", vals + partCount, partCount);
    s.parts.setInts("offset", vals + (2 * partCount), partCount);
    s.parts.addRecords(partCount);
    free(vals);
  }

  /// Simply calls clear()
  Meta::~Meta(){clear();}

  /// Switches the object to non-shared memory backed mode, with enough room for the given track
  /// count. Should not be called repeatedly, nor to switch modes.
  void Meta::sBufMem(size_t trackCount){
    size_t bufferSize = META_META_OFFSET + META_TRACK_OFFSET + META_META_RECORDSIZE +
                        (trackCount * META_TRACK_RECORDSIZE);
    isMemBuf = true;
    streamMemBuf = (char *)malloc(bufferSize);
    memset(streamMemBuf, 0, bufferSize);
    stream = Util::RelAccX(streamMemBuf, false);
  }

  /// Initializes shared memory backed mode, with enough room for the given track count.
  /// Should not be called repeatedly, nor to switch modes.
  void Meta::sBufShm(const std::string &_streamName, size_t trackCount, bool master){
    isMaster = master;
    if (isMaster){HIGH_MSG("Creating meta page for stream %s", _streamName.c_str());}

    size_t bufferSize = META_META_OFFSET + META_TRACK_OFFSET + META_META_RECORDSIZE +
                        (trackCount * META_TRACK_RECORDSIZE);

    isMemBuf = false;
    streamName = _streamName;

    char pageName[NAME_BUFFER_SIZE];
    snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_META, streamName.c_str());

    if (master){
      streamPage.init(pageName, bufferSize, false, false);
      if (streamPage.mapped){
        FAIL_MSG("Re-claiming page %s", pageName);
        BACKTRACE;
      }else{
        streamPage.init(pageName, bufferSize, true);
      }
      streamPage.master = false;
      stream = Util::RelAccX(streamPage.mapped, false);
    }else{
      streamPage.init(pageName, bufferSize, false, true);
      if (!streamPage.mapped){
        INFO_MSG("Page %s not found", pageName);
        return;
      }
      stream = Util::RelAccX(streamPage.mapped, true);
    }
  }

  /// In master mode, creates and stores the fields for the "stream" child object.
  /// In slave mode, simply class refresh().
  /// Regardless, afterwards the internal RelAccXFieldData members are updated with their correct
  /// values.
  void Meta::streamInit(size_t trackCount){
    if (isMaster){
      ///\todo Add safety for non-initialized stream object;
      stream.addField("vod", RAX_UINT);
      stream.addField("live", RAX_UINT);
      stream.addField("tracks", RAX_NESTED, META_TRACK_OFFSET + (trackCount * META_TRACK_RECORDSIZE));
      stream.addField("source", RAX_STRING, 512);
      stream.addField("maxkeepaway", RAX_16UINT);
      stream.addField("bufferwindow", RAX_64UINT);
      stream.addField("bootmsoffset", RAX_64INT);
      stream.addField("utcoffset", RAX_64INT);
      stream.addField("minfragduration", RAX_64UINT);
      stream.setRCount(1);
      stream.setReady();
      stream.addRecords(1);

      trackList = Util::RelAccX(stream.getPointer("tracks"), false);
      trackList.addField("valid", RAX_UINT);
      trackList.addField("id", RAX_32UINT);
      trackList.addField("type", RAX_32STRING);
      trackList.addField("codec", RAX_32STRING);
      trackList.addField("page", RAX_256STRING);
      trackList.addField("lastupdate", RAX_64UINT);
      trackList.addField("pid", RAX_32UINT);
      trackList.addField("minkeepaway", RAX_64UINT);
      trackList.addField("sourcetid", RAX_32UINT);
      trackList.addField("encryption", RAX_256STRING);
      trackList.addField("ivec", RAX_64UINT);
      trackList.addField("widevine", RAX_256STRING);
      trackList.addField("playready", RAX_STRING, 1024);

      trackList.setRCount(trackCount);
      trackList.setReady();
    }else{
      refresh();
    }
    // Initialize internal bufferFields
    streamVodField = stream.getFieldData("vod");
    streamLiveField = stream.getFieldData("live");
    streamSourceField = stream.getFieldData("source");
    streamMaxKeepAwayField = stream.getFieldData("maxkeepaway");
    streamBufferWindowField = stream.getFieldData("bufferwindow");
    streamBootMsOffsetField = stream.getFieldData("bootmsoffset");
    streamUTCOffsetField = stream.getFieldData("utcoffset");
    streamMinimumFragmentDurationField = stream.getFieldData("minfragduration");

    trackValidField = trackList.getFieldData("valid");
    trackIdField = trackList.getFieldData("id");
    trackTypeField = trackList.getFieldData("type");
    trackCodecField = trackList.getFieldData("codec");
    trackPageField = trackList.getFieldData("page");
    trackLastUpdateField = trackList.getFieldData("lastupdate");
    trackPidField = trackList.getFieldData("pid");
    trackMinKeepAwayField = trackList.getFieldData("minkeepaway");
    trackSourceTidField = trackList.getFieldData("sourcetid");
    trackEncryptionField = trackList.getFieldData("encryption");
    trackIvecField = trackList.getFieldData("ivec");
    trackWidevineField = trackList.getFieldData("widevine");
    trackPlayreadyField = trackList.getFieldData("playready");
  }

  /// Reads the "tracks" field from the "stream" child object, populating the "tracks" variable.
  /// Does not clear "tracks" beforehand, so it may contain stale information afterwards if it was
  /// already populated.
  void Meta::refresh(){
    if (!stream.isReady() || !stream.getPointer("tracks")){
      INFO_MSG("No track pointer, not refreshing.");
      return;
    }
    trackList = Util::RelAccX(stream.getPointer("tracks"), false);
    for (size_t i = 0; i < trackList.getPresent(); i++){
      if (trackList.getInt("valid", i) == 0){continue;}
      if (tracks.count(i)){continue;}
      IPC::sharedPage &p = tM[i];
      p.init(trackList.getPointer("page", i), SHM_STREAM_TRACK_LEN, false, false);

      Track &t = tracks[i];
      t.track = Util::RelAccX(p.mapped, true);
      t.parts = Util::RelAccX(t.track.getPointer("parts"), true);
      t.keys = Util::RelAccX(t.track.getPointer("keys"), true);
      t.fragments = Util::RelAccX(t.track.getPointer("fragments"), true);
      t.pages = Util::RelAccX(t.track.getPointer("pages"), true);

      t.trackIdField = t.track.getFieldData("id");
      t.trackTypeField = t.track.getFieldData("type");
      t.trackCodecField = t.track.getFieldData("codec");
      t.trackFirstmsField = t.track.getFieldData("firstms");
      t.trackLastmsField = t.track.getFieldData("lastms");
      t.trackBpsField = t.track.getFieldData("bps");
      t.trackMaxbpsField = t.track.getFieldData("maxbps");
      t.trackLangField = t.track.getFieldData("lang");
      t.trackInitField = t.track.getFieldData("init");
      t.trackRateField = t.track.getFieldData("rate");
      t.trackSizeField = t.track.getFieldData("size");
      t.trackChannelsField = t.track.getFieldData("channels");
      t.trackWidthField = t.track.getFieldData("width");
      t.trackHeightField = t.track.getFieldData("height");
      t.trackFpksField = t.track.getFieldData("fpks");
      t.trackMissedFragsField = t.track.getFieldData("missedFrags");

      t.partSizeField = t.parts.getFieldData("size");
      t.partDurationField = t.parts.getFieldData("duration");
      t.partOffsetField = t.parts.getFieldData("offset");

      t.keyFirstPartField = t.keys.getFieldData("firstpart");
      t.keyBposField = t.keys.getFieldData("bpos");
      t.keyDurationField = t.keys.getFieldData("duration");
      t.keyNumberField = t.keys.getFieldData("number");
      t.keyPartsField = t.keys.getFieldData("parts");
      t.keyTimeField = t.keys.getFieldData("time");
      t.keySizeField = t.keys.getFieldData("size");

      t.fragmentDurationField = t.fragments.getFieldData("duration");
      t.fragmentKeysField = t.fragments.getFieldData("keys");
      t.fragmentFirstKeyField = t.fragments.getFieldData("firstkey");
      t.fragmentSizeField = t.fragments.getFieldData("size");
    }
  }

  /// Reloads shared memory pages that are marked as needing an update, if any
  /// Returns true if a reload happened
  bool Meta::reloadReplacedPagesIfNeeded(){
    if (isMemBuf){return false;}//Only for shm-backed metadata
    if (!stream.isReady() || !stream.getPointer("tracks")){
      INFO_MSG("No track pointer, not refreshing.");
      return false;
    }
    char pageName[NAME_BUFFER_SIZE];

    if (stream.isReload() || stream.isExit()){
      INFO_MSG("Reloading entire metadata");
      streamPage.close();
      snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_META, streamName.c_str());
      streamPage.init(pageName, 0, false, true);
      if (!streamPage.mapped){
        INFO_MSG("Page %s not found", pageName);
        return true;
      }
      stream = Util::RelAccX(streamPage.mapped, true);
      tM.clear();
      tracks.clear();
      refresh();
      return true;
    }

    bool ret = false;
    for (size_t i = 0; i < trackList.getPresent(); i++){
      if (trackList.getInt("valid", i) == 0){continue;}
      bool always_load = !tracks.count(i);
      if (always_load || tracks[i].track.isReload()){
        ret = true;
        Track &t = tracks[i];
        if (always_load){
          VERYHIGH_MSG("Loading track: %s", trackList.getPointer("page", i));
        }else{
          VERYHIGH_MSG("Reloading track: %s", trackList.getPointer("page", i));
        }
        IPC::sharedPage &p = tM[i];
        p.init(trackList.getPointer("page", i), SHM_STREAM_TRACK_LEN, false, false);
        if (!p.mapped){
          WARN_MSG("Failed to load page %s, retrying later", trackList.getPointer("page", i));
          tM.erase(i);
          tracks.erase(i);
          continue;
        }

        t.track = Util::RelAccX(p.mapped, true);
        t.parts = Util::RelAccX(t.track.getPointer("parts"), true);
        t.keys = Util::RelAccX(t.track.getPointer("keys"), true);
        t.fragments = Util::RelAccX(t.track.getPointer("fragments"), true);
        t.pages = Util::RelAccX(t.track.getPointer("pages"), true);

        t.trackIdField = t.track.getFieldData("id");
        t.trackTypeField = t.track.getFieldData("type");
        t.trackCodecField = t.track.getFieldData("codec");
        t.trackFirstmsField = t.track.getFieldData("firstms");
        t.trackLastmsField = t.track.getFieldData("lastms");
        t.trackBpsField = t.track.getFieldData("bps");
        t.trackMaxbpsField = t.track.getFieldData("maxbps");
        t.trackLangField = t.track.getFieldData("lang");
        t.trackInitField = t.track.getFieldData("init");
        t.trackRateField = t.track.getFieldData("rate");
        t.trackSizeField = t.track.getFieldData("size");
        t.trackChannelsField = t.track.getFieldData("channels");
        t.trackWidthField = t.track.getFieldData("width");
        t.trackHeightField = t.track.getFieldData("height");
        t.trackFpksField = t.track.getFieldData("fpks");
        t.trackMissedFragsField = t.track.getFieldData("missedFrags");

        t.partSizeField = t.parts.getFieldData("size");
        t.partDurationField = t.parts.getFieldData("duration");
        t.partOffsetField = t.parts.getFieldData("offset");

        t.keyFirstPartField = t.keys.getFieldData("firstpart");
        t.keyBposField = t.keys.getFieldData("bpos");
        t.keyDurationField = t.keys.getFieldData("duration");
        t.keyNumberField = t.keys.getFieldData("number");
        t.keyPartsField = t.keys.getFieldData("parts");
        t.keyTimeField = t.keys.getFieldData("time");
        t.keySizeField = t.keys.getFieldData("size");

        t.fragmentDurationField = t.fragments.getFieldData("duration");
        t.fragmentKeysField = t.fragments.getFieldData("keys");
        t.fragmentFirstKeyField = t.fragments.getFieldData("firstkey");
        t.fragmentSizeField = t.fragments.getFieldData("size");

      }
    }
    return ret;
  }

  /// Merges in track information from a given DTSC::Meta object, optionally deleting missing tracks
  /// and optionally making hard copies of the original data.
  void Meta::merge(const DTSC::Meta &M, bool deleteTracks, bool copyData){
    std::set<size_t> editedTracks;
    std::set<size_t> newTracks = M.getValidTracks();
    // Detect new tracks
    for (std::set<size_t>::iterator it = newTracks.begin(); it != newTracks.end(); it++){
      if (trackIDToIndex(M.getID(*it), getpid()) == INVALID_TRACK_ID){editedTracks.insert(*it);}
    }
    for (std::set<size_t>::iterator it = editedTracks.begin(); it != editedTracks.end(); it++){
      size_t fragCount = DEFAULT_FRAGMENT_COUNT;
      size_t keyCount = DEFAULT_KEY_COUNT;
      size_t partCount = DEFAULT_PART_COUNT;
      size_t pageCount = DEFAULT_PAGE_COUNT;
      if (copyData){
        fragCount = M.tracks.at(*it).fragments.getRCount();
        keyCount = M.tracks.at(*it).keys.getRCount();
        partCount = M.tracks.at(*it).parts.getRCount();
        pageCount = M.tracks.at(*it).pages.getRCount();
      }

      size_t newIdx = addTrack(fragCount, keyCount, partCount, pageCount);
      setInit(newIdx, M.getInit(*it));
      setID(newIdx, M.getID(*it));
      setChannels(newIdx, M.getChannels(*it));
      setRate(newIdx, M.getRate(*it));
      setWidth(newIdx, M.getWidth(*it));
      setHeight(newIdx, M.getHeight(*it));
      setSize(newIdx, M.getSize(*it));
      setType(newIdx, M.getType(*it));
      setCodec(newIdx, M.getCodec(*it));
      setLang(newIdx, M.getLang(*it));
      if (copyData){
        setFirstms(newIdx, M.getFirstms(*it));
        setLastms(newIdx, M.getLastms(*it));
      }else{
        setFirstms(newIdx, 0);
        setLastms(newIdx, 0);
      }
      setBps(newIdx, M.getBps(*it));
      setMaxBps(newIdx, M.getMaxBps(*it));
      setFpks(newIdx, M.getFpks(*it));
      setMissedFragments(newIdx, M.getMissedFragments(*it));
      setMinKeepAway(newIdx, M.getMinKeepAway(*it));
      setSourceTrack(newIdx, M.getSourceTrack(*it));
      setEncryption(newIdx, M.getEncryption(*it));
      setPlayReady(newIdx, M.getPlayReady(*it));
      setWidevine(newIdx, M.getWidevine(*it));
      setIvec(newIdx, M.getIvec(*it));
      if (copyData){tracks[newIdx].track.flowFrom(M.tracks.at(*it).track);}
    }

    if (deleteTracks){
      editedTracks.clear();
      std::set<size_t> validTracks = getValidTracks();
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
        if (M.trackIDToIndex(getID(*it), getpid()) == INVALID_TRACK_ID){
          editedTracks.insert(*it);
        }
      }
      for (std::set<size_t>::iterator it = editedTracks.begin(); it != editedTracks.end(); it++){
        removeTrack(*it);
      }
    }
  }

  /// Evaluates to true if this is a shared-memory-backed object, correctly mapped, with a non-exit
  /// state on the "stream" RelAccX page.
  Meta::operator bool() const{
    return (!isMemBuf && streamPage.mapped && !stream.isExit()) || (isMemBuf && streamMemBuf && !stream.isExit());
  }

  /// Intended to be used for encryption. Not currently called anywhere.
  size_t Meta::addCopy(size_t sourceTrack){
    if (isMemBuf){
      WARN_MSG("Unsupported operation for in-memory streams");
      return INVALID_TRACK_ID;
    }
    size_t tNumber = trackList.getPresent();

    Track &t = tracks[tNumber];

    char pageName[NAME_BUFFER_SIZE];
    snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_TM, streamName.c_str(), getpid(), tNumber);
    INFO_MSG("Allocating page %s", pageName);
    tM[tNumber].init(pageName, tM[sourceTrack].len, true);
    tM[tNumber].master = false;

    memcpy(tM[tNumber].mapped, tM[sourceTrack].mapped, tM[sourceTrack].len);
    t.track = Util::RelAccX(tM[tNumber].mapped, true);

    t.parts = Util::RelAccX(t.track.getPointer("parts"), true);
    t.keys = Util::RelAccX(t.track.getPointer("keys"), true);
    t.fragments = Util::RelAccX(t.track.getPointer("fragments"), true);
    t.pages = Util::RelAccX(t.track.getPointer("pages"), true);

    trackList.setString(trackPageField, pageName, tNumber);
    trackList.setInt(trackPidField, getpid(), tNumber);
    trackList.setInt(trackSourceTidField, sourceTrack, tNumber);
    trackList.addRecords(1);
    validateTrack(tNumber);
    return tNumber;
  }

  /// Resizes a given track to be able to hold the given amount of fragments, keys, parts and pages.
  /// Currently called exclusively from Meta::update(), to resize the internal structures.
  void Meta::resizeTrack(size_t source, size_t fragCount, size_t keyCount, size_t partCount, size_t pageCount, const char * reason){
    IPC::semaphore resizeLock;

    if (!isMemBuf){
      std::string pageName = "/";
      pageName += trackList.getPointer(trackPageField, source);
      resizeLock.open(pageName.c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
      resizeLock.wait();
    }

    size_t pageSize = (isMemBuf ? sizeMemBuf[source] : tM[source].len);

    char *orig = (char *)malloc(pageSize);
    if (!orig){
      FAIL_MSG("Failed to re-allocate memory for track %zu: %s", source, strerror(errno));
      return;
    }
    memcpy(orig, (isMemBuf ? tMemBuf[source] : tM[source].mapped), pageSize);

    Track &t = tracks[source];
    t.track.setReload();

    size_t newPageSize = TRACK_TRACK_OFFSET + TRACK_TRACK_RECORDSIZE +
                         (TRACK_FRAGMENT_OFFSET + (TRACK_FRAGMENT_RECORDSIZE * fragCount)) +
                         (TRACK_KEY_OFFSET + (TRACK_KEY_RECORDSIZE * keyCount)) +
                         (TRACK_PART_OFFSET + (TRACK_PART_RECORDSIZE * partCount)) +
                         (TRACK_PAGE_OFFSET + (TRACK_PAGE_RECORDSIZE * pageCount));

    if (isMemBuf){
      free(tMemBuf[source]);
      tMemBuf.erase(source);
      tMemBuf[source] = (char *)malloc(newPageSize);
      if (!tMemBuf[source]){
        FAIL_MSG("Failed to re-allocate memory for track %zu: %s", source, strerror(errno));
        resizeLock.unlink();
        return;
      }
      sizeMemBuf[source] = newPageSize;
      memset(tMemBuf[source], 0, newPageSize);
      t.track = Util::RelAccX(tMemBuf[source], false);
    }else{
      tM[source].master = true;
      tM[source].init(trackList.getPointer(trackPageField, source), newPageSize, true);
      if (!tM[source].mapped){
        FAIL_MSG("Failed to re-allocate shared memory for track %zu: %s", source, strerror(errno));
        resizeLock.unlink();
        return;
      }
      tM[source].master = false;

      t.track = Util::RelAccX(tM[source].mapped, false);
    }
    initializeTrack(t, fragCount, keyCount, partCount, pageCount);

    Util::RelAccX origAccess(orig);
    Util::RelAccX origFragments(origAccess.getPointer("fragments"));
    Util::RelAccX origKeys(origAccess.getPointer("keys"));
    Util::RelAccX origParts(origAccess.getPointer("parts"));
    Util::RelAccX origPages(origAccess.getPointer("pages"));

    MEDIUM_MSG("Track %zu resizing (reason: %s): frags %" PRIu32 "->%zu, keys %" PRIu32 "->%zu, parts %" PRIu32 "->%zu, pages %" PRIu32 "->%zu",
             source, reason,
             origFragments.getRCount(), fragCount,
             origKeys.getRCount(), keyCount,
             origParts.getRCount(), partCount,
             origPages.getRCount(), pageCount);

    t.track.setInt(t.trackIdField, origAccess.getInt("id"));
    t.track.setString(t.trackTypeField, origAccess.getPointer("type"));
    t.track.setString(t.trackCodecField, origAccess.getPointer("codec"));
    t.track.setInt(t.trackFirstmsField, origAccess.getInt("firstms"));
    t.track.setInt(t.trackLastmsField, origAccess.getInt("lastms"));
    t.track.setInt(t.trackBpsField, origAccess.getInt("bps"));
    t.track.setInt(t.trackMaxbpsField, origAccess.getInt("maxbps"));
    t.track.setString(t.trackLangField, origAccess.getPointer("lang"));
    memcpy(t.track.getPointer(t.trackInitField), origAccess.getPointer("init"), 1024 * 1024);
    t.track.setInt(t.trackRateField, origAccess.getInt("rate"));
    t.track.setInt(t.trackSizeField, origAccess.getInt("size"));
    t.track.setInt(t.trackChannelsField, origAccess.getInt("channels"));
    t.track.setInt(t.trackWidthField, origAccess.getInt("width"));
    t.track.setInt(t.trackHeightField, origAccess.getInt("height"));
    t.track.setInt(t.trackFpksField, origAccess.getInt("fpks"));
    t.track.setInt(t.trackMissedFragsField, origAccess.getInt("missedFrags"));

    t.parts.setEndPos(origParts.getEndPos());
    t.parts.setStartPos(origParts.getStartPos());
    t.parts.setDeleted(origParts.getDeleted());
    t.parts.setPresent(origParts.getPresent());

    Util::FieldAccX origPartSizeAccX = origParts.getFieldAccX("size");
    Util::FieldAccX origPartDurationAccX = origParts.getFieldAccX("duration");
    Util::FieldAccX origPartOffsetAccX = origParts.getFieldAccX("offset");

    Util::FieldAccX partSizeAccX = t.parts.getFieldAccX("size");
    Util::FieldAccX partDurationAccX = t.parts.getFieldAccX("duration");
    Util::FieldAccX partOffsetAccX = t.parts.getFieldAccX("offset");

    size_t firstPart = origParts.getStartPos();
    size_t endPart = origParts.getEndPos();
    for (size_t i = firstPart; i < endPart; i++){
      partSizeAccX.set(origPartSizeAccX.uint(i), i);
      partDurationAccX.set(origPartDurationAccX.uint(i), i);
      partOffsetAccX.set(origPartOffsetAccX.uint(i), i);
    }

    t.keys.setEndPos(origKeys.getEndPos());
    t.keys.setStartPos(origKeys.getStartPos());
    t.keys.setDeleted(origKeys.getDeleted());
    t.keys.setPresent(origKeys.getPresent());

    Util::FieldAccX origKeyFirstpartAccX = origKeys.getFieldAccX("firstpart");
    Util::FieldAccX origKeyBposAccX = origKeys.getFieldAccX("bpos");
    Util::FieldAccX origKeyDurationAccX = origKeys.getFieldAccX("duration");
    Util::FieldAccX origKeyNumberAccX = origKeys.getFieldAccX("number");
    Util::FieldAccX origKeyPartsAccX = origKeys.getFieldAccX("parts");
    Util::FieldAccX origKeyTimeAccX = origKeys.getFieldAccX("time");
    Util::FieldAccX origKeySizeAccX = origKeys.getFieldAccX("size");

    Util::FieldAccX keyFirstpartAccX = t.keys.getFieldAccX("firstpart");
    Util::FieldAccX keyBposAccX = t.keys.getFieldAccX("bpos");
    Util::FieldAccX keyDurationAccX = t.keys.getFieldAccX("duration");
    Util::FieldAccX keyNumberAccX = t.keys.getFieldAccX("number");
    Util::FieldAccX keyPartsAccX = t.keys.getFieldAccX("parts");
    Util::FieldAccX keyTimeAccX = t.keys.getFieldAccX("time");
    Util::FieldAccX keySizeAccX = t.keys.getFieldAccX("size");

    size_t firstKey = origKeys.getStartPos();
    size_t endKey = origKeys.getEndPos();
    for (size_t i = firstKey; i < endKey; i++){
      keyFirstpartAccX.set(origKeyFirstpartAccX.uint(i), i);
      keyBposAccX.set(origKeyBposAccX.uint(i), i);
      keyDurationAccX.set(origKeyDurationAccX.uint(i), i);
      keyNumberAccX.set(origKeyNumberAccX.uint(i), i);
      keyPartsAccX.set(origKeyPartsAccX.uint(i), i);
      keyTimeAccX.set(origKeyTimeAccX.uint(i), i);
      keySizeAccX.set(origKeySizeAccX.uint(i), i);
    }

    t.fragments.setEndPos(origFragments.getEndPos());
    t.fragments.setStartPos(origFragments.getStartPos());
    t.fragments.setDeleted(origFragments.getDeleted());
    t.fragments.setPresent(origFragments.getPresent());

    Util::FieldAccX origFragmentDurationAccX = origFragments.getFieldAccX("duration");
    Util::FieldAccX origFragmentKeysAccX = origFragments.getFieldAccX("keys");
    Util::FieldAccX origFragmentFirstkeyAccX = origFragments.getFieldAccX("firstkey");
    Util::FieldAccX origFragmentSizeAccX = origFragments.getFieldAccX("size");

    Util::FieldAccX fragmentDurationAccX = t.fragments.getFieldAccX("duration");
    Util::FieldAccX fragmentKeysAccX = t.fragments.getFieldAccX("keys");
    Util::FieldAccX fragmentFirstkeyAccX = t.fragments.getFieldAccX("firstkey");
    Util::FieldAccX fragmentSizeAccX = t.fragments.getFieldAccX("size");

    size_t firstFragment = origFragments.getStartPos();
    size_t endFragment = origFragments.getEndPos();
    for (size_t i = firstFragment; i < endFragment; i++){
      fragmentDurationAccX.set(origFragmentDurationAccX.uint(i), i);
      fragmentKeysAccX.set(origFragmentKeysAccX.uint(i), i);
      fragmentFirstkeyAccX.set(origFragmentFirstkeyAccX.uint(i), i);
      fragmentSizeAccX.set(origFragmentSizeAccX.uint(i), i);
    }

    t.pages.setEndPos(origPages.getEndPos());
    t.pages.setStartPos(origPages.getStartPos());
    t.pages.setDeleted(origPages.getDeleted());
    t.pages.setPresent(origPages.getPresent());

    Util::FieldAccX origPageFirstkeyAccX = origPages.getFieldAccX("firstkey");
    Util::FieldAccX origPageKeycountAccX = origPages.getFieldAccX("keycount");
    Util::FieldAccX origPagePartsAccX = origPages.getFieldAccX("parts");
    Util::FieldAccX origPageSizeAccX = origPages.getFieldAccX("size");
    Util::FieldAccX origPageAvailAccX = origPages.getFieldAccX("avail");
    Util::FieldAccX origPageFirsttimeAccX = origPages.getFieldAccX("firsttime");
    Util::FieldAccX origPageLastkeytimeAccX = origPages.getFieldAccX("lastkeytime");

    Util::FieldAccX pageFirstkeyAccX = t.pages.getFieldAccX("firstkey");
    Util::FieldAccX pageKeycountAccX = t.pages.getFieldAccX("keycount");
    Util::FieldAccX pagePartsAccX = t.pages.getFieldAccX("parts");
    Util::FieldAccX pageSizeAccX = t.pages.getFieldAccX("size");
    Util::FieldAccX pageAvailAccX = t.pages.getFieldAccX("avail");
    Util::FieldAccX pageFirsttimeAccX = t.pages.getFieldAccX("firsttime");
    Util::FieldAccX pageLastkeytimeAccX = t.pages.getFieldAccX("lastkeytime");

    size_t firstPage = origPages.getStartPos();
    size_t endPage = origPages.getEndPos();
    for (size_t i = firstPage; i < endPage; i++){
      pageFirstkeyAccX.set(origPageFirstkeyAccX.uint(i), i);
      pageKeycountAccX.set(origPageKeycountAccX.uint(i), i);
      pagePartsAccX.set(origPagePartsAccX.uint(i), i);
      pageSizeAccX.set(origPageSizeAccX.uint(i), i);
      pageAvailAccX.set(origPageAvailAccX.uint(i), i);
      pageFirsttimeAccX.set(origPageFirsttimeAccX.uint(i), i);
      pageLastkeytimeAccX.set(origPageLastkeytimeAccX.uint(i), i);
    }
    t.track.setReady();

    free(orig);
    resizeLock.unlink();
  }

  size_t Meta::addDelayedTrack(size_t fragCount, size_t keyCount, size_t partCount, size_t pageCount){
    return addTrack(fragCount, keyCount, partCount, pageCount, false);
  }

  /// Adds a track to the metadata structure.
  /// To be called from the various inputs/outputs whenever they want to add a track.
  size_t Meta::addTrack(size_t fragCount, size_t keyCount, size_t partCount, size_t pageCount, bool setValid){
    char pageName[NAME_BUFFER_SIZE];
    IPC::semaphore trackLock;
    if (!isMemBuf){
      snprintf(pageName, NAME_BUFFER_SIZE, SEM_TRACKLIST, streamName.c_str());
      trackLock.open(pageName, O_CREAT | O_RDWR, ACCESSPERMS, 1);
      if (!trackLock){
        FAIL_MSG("Could not open semaphore to add track!");
        return -1;
      }
      trackLock.wait();
      if (stream.isExit()){
        trackLock.post();
        FAIL_MSG("Not adding track: stream is shutting down");
        return -1;
      }
    }

    size_t pageSize = TRACK_TRACK_OFFSET + TRACK_TRACK_RECORDSIZE +
                      (TRACK_FRAGMENT_OFFSET + (TRACK_FRAGMENT_RECORDSIZE * fragCount)) +
                      (TRACK_KEY_OFFSET + (TRACK_KEY_RECORDSIZE * keyCount)) +
                      (TRACK_PART_OFFSET + (TRACK_PART_RECORDSIZE * partCount)) +
                      (TRACK_PAGE_OFFSET + (TRACK_PAGE_RECORDSIZE * pageCount));

    size_t tNumber = trackList.getPresent();

    snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_TM, streamName.c_str(), getpid(), tNumber);

    Track &t = tracks[tNumber];

    if (isMemBuf){
      tMemBuf[tNumber] = (char *)malloc(pageSize);
      sizeMemBuf[tNumber] = pageSize;
      memset(tMemBuf[tNumber], 0, pageSize);

      t.track = Util::RelAccX(tMemBuf[tNumber], false);
    }else{
      tM[tNumber].init(pageName, pageSize, true);
      tM[tNumber].master = false;

      t.track = Util::RelAccX(tM[tNumber].mapped, false);
    }
    initializeTrack(t, fragCount, keyCount, partCount, pageCount);
    t.track.setReady();
    trackList.setString(trackPageField, pageName, tNumber);
    trackList.setInt(trackPidField, getpid(), tNumber);
    trackList.setInt(trackSourceTidField, INVALID_TRACK_ID, tNumber);
    trackList.addRecords(1);
    if (setValid){validateTrack(tNumber, trackValidDefault);}
    if (!isMemBuf){trackLock.post();}
    return tNumber;
  }

  bool Meta::isClaimed(size_t trackIdx) const{
    return (trackList.getInt(trackPidField, trackIdx) != 0);
  }
  
  void Meta::claimTrack(size_t trackIdx){
    if (trackList.getInt(trackPidField, trackIdx) != 0){
      FAIL_MSG("Cannot claim track: already claimed by PID %" PRIu64, trackList.getInt(trackPidField, trackIdx));
      return;
    }
    trackList.setInt(trackPidField, getpid(), trackIdx);
  }
  
  void Meta::abandonTrack(size_t trackIdx){
    if (trackList.getInt(trackPidField, trackIdx) != getpid()){
      FAIL_MSG("Cannot abandon track: is claimed by PID %" PRIu64 ", not us", trackList.getInt(trackPidField, trackIdx));
      return;
    }
    trackList.setInt(trackPidField, 0, trackIdx);
  }

  /// Internal function that is called whenever a track is (re)written to the memory structures.
  /// Adds the needed fields and sets all the RelAccXFieldData members to point to them.
  void Meta::initializeTrack(Track &t, size_t fragCount, size_t keyCount, size_t partCount, size_t pageCount){
    t.track.addField("id", RAX_32UINT);
    t.track.addField("type", RAX_STRING, 8);
    t.track.addField("codec", RAX_STRING, 8);
    t.track.addField("firstms", RAX_64UINT);
    t.track.addField("lastms", RAX_64UINT);
    t.track.addField("bps", RAX_32UINT);
    t.track.addField("maxbps", RAX_32UINT);
    t.track.addField("lang", RAX_STRING, 4);
    t.track.addField("init", RAX_RAW, 1 * 1024 * 1024); // 1megabyte init data
    t.track.addField("rate", RAX_16UINT);
    t.track.addField("size", RAX_16UINT);
    t.track.addField("channels", RAX_16UINT);
    t.track.addField("width", RAX_32UINT);
    t.track.addField("height", RAX_32UINT);
    t.track.addField("fpks", RAX_16UINT);
    t.track.addField("missedFrags", RAX_32UINT);
    t.track.addField("parts", RAX_NESTED, TRACK_PART_OFFSET + (TRACK_PART_RECORDSIZE * partCount));
    t.track.addField("keys", RAX_NESTED, TRACK_KEY_OFFSET + (TRACK_KEY_RECORDSIZE * keyCount));
    t.track.addField("fragments", RAX_NESTED, TRACK_FRAGMENT_OFFSET + (TRACK_FRAGMENT_RECORDSIZE * fragCount));
    t.track.addField("pages", RAX_NESTED, TRACK_PAGE_OFFSET + (TRACK_PAGE_RECORDSIZE * pageCount));

    t.track.setRCount(1);
    t.track.addRecords(1);

    t.parts = Util::RelAccX(t.track.getPointer("parts"), false);
    t.parts.addField("size", RAX_32UINT);
    t.parts.addField("duration", RAX_16UINT);
    t.parts.addField("offset", RAX_16INT);
    t.parts.setRCount(partCount);
    t.parts.setReady();

    t.keys = Util::RelAccX(t.track.getPointer("keys"), false);
    t.keys.addField("firstpart", RAX_64UINT);
    t.keys.addField("bpos", RAX_64UINT);
    t.keys.addField("duration", RAX_32UINT);
    t.keys.addField("number", RAX_32UINT);
    t.keys.addField("parts", RAX_32UINT);
    t.keys.addField("time", RAX_64UINT);
    t.keys.addField("size", RAX_32UINT);
    t.keys.setRCount(keyCount);
    t.keys.setReady();

    t.fragments = Util::RelAccX(t.track.getPointer("fragments"), false);
    t.fragments.addField("duration", RAX_32UINT);
    t.fragments.addField("keys", RAX_16UINT);
    t.fragments.addField("firstkey", RAX_32UINT);
    t.fragments.addField("size", RAX_32UINT);
    t.fragments.setRCount(fragCount);
    t.fragments.setReady();

    t.trackIdField = t.track.getFieldData("id");
    t.trackTypeField = t.track.getFieldData("type");
    t.trackCodecField = t.track.getFieldData("codec");
    t.trackFirstmsField = t.track.getFieldData("firstms");
    t.trackLastmsField = t.track.getFieldData("lastms");
    t.trackBpsField = t.track.getFieldData("bps");
    t.trackMaxbpsField = t.track.getFieldData("maxbps");
    t.trackLangField = t.track.getFieldData("lang");
    t.trackInitField = t.track.getFieldData("init");
    t.trackRateField = t.track.getFieldData("rate");
    t.trackSizeField = t.track.getFieldData("size");
    t.trackChannelsField = t.track.getFieldData("channels");
    t.trackWidthField = t.track.getFieldData("width");
    t.trackHeightField = t.track.getFieldData("height");
    t.trackFpksField = t.track.getFieldData("fpks");
    t.trackMissedFragsField = t.track.getFieldData("missedFrags");

    t.partSizeField = t.parts.getFieldData("size");
    t.partDurationField = t.parts.getFieldData("duration");
    t.partOffsetField = t.parts.getFieldData("offset");

    t.keyFirstPartField = t.keys.getFieldData("firstpart");
    t.keyBposField = t.keys.getFieldData("bpos");
    t.keyDurationField = t.keys.getFieldData("duration");
    t.keyNumberField = t.keys.getFieldData("number");
    t.keyPartsField = t.keys.getFieldData("parts");
    t.keyTimeField = t.keys.getFieldData("time");
    t.keySizeField = t.keys.getFieldData("size");

    t.fragmentDurationField = t.fragments.getFieldData("duration");
    t.fragmentKeysField = t.fragments.getFieldData("keys");
    t.fragmentFirstKeyField = t.fragments.getFieldData("firstkey");
    t.fragmentSizeField = t.fragments.getFieldData("size");

    t.pages = Util::RelAccX(t.track.getPointer("pages"), false);
    t.pages.addField("firstkey", RAX_32UINT);
    t.pages.addField("keycount", RAX_32UINT);
    t.pages.addField("parts", RAX_32UINT);
    t.pages.addField("size", RAX_32UINT);
    t.pages.addField("avail", RAX_32UINT);
    t.pages.addField("firsttime", RAX_64UINT);
    t.pages.addField("lastkeytime", RAX_64UINT);
    t.pages.setRCount(pageCount);
    t.pages.setReady();
  }

  /// Sets the given track's init data.
  /// Simply calls setInit(size_t, const char *, size_t) using values from the referenced
  /// std::string.
  void Meta::setInit(size_t trackIdx, const std::string &init){
    setInit(trackIdx, init.data(), init.size());
  }

  /// Sets the given track's init data.setvod
  void Meta::setInit(size_t trackIdx, const char *init, size_t initLen){
    DTSC::Track &t = tracks.at(trackIdx);
    char *_init = t.track.getPointer(t.trackInitField);
    Bit::htobs(_init, initLen);
    memcpy(_init + 2, init, initLen);
  }

  /// Retrieves the given track's init data as std::string.
  std::string Meta::getInit(size_t idx) const{
    const DTSC::Track &t = tracks.at(idx);
    char *src = t.track.getPointer(t.trackInitField);
    uint16_t size = Bit::btohs(src);
    return std::string(src + 2, size);
  }

  void Meta::setSource(const std::string &src){stream.setString(streamSourceField, src);}
  std::string Meta::getSource() const{return stream.getPointer(streamSourceField);}

  void Meta::setID(size_t trackIdx, size_t id){
    trackList.setInt(trackIdField, id, trackIdx);
    DTSC::Track &t = tracks.at(trackIdx);
    t.track.setInt(t.trackIdField, id);
  }
  size_t Meta::getID(size_t trackIdx) const{return trackList.getInt(trackIdField, trackIdx);}

  /// Writes Util::bootSecs() to the track's last updated field.
  void Meta::markUpdated(size_t trackIdx){
    trackList.setInt(trackLastUpdateField, Util::bootSecs(), trackIdx);
  }

  /// Reads the track's last updated field, which should be the Util::bootSecs() value of the time
  /// of last update.
  uint64_t Meta::getLastUpdated(size_t trackIdx) const{
    return trackList.getInt(trackLastUpdateField, trackIdx);
  }

  /// Reads the most recently updated track last updated field, which should be the Util::bootSecs()
  /// value of the time of last update.
  uint64_t Meta::getLastUpdated() const{
    uint64_t ret = 0;
    std::set<size_t> validTracks = getValidTracks();
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
      uint64_t trackUp = getLastUpdated(*it);
      if (trackUp > ret){ret = trackUp;}
    }
    return ret;
  }

  void Meta::setChannels(size_t trackIdx, uint16_t channels){
    DTSC::Track &t = tracks.at(trackIdx);
    t.track.setInt(t.trackChannelsField, channels);
  }
  uint16_t Meta::getChannels(size_t trackIdx) const{
    const DTSC::Track &t = tracks.at(trackIdx);
    return t.track.getInt(t.trackChannelsField);
  }

  void Meta::setWidth(size_t trackIdx, uint32_t width){
    DTSC::Track &t = tracks.at(trackIdx);
    t.track.setInt(t.trackWidthField, width);
  }
  uint32_t Meta::getWidth(size_t trackIdx) const{
    const DTSC::Track &t = tracks.at(trackIdx);
    return t.track.getInt(t.trackWidthField);
  }

  void Meta::setHeight(size_t trackIdx, uint32_t height){
    DTSC::Track &t = tracks.at(trackIdx);
    t.track.setInt(t.trackHeightField, height);
  }
  uint32_t Meta::getHeight(size_t trackIdx) const{
    const DTSC::Track &t = tracks.at(trackIdx);
    return t.track.getInt(t.trackHeightField);
  }

  void Meta::setRate(size_t trackIdx, uint32_t rate){
    DTSC::Track &t = tracks.at(trackIdx);
    t.track.setInt(t.trackRateField, rate);
  }
  uint32_t Meta::getRate(size_t trackIdx) const{
    const DTSC::Track &t = tracks.at(trackIdx);
    return t.track.getInt(t.trackRateField);
  }

  void Meta::setSize(size_t trackIdx, uint16_t size){
    DTSC::Track &t = tracks.at(trackIdx);
    t.track.setInt(t.trackSizeField, size);
  }
  uint16_t Meta::getSize(size_t trackIdx) const{
    const DTSC::Track &t = tracks.at(trackIdx);
    return t.track.getInt(t.trackSizeField);
  }

  void Meta::setType(size_t trackIdx, const std::string &type){
    trackList.setString(trackTypeField, type, trackIdx);
    DTSC::Track &t = tracks.at(trackIdx);
    t.track.setString(t.trackTypeField, type);
  }
  std::string Meta::getType(size_t trackIdx) const{
    return trackList.getPointer(trackTypeField, trackIdx);
  }

  void Meta::setCodec(size_t trackIdx, const std::string &codec){
    trackList.setString(trackCodecField, codec, trackIdx);
    DTSC::Track &t = tracks.at(trackIdx);
    t.track.setString(t.trackCodecField, codec);
  }
  std::string Meta::getCodec(size_t trackIdx) const{
    return trackList.getPointer(trackCodecField, trackIdx);
  }

  void Meta::setLang(size_t trackIdx, const std::string &lang){
    DTSC::Track &t = tracks.at(trackIdx);
    t.track.setString(t.trackLangField, lang);
  }
  std::string Meta::getLang(size_t trackIdx) const{
    const DTSC::Track &t = tracks.at(trackIdx);
    if (!t.track.isReady()){return "";}
    return t.track.getPointer(t.trackLangField);
  }

  void Meta::setFirstms(size_t trackIdx, uint64_t firstms){
    DTSC::Track &t = tracks.at(trackIdx);
    t.track.setInt(t.trackFirstmsField, firstms);
  }
  uint64_t Meta::getFirstms(size_t trackIdx) const{
    const DTSC::Track &t = tracks.at(trackIdx);
    return t.track.getInt(t.trackFirstmsField);
  }

  void Meta::setLastms(size_t trackIdx, uint64_t lastms){
    DTSC::Track &t = tracks.at(trackIdx);
    t.track.setInt(t.trackLastmsField, lastms);
  }
  uint64_t Meta::getLastms(size_t trackIdx) const{
    const DTSC::Track &t = tracks.find(trackIdx)->second;
    return t.track.getInt(t.trackLastmsField);
  }

  uint64_t Meta::getDuration(size_t trackIdx) const{
    const DTSC::Track &t = tracks.at(trackIdx);
    return t.track.getInt(t.trackLastmsField) - t.track.getInt(t.trackFirstmsField);
  }

  void Meta::setBps(size_t trackIdx, uint64_t bps){
    DTSC::Track &t = tracks.at(trackIdx);
    t.track.setInt(t.trackBpsField, bps);
  }
  uint64_t Meta::getBps(size_t trackIdx) const{
    const DTSC::Track &t = tracks.at(trackIdx);
    return t.track.getInt(t.trackBpsField);
  }

  void Meta::setMaxBps(size_t trackIdx, uint64_t bps){
    DTSC::Track &t = tracks.at(trackIdx);
    t.track.setInt(t.trackMaxbpsField, bps);
  }
  uint64_t Meta::getMaxBps(size_t trackIdx) const{
    const DTSC::Track &t = tracks.at(trackIdx);
    return t.track.getInt(t.trackMaxbpsField);
  }

  void Meta::setFpks(size_t trackIdx, uint64_t bps){
    DTSC::Track &t = tracks.at(trackIdx);
    t.track.setInt(t.trackFpksField, bps);
  }
  uint64_t Meta::getFpks(size_t trackIdx) const{
    const DTSC::Track &t = tracks.at(trackIdx);
    return t.track.getInt(t.trackFpksField);
  }

  void Meta::setMissedFragments(size_t trackIdx, uint32_t bps){
    DTSC::Track &t = tracks.at(trackIdx);
    t.track.setInt(t.trackMissedFragsField, bps);
  }
  uint32_t Meta::getMissedFragments(size_t trackIdx) const{
    const DTSC::Track &t = tracks.at(trackIdx);
    return t.track.getInt(t.trackMissedFragsField);
  }

  void Meta::setMinKeepAway(size_t trackIdx, uint64_t minKeepAway){
    trackList.setInt(trackMinKeepAwayField, minKeepAway, trackIdx);
  }

  uint64_t Meta::getMinKeepAway(size_t trackIdx) const{
    return trackList.getInt(trackMinKeepAwayField, trackIdx);
  }

  void Meta::setMaxKeepAway(uint64_t maxKeepAway){
    stream.setInt(streamMaxKeepAwayField, maxKeepAway);
  }

  uint64_t Meta::getMaxKeepAway() const{
    return stream.getInt(streamMaxKeepAwayField);
  }

  void Meta::setEncryption(size_t trackIdx, const std::string &encryption){
    trackList.setString(trackEncryptionField, encryption, trackIdx);
  }
  std::string Meta::getEncryption(size_t trackIdx) const{
    return trackList.getPointer(trackEncryptionField, trackIdx);
  }

  void Meta::setWidevine(size_t trackIdx, const std::string &widevine){
    trackList.setString(trackWidevineField, widevine, trackIdx);
  }
  std::string Meta::getWidevine(size_t trackIdx) const{
    return trackList.getPointer(trackWidevineField, trackIdx);
  }

  void Meta::setPlayReady(size_t trackIdx, const std::string &playReady){
    trackList.setString(trackPlayreadyField, playReady, trackIdx);
  }
  std::string Meta::getPlayReady(size_t trackIdx) const{
    return trackList.getPointer(trackPlayreadyField, trackIdx);
  }

  void Meta::setIvec(size_t trackIdx, uint64_t ivec){
    trackList.setInt(trackIvecField, ivec, trackIdx);
  }
  uint64_t Meta::getIvec(size_t trackIdx) const{
    return trackList.getInt(trackIvecField, trackIdx);
  }

  void Meta::setSourceTrack(size_t trackIdx, size_t sourceTrack){
    trackList.setInt(trackSourceTidField, sourceTrack, trackIdx);
  }
  uint64_t Meta::getSourceTrack(size_t trackIdx) const{
    return trackList.getInt(trackSourceTidField, trackIdx);
  }

  void Meta::setVod(bool vod){
    stream.setInt(streamVodField, vod ? 1 : 0);
  }
  bool Meta::getVod() const{return stream.getInt(streamVodField);}

  void Meta::setLive(bool live){
    stream.setInt(streamLiveField, live ? 1 : 0);
  }
  bool Meta::getLive() const{return stream.getInt(streamLiveField);}

  bool Meta::hasBFrames(size_t idx) const{
    std::set<size_t> vTracks = getValidTracks();
    for (std::set<size_t>::iterator it = vTracks.begin(); it != vTracks.end(); it++){
      if (idx != INVALID_TRACK_ID && idx != *it){continue;}
      if (getType(*it) != "video"){continue;}
      DTSC::Parts p(parts(*it));
      size_t ctr = 0;
      int64_t prevOffset = 0;
      bool firstOffset = true;
      for (size_t i = p.getFirstValid(); i < p.getEndValid(); ++i){
        if (firstOffset){
          firstOffset = false;
          prevOffset = p.getOffset(i);
        }
        if (p.getOffset(i) != prevOffset){return true;}
        if (++ctr >= 100){break;}
      }
    }
    return false;
  }

  void Meta::setBufferWindow(uint64_t bufferWindow){
    stream.setInt(streamBufferWindowField, bufferWindow);
  }
  uint64_t Meta::getBufferWindow() const{return stream.getInt(streamBufferWindowField);}

  void Meta::setBootMsOffset(int64_t bootMsOffset){
    DONTEVEN_MSG("Setting streamBootMsOffsetField to %" PRId64, bootMsOffset);
    stream.setInt(streamBootMsOffsetField, bootMsOffset);
  }
  int64_t Meta::getBootMsOffset() const{return stream.getInt(streamBootMsOffsetField);}

  void Meta::setUTCOffset(int64_t UTCOffset){
    stream.setInt(streamUTCOffsetField, UTCOffset);
  }
  int64_t Meta::getUTCOffset() const{return stream.getInt(streamUTCOffsetField);}

  /*LTS-START*/
  void Meta::setMinimumFragmentDuration(uint64_t fragmentDuration){
    stream.setInt(streamMinimumFragmentDurationField, fragmentDuration);
  }
  uint64_t Meta::getMinimumFragmentDuration() const{
    uint64_t res = stream.getInt(streamMinimumFragmentDurationField);
    if (res > 0){return res;}
    return DEFAULT_FRAGMENT_DURATION;
  }
  /*LTS-END*/

  std::set<size_t> Meta::getValidTracks(bool skipEmpty) const{
    std::set<size_t> res;
    if (!(*this) && !isMemBuf){
      INFO_MSG("Shared metadata not ready yet - no tracks valid");
      return res;
    }
    uint64_t firstValid = trackList.getDeleted();
    uint64_t beyondLast = trackList.getEndPos();
    for (size_t i = firstValid; i < beyondLast; i++){
      if (trackList.getInt(trackValidField, i) & trackValidMask){res.insert(i);}
      if (trackList.getInt(trackSourceTidField, i) != INVALID_TRACK_ID &&
          std::string(trackList.getPointer(trackEncryptionField, i)) != ""){
        res.erase(trackList.getInt(trackSourceTidField, i));
      }
      if (!tracks.count(i) || !tracks.at(i).track.isReady()){res.erase(i);}
      if (skipEmpty){
        if (res.count(i) && !tracks.at(i).parts.getPresent()){res.erase(i);}
      }
    }
    return res;
  }

  std::set<size_t> Meta::getMySourceTracks(size_t pid) const{
    std::set<size_t> res;
    if (!streamPage.mapped){return res;}
    uint64_t firstValid = trackList.getDeleted();
    uint64_t beyondLast = firstValid + trackList.getPresent();
    for (size_t i = firstValid; i < beyondLast; i++){
      if (trackList.getInt(trackValidField, i) && trackList.getInt(trackPidField, i) == pid){
        res.insert(i);
      }
    }
    return res;
  }

  /// Sets the track valid field to 1, also calling markUpdated()
  void Meta::validateTrack(size_t trackIdx, uint8_t validType){
    markUpdated(trackIdx);
    trackList.setInt(trackValidField, validType, trackIdx);
  }

  void Meta::removeEmptyTracks(){
    reloadReplacedPagesIfNeeded();
    std::set<size_t> validTracks = getValidTracks();
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
      if (!tracks.at(*it).parts.getPresent()){removeTrack(*it);}
    }
  }

  /// Removes the track from the memory structure and caches.
  void Meta::removeTrack(size_t trackIdx){
    if (!getValidTracks().count(trackIdx)){return;}
    Track &t = tracks[trackIdx];
    for (uint64_t i = t.pages.getDeleted(); i < t.pages.getEndPos(); i++){
      if (t.pages.getInt("avail", i) == 0){continue;}
      char thisPageName[NAME_BUFFER_SIZE];
      snprintf(thisPageName, NAME_BUFFER_SIZE, SHM_TRACK_DATA, streamName.c_str(), trackIdx,
               (uint32_t)t.pages.getInt("firstkey", i));
      IPC::sharedPage p(thisPageName, 20971520);
      p.master = true;
    }
    tM[trackIdx].master = true;
    tM.erase(trackIdx);
    tracks.erase(trackIdx);

    trackList.setInt(trackValidField, 0, trackIdx);
  }

  /// Removes the first key from the memory structure and caches.
  bool Meta::removeFirstKey(size_t trackIdx){

    IPC::semaphore resizeLock;

    if (!isMemBuf){
      const char * pageName = trackList.getPointer(trackPageField, trackIdx);
      resizeLock.open(pageName, O_CREAT | O_RDWR, ACCESSPERMS, 1);
      if (!resizeLock.tryWait()){
        MEDIUM_MSG("Metadata is busy, delaying deletion of key a bit");
        resizeLock.close();
        return false;
      }
      if (reloadReplacedPagesIfNeeded()){
        MEDIUM_MSG("Metadata just got replaced, delaying deletion of key a bit");
        return false;
      }
    }
    Track &t = tracks[trackIdx];
    DONTEVEN_MSG("Deleting parts: %" PRIu64 "->%" PRIu64 " del'd, %zu pres", t.parts.getDeleted(), t.parts.getDeleted()+t.keys.getInt(t.keyPartsField, t.keys.getDeleted()), t.parts.getPresent());
    t.parts.deleteRecords(t.keys.getInt(t.keyPartsField, t.keys.getDeleted()));
    DONTEVEN_MSG("Deleting key: %" PRIu64 "->%" PRIu64 " del'd, %zu pres", t.keys.getDeleted(), t.keys.getDeleted()+1, t.keys.getPresent());
    t.keys.deleteRecords(1);
    if (t.fragments.getInt(t.fragmentFirstKeyField, t.fragments.getDeleted()) < t.keys.getDeleted()){
      t.fragments.deleteRecords(1);
      setMissedFragments(trackIdx, getMissedFragments(trackIdx) + 1);
    }
    if (t.pages.getPresent() > 1 && t.pages.getInt("firstkey", t.pages.getDeleted() + 1) < t.keys.getDeleted()){
      // Initialize the correct page, make it master so it gets cleaned up when leaving scope.
      char thisPageName[NAME_BUFFER_SIZE];
      snprintf(thisPageName, NAME_BUFFER_SIZE, SHM_TRACK_DATA, streamName.c_str(), trackIdx,
               (uint32_t)t.pages.getInt("firstkey", t.pages.getDeleted()));
      IPC::sharedPage p(thisPageName, 20971520);
      p.master = true;

      // Then delete the page entry
      t.pages.deleteRecords(1);
    }
    setFirstms(trackIdx, t.keys.getInt(t.keyTimeField, t.keys.getDeleted()));
    if (resizeLock){resizeLock.unlink();}
    return true;
  }

  ///\brief Updates a meta object given a DTSC::Packet with byte position override.
  void Meta::updatePosOverride(DTSC::Packet &pack, uint64_t bpos){
    char *data;
    size_t dataLen;
    pack.getString("data", data, dataLen);
    update(pack.getTime(), pack.getInt("offset"), pack.getTrackId(), dataLen, bpos,
           pack.getFlag("keyframe"), pack.getDataLen());
  }

  ///\brief Updates a meta object given a DTSC::Packet
  void Meta::update(const DTSC::Packet &pack){
    char *data;
    size_t dataLen;
    pack.getString("data", data, dataLen);
    update(pack.getTime(), pack.getInt("offset"), pack.getTrackId(), dataLen, pack.getInt("bpos"),
           pack.getFlag("keyframe"), pack.getDataLen());
  }

  /// Helper class that calculates inter-packet jitter
  class jitterTimer{
  public:
    uint64_t trueTime[8]; // Array of bootMS-based measurement points
    uint64_t packTime[8]; // Array of corresponding packet times
    uint64_t curJitter;   // Maximum jitter measurement in past 10 seconds
    unsigned int x;       // Current indice within above two arrays
    uint64_t maxJitter;   // Highest jitter ever observed by this jitterTimer
    uint64_t lastTime;    // Last packet used for a measurement point
    jitterTimer(){
      for (int i = 0; i < 8; ++i){
        trueTime[i] = 0;
        packTime[i] = 0;
      }
      maxJitter = 200;
      lastTime = 0;
      x = 0;
    }
    uint64_t addPack(uint64_t t){
      if (veryUglyJitterOverride){return veryUglyJitterOverride;}
      uint64_t curMs = Util::bootMS();
      if (!x){
        // First call, set the whole array to this packet
        for (int i = 0; i < 8; ++i){
          trueTime[i] = curMs;
          packTime[i] = t;
        }
        ++x;
        trueTime[x % 8] = curMs;
        packTime[x % 8] = t;
        lastTime = t;
        curJitter = 0;
      }
      if (t > lastTime + 2500){
        if ((x % 4) == 0){
          if (maxJitter > 50 && curJitter < maxJitter - 50){
            MEDIUM_MSG("Jitter lowered from %" PRIu64 " to %" PRIu64 " ms", maxJitter, curJitter);
            maxJitter = curJitter;
          }
          curJitter = maxJitter*0.90;
        }
        ++x;
        trueTime[x % 8] = curMs;
        packTime[x % 8] = t;
        lastTime = t;
      }
      uint64_t realTime = (curMs - trueTime[(x + 1) % 8]);
      uint64_t arriTime = (t - packTime[(x + 1) % 8]);
      int64_t jitter = (realTime - arriTime);
      if (jitter < 0){
        // Negative jitter = packets arriving too soon.
        // This is... ehh... not a bad thing? I guess..?
        // if (jitter < -1000){
        //  INFO_MSG("Jitter = %" PRId64 " ms (max: %" PRIu64 ")", jitter, maxJitter);
        //}
      }else{
        // Positive jitter = packets arriving too late.
        // We need to delay playback at least by this amount to account for it.
        if ((uint64_t)jitter > maxJitter){
          if (jitter - maxJitter > 420){
            INFO_MSG("Jitter increased from %" PRIu64 " to %" PRId64 " ms", maxJitter, jitter);
          }else{
            HIGH_MSG("Jitter increased from %" PRIu64 " to %" PRId64 " ms", maxJitter, jitter);
          }
          maxJitter = (uint64_t)jitter;
        }
        if (curJitter < (uint64_t)jitter){curJitter = (uint64_t)jitter;}
      }
      return maxJitter;
    }
  };

  /// Updates the metadata given the packet's properties.
  void Meta::update(uint64_t packTime, int64_t packOffset, uint32_t packTrack, uint64_t packDataSize,
                    uint64_t packBytePos, bool isKeyframe, uint64_t packSendSize){
    ///\todo warning Re-Implement Ivec
    if (getLive()){
      static std::map<size_t, jitterTimer> theJitters;
      setMinKeepAway(packTrack, theJitters[packTrack].addPack(packTime));
    }

    DONTEVEN_MSG("Updating meta with: t=%" PRIu64 ", o=%" PRId64 ", s=%" PRIu64 ", t=%" PRIu32
                 ", p=%" PRIu64,
                 packTime, packOffset, packDataSize, packTrack, packBytePos);
    if (!packSendSize){
      // time and trackID are part of the 20-byte header.
      // the container object adds 4 bytes (plus 2+namelen for each content, see below)
      // offset, if non-zero, adds 9 bytes (integer type) and 8 bytes (2+namelen)
      // bpos, if >= 0, adds 9 bytes (integer type) and 6 bytes (2+namelen)
      // keyframe, if true, adds 9 bytes (integer type) and 10 bytes (2+namelen)
      // data adds packDataSize+5 bytes (string type) and 6 bytes (2+namelen)
      packSendSize = 24 + (packOffset ? 17 : 0) + (packBytePos > 0 ? 15 : 0) +
                     (isKeyframe ? 19 : 0) + packDataSize + 11;
    }

    if ((packBytePos > 0) && !getVod()){setVod(true);}

    size_t tNumber = packTrack;
    std::map<size_t, DTSC::Track>::iterator it = tracks.find(tNumber);
    if (it == tracks.end()){
      ERROR_MSG("Could not buffer packet for track %zu: track not found", tNumber);
      return;
    }

    Track &t = it->second;
    if (packTime < getLastms(tNumber)){
      static bool warned = false;
      if (!warned){
        ERROR_MSG("Received packets for track %zu in wrong order (%" PRIu64 " < %" PRIu64
                  ") - ignoring! Further messages on HIGH level.",
                  tNumber, packTime, getLastms(tNumber));
        warned = true;
      }else{
        HIGH_MSG("Received packets for track %zu in wrong order (%" PRIu64 " < %" PRIu64
                 ") - ignoring!",
                 tNumber, packTime, getLastms(tNumber));
      }
      return;
    }

    uint64_t newPartNum = t.parts.getEndPos();
    if ((newPartNum - t.parts.getDeleted()) >= t.parts.getRCount()){
      resizeTrack(tNumber, t.fragments.getRCount(), t.keys.getRCount(), t.parts.getRCount() * 2, t.pages.getRCount(), "not enough parts");
    }
    t.parts.setInt(t.partSizeField, packDataSize, newPartNum);
    t.parts.setInt(t.partOffsetField, packOffset, newPartNum);
    if (newPartNum){
      t.parts.setInt(t.partDurationField, packTime - getLastms(tNumber), newPartNum - 1);
      t.parts.setInt(t.partDurationField, packTime - getLastms(tNumber), newPartNum);
    }else{
      t.parts.setInt(t.partDurationField, 0, newPartNum);
      setFirstms(tNumber, packTime);
    }
    t.parts.addRecords(1);

    uint64_t newKeyNum = t.keys.getEndPos();
    if (isKeyframe || newKeyNum == 0 ||
        (getType(tNumber) != "video" && packTime >= AUDIO_KEY_INTERVAL &&
         packTime - t.keys.getInt(t.keyTimeField, newKeyNum - 1) >= AUDIO_KEY_INTERVAL)){
      if ((newKeyNum - t.keys.getDeleted()) >= t.keys.getRCount()){
        resizeTrack(tNumber, t.fragments.getRCount(), t.keys.getRCount() * 2, t.parts.getRCount(), t.pages.getRCount(), "not enough keys");
      }
      t.keys.setInt(t.keyBposField, packBytePos, newKeyNum);
      t.keys.setInt(t.keyTimeField, packTime, newKeyNum);
      t.keys.setInt(t.keyPartsField, 0, newKeyNum);
      t.keys.setInt(t.keyDurationField, 0, newKeyNum);
      t.keys.setInt(t.keySizeField, 0, newKeyNum);
      t.keys.setInt(t.keyNumberField, newKeyNum, newKeyNum);
      if (newKeyNum){
        t.keys.setInt(t.keyFirstPartField,
                      t.keys.getInt(t.keyFirstPartField, newKeyNum - 1) +
                          t.keys.getInt(t.keyPartsField, newKeyNum - 1),
                      newKeyNum);
        // Update duration of previous key too
        t.keys.setInt(t.keyDurationField, packTime - t.keys.getInt(t.keyTimeField, newKeyNum - 1),
                      newKeyNum - 1);
      }else{
        t.keys.setInt(t.keyFirstPartField, 0, newKeyNum);
      }
      t.keys.addRecords(1);
      t.track.setInt(t.trackFirstmsField, t.keys.getInt(t.keyTimeField, t.keys.getDeleted()));

      uint64_t newFragNum = t.fragments.getEndPos();
      if (newFragNum == 0 ||
          (packTime >= getMinimumFragmentDuration() &&
           (packTime - getMinimumFragmentDuration()) >=
               t.keys.getInt(t.keyTimeField, t.fragments.getInt(t.fragmentFirstKeyField, newFragNum - 1)))){
        if ((newFragNum - t.fragments.getDeleted()) >= t.fragments.getRCount()){
          resizeTrack(tNumber, t.fragments.getRCount() * 2, t.keys.getRCount(), t.parts.getRCount(), t.pages.getRCount(), "not enough frags");
        }
        if (newFragNum){
          t.fragments.setInt(t.fragmentDurationField,
                             packTime - t.keys.getInt(t.keyTimeField, t.fragments.getInt(t.fragmentFirstKeyField,
                                                                                         newFragNum - 1)),
                             newFragNum - 1);

          uint64_t totalBytes = 0;
          uint64_t totalDuration = 0;

          for (size_t fragIdx = t.fragments.getStartPos(); fragIdx < newFragNum; fragIdx++){
            totalBytes += t.fragments.getInt(t.fragmentSizeField, fragIdx);
            totalDuration += t.fragments.getInt(t.fragmentDurationField, fragIdx);
          }
          setBps(tNumber, (totalDuration ? (totalBytes * 1000) / totalDuration : 0));

          setMaxBps(tNumber, std::max(getMaxBps(tNumber),
                                      (t.fragments.getInt(t.fragmentSizeField, newFragNum - 1) * 1000) /
                                          t.fragments.getInt(t.fragmentDurationField, newFragNum - 1)));
        }
        t.fragments.setInt(t.fragmentFirstKeyField, newKeyNum, newFragNum);
        t.fragments.setInt(t.fragmentDurationField, 0, newFragNum);
        t.fragments.setInt(t.fragmentSizeField, 0, newFragNum);
        t.fragments.setInt(t.fragmentKeysField, 1, newFragNum);
        t.fragments.setInt(t.fragmentFirstKeyField, t.keys.getInt(t.keyNumberField, newKeyNum), newFragNum);
        t.fragments.addRecords(1);
      }else{
        t.fragments.setInt(t.fragmentKeysField,
                           t.fragments.getInt(t.fragmentKeysField, newFragNum - 1) + 1, newFragNum - 1);
      }
    }else{
      uint64_t lastKeyNum = t.keys.getEndPos() - 1;
      t.keys.setInt(t.keyDurationField,
                    t.keys.getInt(t.keyDurationField, lastKeyNum) +
                        t.parts.getInt(t.partDurationField, newPartNum - 1),
                    lastKeyNum);
    }

    uint64_t lastKeyNum = t.keys.getEndPos() - 1;
    t.keys.setInt(t.keyPartsField, t.keys.getInt(t.keyPartsField, lastKeyNum) + 1, lastKeyNum);
    t.keys.setInt(t.keySizeField, t.keys.getInt(t.keySizeField, lastKeyNum) + packSendSize, lastKeyNum);
    uint64_t lastFragNum = t.fragments.getEndPos() - 1;
    t.fragments.setInt(t.fragmentSizeField,
                       t.fragments.getInt(t.fragmentSizeField, lastFragNum) + packDataSize, lastFragNum);
    t.track.setInt(t.trackLastmsField, packTime);
    markUpdated(tNumber);
  }

  /// Prints the metadata and tracks in human-readable format
  std::string Meta::toPrettyString() const{
    std::stringstream r;
    r << "Metadata for stream " << streamName << std::endl;
    r << stream.toPrettyString();
    for (std::map<size_t, Track>::const_iterator it = tracks.begin(); it != tracks.end(); it++){
      r << "  Track " << it->first << ": " << it->second.track.toPrettyString() << std::endl;
    }
    return r.str();
  }

  /// Loops over the active tracks, returning the index of the track with the given ID for the given
  /// process.
  size_t Meta::trackIDToIndex(size_t trackID, size_t pid) const{
    for (size_t i = 0; i < trackList.getPresent(); i++){
      if (pid && trackList.getInt(trackPidField, i) != pid){continue;}
      if (trackList.getInt(trackIdField, i) == trackID){return i;}
    }
    return INVALID_TRACK_ID;
  }

  /// Returns a pretty-printed (optionally unique) name for the given track
  std::string Meta::getTrackIdentifier(size_t idx, bool unique) const{
    std::stringstream result;
    std::string type = getType(idx);
    if (type == ""){
      result << "metadata_" << idx;
      return result.str();
    }
    result << type << "_";
    result << getCodec(idx) << "_";
    if (type == "audio"){
      result << getChannels(idx) << "ch_";
      result << getRate(idx) << "hz";
    }else if (type == "video"){
      result << getWidth(idx) << "x" << getHeight(idx) << "_";
      result << (double)getFpks(idx) / 1000 << "fps";
    }
    if (getLang(idx) != "" && getLang(idx) != "und"){result << "_" << getLang(idx);}
    if (unique){result << "_" << idx;}
    return result.str();
  }

  const Util::RelAccX &Meta::parts(size_t idx) const{return tracks.at(idx).parts;}
  Util::RelAccX &Meta::keys(size_t idx){return tracks.at(idx).keys;}
  const Util::RelAccX &Meta::keys(size_t idx) const{return tracks.at(idx).keys;}
  const Util::RelAccX &Meta::fragments(size_t idx) const{return tracks.at(idx).fragments;}
  const Util::RelAccX &Meta::pages(size_t idx) const{return tracks.at(idx).pages;}
  Util::RelAccX &Meta::pages(size_t idx){return tracks.at(idx).pages;}

  /// Wipes internal structures, also marking as outdated and deleting memory structures if in
  /// master mode.
  void Meta::clear(){
    if (isMemBuf){
      isMemBuf = false;
      free(streamMemBuf);
      streamMemBuf = 0;
      for (std::map<size_t, char *>::iterator it = tMemBuf.begin(); it != tMemBuf.end(); it++){
        free(it->second);
      }
      tMemBuf.clear();
      sizeMemBuf.clear();
    }else if (isMaster){
      IPC::semaphore trackLock;
      if (streamName.size()){
        char pageName[NAME_BUFFER_SIZE];
        snprintf(pageName, NAME_BUFFER_SIZE, SEM_TRACKLIST, streamName.c_str());
        trackLock.open(pageName, O_CREAT|O_RDWR, ACCESSPERMS, 1);
        trackLock.tryWaitOneSecond();
      }
      std::set<size_t> toRemove;
      for (std::map<size_t, IPC::sharedPage>::iterator it = tM.begin(); it != tM.end(); it++){
        if (!it->second.mapped){continue;}
        toRemove.insert(it->first);
      }
      for (std::set<size_t>::iterator it = toRemove.begin(); it != toRemove.end(); it++){
        removeTrack(*it);
      }
      if (streamPage.mapped && stream.isReady()){stream.setExit();}
      streamPage.master = true;
      if (streamName.size()){
        //Wipe tracklist semaphore. This is not done anywhere else in the codebase.
        trackLock.unlink();
      }
    }
    stream = Util::RelAccX();
    trackList = Util::RelAccX();
    streamPage.close();
    tM.clear();
    tracks.clear();
    isMaster = true;
    streamName = "";
  }

  /// Makes a minimally-sized copy of the given Meta object.
  /// Only used internally by the remap() function.
  void Meta::minimalFrom(const DTSC::Meta &src){
    clear();
    sBufMem();
    streamInit();
    stream.flowFrom(src.stream);

    for (int i = 0; i < src.trackList.getPresent(); i++){
      Track &t = tracks[i];
      tMemBuf[i] = (char *)malloc(SHM_STREAM_TRACK_LEN);
      sizeMemBuf[i] = SHM_STREAM_TRACK_LEN;
      memset(tMemBuf[i], 0, SHM_STREAM_TRACK_LEN);
      t.track = Util::RelAccX(tMemBuf[i], false);
      initializeTrack(t);
      t.track.flowFrom(src.tracks.at(i).track);
      t.track.setReady();
    }
  }

  /// Re-maps the current Meta object by making a minimal copy in a temporary object, then flowing
  /// the current object from the temporary object. Not currently used by anything...?
  void Meta::remap(const std::string &_streamName){
    Meta M;
    M.minimalFrom(*this);

    reInit(_streamName.size() ? _streamName : streamName);

    stream.flowFrom(M.stream);
    for (size_t i = 0; i < M.trackList.getPresent(); i++){
      Track &t = tracks[i];

      char pageName[NAME_BUFFER_SIZE];
      snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_TM, streamName.c_str(), getpid(), i);

      tM[i].init(pageName, SHM_STREAM_TRACK_LEN, true);
      tM[i].master = false;

      t.track = Util::RelAccX(tM[i].mapped, false);
      initializeTrack(t);
      t.track.flowFrom(M.tracks[i].track);
      t.track.setReady();
    }
  }

  ///\brief Determines the "packed" size of a Meta object
  uint64_t Meta::getSendLen(bool skipDynamic, std::set<size_t> selectedTracks) const{
    uint64_t dataLen = 34; // + (merged ? 17 : 0) + (bufferWindow ? 24 : 0) + 21;
    if (getVod()){dataLen += 14;}
    if (getLive()){dataLen += 15 + 19;} // 19 for unixzero
    for (std::map<size_t, Track>::const_iterator it = tracks.begin(); it != tracks.end(); it++){
      if (!it->second.parts.getPresent()){continue;}
      if (!selectedTracks.size() || selectedTracks.count(it->first)){
        dataLen += (124 + getInit(it->first).size() + getCodec(it->first).size() +
                    getType(it->first).size() + getTrackIdentifier(it->first, true).size());
        if (!skipDynamic){
          dataLen += ((it->second.fragments.getPresent() * DTSH_FRAGMENT_SIZE) + 16);
          dataLen += ((it->second.keys.getPresent() * DTSH_KEY_SIZE) + 11);
          dataLen += ((it->second.keys.getPresent() * 4) + 15);
          dataLen += ((it->second.parts.getPresent() * DTSH_PART_SIZE) + 12);
          //          dataLen += ivecs.size() * 8 + 12; /*LTS*/
          if (it->second.track.getInt("missedFrags")){dataLen += 23;}
        }
        std::string lang = getLang(it->first);
        if (lang.size() && lang != "und"){dataLen += 11 + lang.size();}
        if (getType(it->first) == "audio"){
          dataLen += 49;
        }else if (getType(it->first) == "video"){
          dataLen += 48;
        }
      }
    }
    /*
    if (sourceURI.size()){
      dataLen += 13 + sourceURI.size();
    }
    */
    return dataLen + 8; // add 8 bytes header
  }

  ///\brief Converts a short to a char*
  inline char *c16(short input){
    static char result[2];
    Bit::htobs(result, input);
    return result;
  }

  ///\brief Converts a short to a char*
  inline char *c24(int input){
    static char result[3];
    Bit::htob24(result, input);
    return result;
  }

  ///\brief Converts an integer to a char*
  inline char *c32(int input){
    static char result[4];
    Bit::htobl(result, input);
    return result;
  }

  ///\brief Converts a long long to a char*
  inline char *c64(long long int input){
    static char result[8];
    Bit::htobll(result, input);
    return result;
  }

  /// Converts the current Meta object to JSON format
  void Meta::toJSON(JSON::Value &res, bool skipDynamic, bool tracksOnly) const{
    res.null();
    if (!skipDynamic){
      WARN_MSG("Skipping dynamic stuff even though skipDynamic is set to false");
    }
    uint64_t jitter = 0;
    bool bframes = false;
    std::set<size_t> validTracks = getValidTracks();
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
      JSON::Value &trackJSON = res["tracks"][getTrackIdentifier(*it, true)];
      std::string type = getType(*it);

      trackJSON["codec"] = getCodec(*it);
      trackJSON["type"] = type;
      trackJSON["idx"] = (uint64_t)*it;
      trackJSON["trackid"] = (uint64_t)getID(*it);
      trackJSON["init"] = getInit(*it);
      trackJSON["firstms"] = getFirstms(*it);
      trackJSON["lastms"] = getLastms(*it);
      trackJSON["bps"] = getBps(*it);
      trackJSON["maxbps"] = getMaxBps(*it);
      if (!skipDynamic && getLive()){
        if (getMissedFragments(*it)){trackJSON["missed_frags"] = getMissedFragments(*it);}
      }
      uint64_t trkJitter = getMinKeepAway(*it);
      if (trkJitter){
        trackJSON["jitter"] = trkJitter;
        if (trkJitter > jitter){jitter = trkJitter;}
      }

      if (getLang(*it) != "" && getLang(*it) != "und"){trackJSON["lang"] = getLang(*it);}
      if (type == "audio"){
        trackJSON["rate"] = getRate(*it);
        trackJSON["size"] = getSize(*it);
        trackJSON["channels"] = getChannels(*it);
      }else if (type == "video"){
        trackJSON["width"] = getWidth(*it);
        trackJSON["height"] = getHeight(*it);
        trackJSON["fpks"] = getFpks(*it);
        if (hasBFrames(*it)){
          bframes = true;
          trackJSON["bframes"] = 1;
        }
      }
    }
    if (tracksOnly){
      JSON::Value v = res["tracks"];
      res = v;
      return;
    }
    if (jitter){res["jitter"] = jitter;}
    res["bframes"] = bframes?1:0;
    if (getMaxKeepAway()){res["maxkeepaway"] = getMaxKeepAway();}
    if (getLive()){
      res["live"] = 1u;
    }else{
      res["vod"] = 1u;
    }
    res["version"] = DTSH_VERSION;
    if (getBufferWindow()){res["buffer_window"] = getBufferWindow();}
    if (getSource() != ""){res["source"] = getSource();}
  }

  /// Sends the current Meta object through a socket in DTSH format
  void Meta::send(Socket::Connection &conn, bool skipDynamic, std::set<size_t> selectedTracks, bool reID) const{
    std::string lVars;
    size_t lVarSize = 0;
    if (inputLocalVars.size()){
      lVars = inputLocalVars.toString();
      lVarSize = 2 + 14 + 5 + lVars.size();
    }

    conn.SendNow(DTSC::Magic_Header, 4);
    conn.SendNow(c32(getSendLen(skipDynamic, selectedTracks) - 8), 4);
    conn.SendNow("\340", 1);
    if (getVod()){conn.SendNow("\000\003vod\001\000\000\000\000\000\000\000\001", 14);}
    if (getLive()){conn.SendNow("\000\004live\001\000\000\000\000\000\000\000\001", 15);}
    conn.SendNow("\000\007version\001", 10);
    conn.SendNow(c64(DTSH_VERSION), 8);
    if (getLive()){
      conn.SendNow("\000\010unixzero\001", 11);
      conn.SendNow(c64(Util::unixMS() - Util::bootMS() + getBootMsOffset()), 8);
    }
    if (lVarSize){
      conn.SendNow("\000\016inputLocalVars\002", 17);
      conn.SendNow(c32(lVars.size()), 4);
      conn.SendNow(lVars.data(), lVars.size());
    }
    conn.SendNow("\000\006tracks\340", 9);
    for (std::set<size_t>::const_iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      std::string tmp = getTrackIdentifier(*it, true);
      conn.SendNow(c16(tmp.size()), 2);
      conn.SendNow(tmp.data(), tmp.size());
      conn.SendNow("\340", 1); // Begin track object

      if (!skipDynamic){
        const Util::RelAccX &fragments = tracks.at(*it).fragments;
        const Util::RelAccX &keys = tracks.at(*it).keys;
        const Util::RelAccX &parts = tracks.at(*it).parts;

        size_t fragBegin = fragments.getStartPos();
        size_t fragCount = fragments.getPresent();
        size_t keyBegin = keys.getStartPos();
        size_t keyCount = keys.getPresent();
        size_t partBegin = parts.getStartPos();
        size_t partCount = parts.getPresent();

        conn.SendNow("\000\011fragments\002", 12);
        conn.SendNow(c32(fragCount * DTSH_FRAGMENT_SIZE), 4);
        for (size_t i = 0; i < fragCount; i++){
          conn.SendNow(c32(fragments.getInt("duration", i + fragBegin)), 4);
          conn.SendNow(std::string(1, (char)fragments.getInt("keys", i + fragBegin)));

          conn.SendNow(c32(fragments.getInt("firstkey", i + fragBegin) + 1), 4);
          conn.SendNow(c32(fragments.getInt("size", i + fragBegin)), 4);
        }

        conn.SendNow("\000\004keys\002", 7);
        conn.SendNow(c32(keyCount * DTSH_KEY_SIZE), 4);
        for (size_t i = 0; i < keyCount; i++){
          conn.SendNow(c64(keys.getInt("bpos", i + fragBegin)), 8);
          conn.SendNow(c24(keys.getInt("duration", i + keyBegin)), 3);
          conn.SendNow(c32(keys.getInt("number", i + keyBegin)), 4);
          conn.SendNow(c16(keys.getInt("parts", i + keyBegin)), 2);
          conn.SendNow(c64(keys.getInt("time", i + keyBegin)), 8);
        }
        conn.SendNow("\000\010keysizes\002,", 11);
        conn.SendNow(c32(keyCount * 4), 4);
        for (size_t i = 0; i < keyCount; i++){
          conn.SendNow(c32(keys.getInt("size", i + keyBegin)), 4);
        }

        conn.SendNow("\000\005parts\002", 8);
        conn.SendNow(c32(partCount * DTSH_PART_SIZE), 4);
        for (size_t i = 0; i < partCount; i++){
          conn.SendNow(c24(parts.getInt("size", i + partBegin)), 3);
          conn.SendNow(c24(parts.getInt("duration", i + partBegin)), 3);
          conn.SendNow(c24(parts.getInt("offset", i + partBegin)), 3);
        }
      }

      const Util::RelAccX &track = tracks.at(*it).track;
      conn.SendNow("\000\007trackid\001", 10);
      if (reID){
        conn.SendNow(c64((*it) + 1), 8);
      }else{
        conn.SendNow(c64(track.getInt("id")), 8);
      }

      if (!skipDynamic && track.getInt("missedFrags")){
        conn.SendNow("\000\014missed_frags\001", 15);
        conn.SendNow(c64(track.getInt("missedFrags")), 8);
      }

      conn.SendNow("\000\007firstms\001", 10);
      conn.SendNow(c64(track.getInt("firstms")), 8);
      conn.SendNow("\000\006lastms\001", 9);
      conn.SendNow(c64(track.getInt("lastms")), 8);

      conn.SendNow("\000\003bps\001", 6);
      conn.SendNow(c64(track.getInt("bps")), 8);

      conn.SendNow("\000\006maxbps\001", 9);
      conn.SendNow(c64(track.getInt("maxbps")), 8);

      tmp = getInit(*it);
      conn.SendNow("\000\004init\002", 7);
      conn.SendNow(c32(tmp.size()), 4);
      conn.SendNow(tmp.data(), tmp.size());

      tmp = getCodec(*it);
      conn.SendNow("\000\005codec\002", 8);
      conn.SendNow(c32(tmp.size()), 4);
      conn.SendNow(tmp.data(), tmp.size());

      tmp = getLang(*it);
      if (tmp.size() && tmp != "und"){
        conn.SendNow("\000\004lang\002", 7);
        conn.SendNow(c32(tmp.size()), 4);
        conn.SendNow(tmp.data(), tmp.size());
      }

      tmp = getType(*it);
      conn.SendNow("\000\004type\002", 7);
      conn.SendNow(c32(tmp.size()), 4);
      conn.SendNow(tmp.data(), tmp.size());

      if (tmp == "audio"){
        conn.SendNow("\000\004rate\001", 7);
        conn.SendNow(c64(track.getInt("rate")), 8);
        conn.SendNow("\000\004size\001", 7);
        conn.SendNow(c64(track.getInt("size")), 8);
        conn.SendNow("\000\010channels\001", 11);
        conn.SendNow(c64(track.getInt("channels")), 8);
      }else if (tmp == "video"){
        conn.SendNow("\000\005width\001", 8);
        conn.SendNow(c64(track.getInt("width")), 8);
        conn.SendNow("\000\006height\001", 9);
        conn.SendNow(c64(track.getInt("height")), 8);
        conn.SendNow("\000\004fpks\001", 7);
        conn.SendNow(c64(track.getInt("fpks")), 8);
      }
      conn.SendNow("\000\000\356", 3); // End this track Object
    }
    conn.SendNow("\000\000\356", 3); // End tracks object
    conn.SendNow("\000\000\356", 3); // End global object
  }

  /// Returns true if the given track index is marked as valid and present in the tracks structure.
  bool Meta::trackLoaded(size_t idx) const{
    if (!trackValid(idx)){return false;}
    if (!tracks.count(idx)){
      INFO_MSG("Track %zu is not yet loaded", idx);
      return false;
    }
    return true;
  }

  /// Returns true if the given track index is marked as valid. For this the track does not have to
  /// be loaded as well
  uint8_t Meta::trackValid(size_t idx) const{
    if (idx > trackList.getPresent()){return 0;}
    return trackList.getInt(trackValidField, idx);
  }

  /// Returns the current highest track index (zero-based).
  size_t Meta::trackCount() const{return trackList.getPresent();}

  /// Returns the index the first video track, or the first track.
  /// Will print a WARN-level message if there are no tracks.
  size_t Meta::mainTrack() const{
    if (!trackList.getPresent()){return INVALID_TRACK_ID;}
    std::set<size_t> validTracks = getValidTracks();
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
      if (getType(*it) == "video"){return *it;}
    }
    return *validTracks.begin();
  }

  /// Returns the duration of the longest fragment in the given track.
  uint32_t Meta::biggestFragment(uint32_t idx) const{
    if (!trackList.getPresent()){return 0;}
    uint32_t trackIdx = (idx == INVALID_TRACK_ID ? mainTrack() : idx);
    if (!tM.count(trackIdx)){return 0;}
    DTSC::Fragments fragments(tracks.at(trackIdx).fragments);
    uint64_t firstFragment = fragments.getFirstValid();
    uint64_t endFragment = fragments.getEndValid();
    uint32_t ret = 0;
    for (uint64_t i = firstFragment; i < endFragment; i++){
      uint32_t fragDur = fragments.getDuration(i);
      if (fragDur > ret){ret = fragDur;}
    }
    return ret;
  }

  bool Meta::tracksAlign(size_t idx1, size_t idx2) const{
    if (!tM.count(idx1) || !tM.count(idx2)){return false;}
    DTSC::Fragments frag1(tracks.at(idx1).fragments);
    DTSC::Fragments frag2(tracks.at(idx2).fragments);
    if (frag1.getFirstValid() >= frag2.getFirstValid()){
      size_t firstValid = frag1.getFirstValid();
      size_t firstTime = getTimeForFragmentIndex(idx1, firstValid);
      size_t secondIndex = getFragmentIndexForTime(idx2, firstTime);
      size_t count = std::min(frag1.getValidCount(), frag2.getEndValid() - secondIndex);
      if (count <= 2){
        INFO_MSG("Determining track alignment between track %zu and %zu  based on %zu fragments, "
                 "might be inaccurate",
                 idx1, idx2, count);
      }
      for (size_t i = 0; i < count; i++){
        if (getTimeForFragmentIndex(idx1, firstValid + i) != getTimeForFragmentIndex(idx2, secondIndex + i)){
          return false;
        }
      }
    }else{
      size_t firstValid = frag2.getFirstValid();
      size_t firstTime = getTimeForFragmentIndex(idx2, firstValid);
      size_t secondIndex = getFragmentIndexForTime(idx1, firstTime);
      size_t count = std::min(frag2.getValidCount(), frag1.getEndValid() - secondIndex);
      if (count <= 2){
        INFO_MSG("Determining track alignment between track %zu and %zu  based on %zu fragments, "
                 "might be inaccurate",
                 idx1, idx2, count);
      }
      for (size_t i = 0; i < count; i++){
        if (getTimeForFragmentIndex(idx2, firstValid + i) != getTimeForFragmentIndex(idx1, secondIndex + i)){
          return false;
        }
      }
    }
    return true;
  }

  /// Gets indice of the fragment containing timestamp, or last fragment if nowhere.
  uint32_t Meta::getFragmentIndexForTime(uint32_t idx, uint64_t timestamp) const{
    DTSC::Fragments fragments(tracks.at(idx).fragments);
    DTSC::Keys keys(tracks.at(idx).keys);
    uint32_t firstFragment = fragments.getFirstValid();
    uint32_t endFragment = fragments.getEndValid();
    for (size_t i = firstFragment; i < endFragment; i++){
      uint32_t keyNumber = fragments.getFirstKey(i);
      uint32_t duration = fragments.getDuration(i);
      if (timestamp < keys.getTime(keyNumber) + duration){return i;}
    }
    if (endFragment > firstFragment){
      if (timestamp < getLastms(idx)){return endFragment - 1;}
    }
    return endFragment;
  }

  /// Returns the timestamp for the given key index in the given track index
  uint64_t Meta::getTimeForKeyIndex(uint32_t idx, uint32_t keyIdx) const{
    DTSC::Keys keys(tracks.at(idx).keys);
    return keys.getTime(keyIdx);
  }

  /// Returns indice of the key containing timestamp, or last key if nowhere.
  uint32_t Meta::getKeyIndexForTime(uint32_t idx, uint64_t timestamp) const{
    DTSC::Keys keys(tracks.at(idx).keys);
    uint32_t firstKey = keys.getFirstValid();
    uint32_t endKey = keys.getEndValid();

    for (size_t i = firstKey; i < endKey; i++){
      if (keys.getTime(i) + keys.getDuration(i) > timestamp){return i;}
    }
    return endKey;
  }

  /// Returns the tiestamp for the given fragment index in the given track index.
  uint64_t Meta::getTimeForFragmentIndex(uint32_t idx, uint32_t fragmentIdx) const{
    DTSC::Fragments fragments(tracks.at(idx).fragments);
    DTSC::Keys keys(tracks.at(idx).keys);
    return keys.getTime(fragments.getFirstKey(fragmentIdx));
  }

  /// Returns the part index for the given timestamp.
  /// Assumes the Packet is for the given track, and assumes the metadata and track data are not out
  /// of sync. Works by looking up the key for the Packet's timestamp, then walking through the
  /// parts until the time matches or exceeds the time of the Packet. Returns zero if the track
  /// index is invalid or if the timestamp cannot be found.
  uint32_t Meta::getPartIndex(uint64_t timestamp, size_t idx) const{
    if (idx == INVALID_TRACK_ID){return 0;}

    uint32_t res = 0;
    uint32_t keyIdx = getKeyIndexForTime(idx, timestamp);
    DTSC::Keys Keys(keys(idx));
    DTSC::Parts Parts(parts(idx));
    uint64_t currentTime = Keys.getTime(keyIdx);
    res = Keys.getFirstPart(keyIdx);
    size_t endPart = Parts.getEndValid();
    for (size_t i = res; i < endPart; i++){
      if (currentTime >= timestamp){return res;}
      currentTime += Parts.getDuration(i);
      res++;
    }
    return res;
  }

  /// Returns the part timestamp for the given index.
  /// Assumes the Packet is for the given track, and assumes the metadata and track data are not out
  /// of sync. Works by looking up the packet's key's timestamp, then walking through the
  /// parts adding up durations until we reach the part we want. Returns zero if the track
  /// index is invalid or if the timestamp cannot be found.
  uint64_t Meta::getPartTime(uint32_t partIndex, size_t idx) const{
    if (idx == INVALID_TRACK_ID){return 0;}
    DTSC::Keys Keys(keys(idx));
    DTSC::Parts Parts(parts(idx));
    size_t kId = 0;
    for (kId = 0; kId < Keys.getEndValid(); ++kId){
      size_t keyPartId = Keys.getFirstPart(kId);
      if (keyPartId+Keys.getParts(kId) > partIndex){
        //It's inside this key. Step through.
        uint64_t res = Keys.getTime(kId);
        while (keyPartId < partIndex){
          res += Parts.getDuration(keyPartId);
          ++keyPartId;
        }
        return res;
      }
    }
    return 0;
  }

  /// Given the current page, check if the next page is available. Returns true if it is.
  bool Meta::nextPageAvailable(uint32_t idx, size_t currentPage) const{
    const Util::RelAccX &pages = tracks.at(idx).pages;
    for (size_t i = pages.getStartPos(); i + 1 < pages.getEndPos(); ++i){
      if (pages.getInt("firstkey", i) == currentPage){return pages.getInt("avail", i + 1);}
    }
    return false;
  }

  /// Given a timestamp, returns the page number that timestamp can be found on.
  /// If the timestamp is not available, returns the closest page number that is.
  size_t Meta::getPageNumberForTime(uint32_t idx, uint64_t time) const{
    const Util::RelAccX &pages = tracks.at(idx).pages;
    Util::RelAccXFieldData avail = pages.getFieldData("avail");
    Util::RelAccXFieldData firsttime = pages.getFieldData("firsttime");
    uint32_t res = pages.getStartPos();
    uint64_t endPos = pages.getEndPos();
    for (uint64_t i = res; i < endPos; ++i){
      if (pages.getInt(avail, i) == 0){continue;}
      if (pages.getInt(firsttime, i) > time){break;}
      res = i;
    }
    DONTEVEN_MSG("Page number for time %" PRIu64 " on track %" PRIu32 " can be found on page %" PRIu64, time, idx, pages.getInt("firstkey", res));
    return pages.getInt("firstkey", res);
  }

  /// Given a key, returns the page number it can be found on.
  /// If the key is not available, returns the closest page that is.
  size_t Meta::getPageNumberForKey(uint32_t idx, uint64_t keyNum) const{
    const Util::RelAccX &pages = tracks.at(idx).pages;
    size_t res = pages.getStartPos();
    for (size_t i = pages.getStartPos(); i < pages.getEndPos(); ++i){
      if (pages.getInt("avail", i) == 0){continue;}
      if (pages.getInt("firstkey", i) > keyNum){break;}
      res = i;
    }
    return pages.getInt("firstkey", res);
  }

  /// Returns the key number containing a given time.
  /// Or, closest key if given time is not available.
  /// Or, INVALID_KEY_NUM if no keys are available at all.
  /// If the time is in the gap before a key, returns that next key instead.
  size_t Meta::getKeyNumForTime(uint32_t idx, uint64_t time) const{
    const Track &trk = tracks.at(idx);
    const Util::RelAccX &keys = trk.keys;
    const Util::RelAccX &parts = trk.parts;
    if (!keys.getEndPos()){return INVALID_KEY_NUM;}
    size_t res = keys.getStartPos();
    for (size_t i = res; i < keys.getEndPos(); i++){
      if (keys.getInt(trk.keyTimeField, i) > time){
        //It's possible we overshot our timestamp, but the previous key does not contain it.
        //This happens when seeking to a timestamp past the last part of the previous key, but
        //before the first part of the next key.
        //In this case, we should _not_ return the previous key, but the current key.
        //That prevents getting stuck at the end of the page, waiting for a part to show up that never will.
        if (keys.getInt(trk.keyFirstPartField, i) > parts.getStartPos()){
          uint64_t dur = parts.getInt(trk.partDurationField, keys.getInt(trk.keyFirstPartField, i)-1);
          if (keys.getInt(trk.keyTimeField, i) - dur < time){res = i;}
        }
        continue;
      }
      res = i;
    }
    DONTEVEN_MSG("Key number for time %" PRIu64 " on track %" PRIu32 " is %zu", time, idx, res);
    return res;
  }

  /// By reference, returns a JSON object with health information on the stream
  void Meta::getHealthJSON(JSON::Value &retRef) const{
    // clear the reference of old data, first
    retRef.null();
    bool hasH264 = false;
    bool hasAAC = false;
    std::stringstream issues;
    std::set<size_t> validTracks = getValidTracks();
    uint64_t jitter = 0;
    uint64_t buffer = 0;
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
      size_t i = *it;
      JSON::Value &track = retRef[getTrackIdentifier(i)];
      uint64_t minKeep = getMinKeepAway(*it);
      track["jitter"] = minKeep;
      std::string codec = getCodec(i);
      std::string type = getType(i);
      track["kbits"] = getBps(i) * 8 / 1024;
      track["codec"] = codec;
      uint32_t shrtest_key = 0xFFFFFFFFul;
      uint32_t longest_key = 0;
      uint32_t shrtest_prt = 0xFFFFFFFFul;
      uint32_t longest_prt = 0;
      uint32_t shrtest_cnt = 0xFFFFFFFFul;
      uint32_t longest_cnt = 0;
      DTSC::Keys Mkeys(keys(i));
      uint32_t firstKey = Mkeys.getFirstValid();
      uint32_t endKey = Mkeys.getEndValid();
      for (uint32_t k = firstKey; k+1 < endKey; k++){
        uint64_t kDur = Mkeys.getDuration(k);
        uint64_t kParts = Mkeys.getParts(k);
        if (!kDur){continue;}
        if (kDur > longest_key){longest_key = kDur;}
        if (kDur < shrtest_key){shrtest_key = kDur;}
        if (kParts > longest_cnt){longest_cnt = kParts;}
        if (kParts < shrtest_cnt){shrtest_cnt = kParts;}
        if ((kDur / kParts) > longest_prt){longest_prt = (kDur / kParts);}
        if ((kDur / kParts) < shrtest_prt){shrtest_prt = (kDur / kParts);}
      }
      track["keys"]["ms_min"] = shrtest_key;
      track["keys"]["ms_max"] = longest_key;
      track["keys"]["frame_ms_min"] = shrtest_prt;
      track["keys"]["frame_ms_max"] = longest_prt;
      track["keys"]["frames_min"] = shrtest_cnt;
      track["keys"]["frames_max"] = longest_cnt;
      uint64_t trBuffer = getLastms(i) - getFirstms(i);
      track["buffer"] = trBuffer;
      size_t srcTrk = getSourceTrack(i);
      if (srcTrk != INVALID_TRACK_ID){
        if (trackValid(srcTrk)){
          track["source"] = getTrackIdentifier(srcTrk);
        }else{
          track["source"] = "Invalid track " + JSON::Value((uint64_t)srcTrk).asString();
        }
      }else{
        if (jitter < minKeep){jitter = minKeep;}
        if (longest_prt > 500){
          issues << "unstable connection (" << longest_prt << "ms " << codec << " frame)! ";
        }
        if (shrtest_cnt < 6){
          issues << "unstable connection (" << shrtest_cnt << " " << codec << " frame(s) in key)! ";
        }
        if (longest_key > shrtest_key*1.30){
          issues << "unstable key interval (" << (uint32_t)(((longest_key/shrtest_key)-1)*100) << "% " << codec << " variance)! ";
        }
      }
      if (buffer < trBuffer){buffer = trBuffer;}
      if (codec == "AAC"){hasAAC = true;}
      if (codec == "H264"){hasH264 = true;}
      if (type == "video"){
        track["width"] = getWidth(i);
        track["height"] = getHeight(i);
        track["fpks"] = getFpks(i);
        track["bframes"] = hasBFrames(i);
      }
      if (type == "audio"){
        track["rate"] = getRate(i);
        track["channels"] = getChannels(i);
      }
    }
    if (jitter > 500){
      issues << "High jitter (" << jitter << "ms)! ";
    }
    retRef["jitter"] = jitter;
    retRef["buffer"] = buffer;
    if (getMaxKeepAway()){
      retRef["maxkeepaway"] = getMaxKeepAway();
    }
    if ((hasAAC || hasH264) && validTracks.size() > 1){
      if (!hasAAC){issues << "HLS no audio!";}
      if (!hasH264){issues << "HLS no video!";}
    }
    if (issues.str().size()){retRef["issues"] = issues.str();}
    // return is by reference
  }

  /// Returns true if the tracks idx1 and idx2 are keyframe aligned
  bool Meta::keyTimingsMatch(size_t idx1, size_t idx2) const {
    const DTSC::Track &t1 = tracks.at(idx1);
    const DTSC::Track &t2 = tracks.at(idx2);
    uint64_t t1Firstms = t1.track.getInt(t1.trackFirstmsField);
    uint64_t t2Firstms = t2.track.getInt(t2.trackFirstmsField);
    uint64_t firstms = t1Firstms > t2Firstms ? t1Firstms : t2Firstms;

    uint64_t t1Lastms = t1.track.getInt(t1.trackFirstmsField);
    uint64_t t2Lastms = t2.track.getInt(t2.trackFirstmsField);
    uint64_t lastms = t1Lastms > t2Lastms ? t1Lastms : t2Lastms;

    if (firstms > lastms) {
      WARN_MSG("Cannot check for timing alignment for tracks %zu and %zu: No overlap", idx1, idx2);
      return false;
    }

    uint32_t keyIdx1 = getKeyIndexForTime(idx1,firstms);
    uint32_t keyIdx2 = getKeyIndexForTime(idx2,firstms);

    DTSC::Keys keys1(tracks.at(idx1).keys);
    DTSC::Keys keys2(tracks.at(idx2).keys);

    while(true) {
      if (lastms < keys1.getTime(keyIdx1) || lastms < keys2.getTime(keyIdx2)) {return true;}
      if (keys1.getTime(keyIdx1) != keys2.getTime(keyIdx2)) {return false;}
      keyIdx1++;
      keyIdx2++;
    }
    return true;
  }

  Parts::Parts(const Util::RelAccX &_parts) : parts(_parts){
    sizeField = parts.getFieldData("size");
    durationField = parts.getFieldData("duration");
    offsetField = parts.getFieldData("offset");
  }

  size_t Parts::getFirstValid() const{return parts.getDeleted();}
  size_t Parts::getEndValid() const{return parts.getEndPos();}
  size_t Parts::getValidCount() const{return getEndValid() - getFirstValid();}
  size_t Parts::getSize(size_t idx) const{return parts.getInt(sizeField, idx);}
  uint64_t Parts::getDuration(size_t idx) const{return parts.getInt(durationField, idx);}
  int64_t Parts::getOffset(size_t idx) const{return parts.getInt(offsetField, idx);}

  Keys::Keys(Util::RelAccX &_keys) : isConst(false), keys(_keys), cKeys(_keys){
    firstPartField = cKeys.getFieldData("firstpart");
    bposField = cKeys.getFieldData("bpos");
    durationField = cKeys.getFieldData("duration");
    numberField = cKeys.getFieldData("number");
    partsField = cKeys.getFieldData("parts");
    timeField = cKeys.getFieldData("time");
    sizeField = cKeys.getFieldData("size");
  }

  Keys::Keys(const Util::RelAccX &_keys) : isConst(true), keys(empty), cKeys(_keys){
    firstPartField = cKeys.getFieldData("firstpart");
    bposField = cKeys.getFieldData("bpos");
    durationField = cKeys.getFieldData("duration");
    numberField = cKeys.getFieldData("number");
    partsField = cKeys.getFieldData("parts");
    timeField = cKeys.getFieldData("time");
    sizeField = cKeys.getFieldData("size");
  }

  size_t Keys::getFirstValid() const{return cKeys.getDeleted();}
  size_t Keys::getEndValid() const{return cKeys.getEndPos();}
  size_t Keys::getValidCount() const{return getEndValid() - getFirstValid();}

  size_t Keys::getFirstPart(size_t idx) const{return cKeys.getInt(firstPartField, idx);}
  size_t Keys::getBpos(size_t idx) const{return cKeys.getInt(bposField, idx);}
  uint64_t Keys::getDuration(size_t idx) const{return cKeys.getInt(durationField, idx);}
  size_t Keys::getNumber(size_t idx) const{return cKeys.getInt(numberField, idx);}
  size_t Keys::getParts(size_t idx) const{return cKeys.getInt(partsField, idx);}
  uint64_t Keys::getTime(size_t idx) const{return cKeys.getInt(timeField, idx);}
  void Keys::setSize(size_t idx, size_t _size){
    if (isConst){return;}
    keys.setInt(sizeField, _size, idx);
  }
  size_t Keys::getSize(size_t idx) const{return cKeys.getInt(sizeField, idx);}

  Fragments::Fragments(const Util::RelAccX &_fragments) : fragments(_fragments){}
  size_t Fragments::getFirstValid() const{return fragments.getDeleted();}
  size_t Fragments::getEndValid() const{return fragments.getEndPos();}
  size_t Fragments::getValidCount() const{return getEndValid() - getFirstValid();}
  uint64_t Fragments::getDuration(size_t idx) const{return fragments.getInt("duration", idx);}
  size_t Fragments::getKeycount(size_t idx) const{return fragments.getInt("keys", idx);}
  size_t Fragments::getFirstKey(size_t idx) const{return fragments.getInt("firstkey", idx);}
  size_t Fragments::getSize(size_t idx) const{return fragments.getInt("size", idx);}
}// namespace DTSC
