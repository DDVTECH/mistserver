#include "dtsc.h"
#include "defines.h"
#include "bitfields.h"
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <fstream>

#define AUDIO_KEY_INTERVAL 5000 ///< This define controls the keyframe interval for non-video tracks, such as audio and metadata tracks.

namespace DTSC {
  /// Default constructor for packets - sets a null pointer and invalid packet.
  Packet::Packet() {
    data = NULL;
    bufferLen = 0;
    dataLen = 0;
    master = false;
    version = DTSC_INVALID;
  }

  /// Copy constructor for packets, copies an existing packet with same noCopy flag as original.
  Packet::Packet(const Packet & rhs) {
    master = false;
    bufferLen = 0;
    data = NULL;
    if (rhs.data && rhs.dataLen){
      reInit(rhs.data, rhs.dataLen, !rhs.master);
    }else{
      null();
    }
  }

  /// Data constructor for packets, either references or copies a packet from raw data.
  Packet::Packet(const char * data_, unsigned int len, bool noCopy) {
    master = false;
    bufferLen = 0;
    data = NULL;
    reInit(data_, len, noCopy);
  }

  /// This destructor clears frees the data pointer if the packet was not a reference.
  Packet::~Packet() {
    if (master && data) {
      free(data);
    }
  }

  /// Copier for packets, copies an existing packet with same noCopy flag as original.
  /// If going from copy to noCopy, this will free the data pointer first.
  void Packet::operator = (const Packet & rhs) {
    if (master && !rhs.master) {
      null();
    }
    if (rhs && rhs.data && rhs.dataLen) {
      reInit(rhs.data, rhs.dataLen, !rhs.master);
    } else {
      null();
    }
  }

  /// Returns true if the packet is deemed valid, false otherwise.
  /// Valid packets have a length of at least 8, known header type, and length equal to the length set in the header.
  Packet::operator bool() const {
    if (!data) {
      DEBUG_MSG(DLVL_DONTEVEN, "No data");
      return false;
    }
    if (dataLen < 8) {
      DEBUG_MSG(DLVL_VERYHIGH, "Datalen < 8");
      return false;
    }
    if (version == DTSC_INVALID) {
      DEBUG_MSG(DLVL_VERYHIGH, "No valid version");
      return false;
    }
    if (ntohl(((int *)data)[1]) + 8 > dataLen) {
      DEBUG_MSG(DLVL_VERYHIGH, "Length mismatch");
      return false;
    }
    return true;
  }

  /// Returns the recognized packet type.
  /// This type is set by reInit and all constructors, and then just referenced from there on.
  packType Packet::getVersion() const {
    return version;
  }

  /// Resets this packet back to the same state as if it had just been freshly constructed.
  /// If needed, this frees the data pointer.
  void Packet::null() {
    if (master && data) {
      free(data);
    }
    master = false;
    data = NULL;
    bufferLen = 0;
    dataLen = 0;
    version = DTSC_INVALID;
  }

  /// Internally used resize function for when operating in copy mode and the internal buffer is too small.
  /// It will only resize up, never down.
  ///\param len The length th scale the buffer up to if necessary
  void Packet::resize(unsigned int len) {
    if (master && len > bufferLen) {
      char * tmp = (char *)realloc(data, len);
      if (tmp) {
        data = tmp;
        bufferLen = len;
      } else {
        DEBUG_MSG(DLVL_FAIL, "Out of memory on parsing a packet");
      }
    }
  }

  void Packet::reInit(Socket::Connection & src) {
    int sleepCount = 0;
    null();
    int toReceive = 0;
    while (src.connected()){
      if (!toReceive && src.Received().available(8)){
        if (src.Received().copy(2) != "DT"){
          WARN_MSG("Invalid DTSC Packet header encountered (%s)", src.Received().copy(4).c_str());
          break;
        }
        toReceive = Bit::btohl(src.Received().copy(8).data() + 4);
      }
      if (toReceive && src.Received().available(toReceive + 8)){
        std::string dataBuf = src.Received().remove(toReceive + 8);
        reInit(dataBuf.data(), dataBuf.size());
        return;
      }
      if(!src.spool()){
        if (sleepCount++ > 150){
          WARN_MSG("Waiting for packet on connection timed out");
          return;
        }
        Util::wait(100);
      }
    }
  }

  ///\brief Initializes a packet with new data
  ///\param data_ The new data for the packet
  ///\param len The length of the data pointed to by data_
  ///\param noCopy Determines whether to make a copy or not
  void Packet::reInit(const char * data_, unsigned int len, bool noCopy) {
    if (!data_) {
      WARN_MSG("ReInit received a null pointer with len %d, nulling", len);
      null();
      return;
    }
    if (!data_[0] && !data_[1] && !data_[2] && !data_[3]){
      null();
      return;
    }
    if (data_[0] != 'D' || data_[1] != 'T') {
      unsigned int twlen = len;
      if (twlen > 20){twlen = 20;}
      DEBUG_MSG(DLVL_HIGH, "ReInit received a pointer that didn't start with 'DT' but with %s (%u) - data corruption?", JSON::Value(std::string(data_, twlen)).toString().c_str(), len);
      null();
      return;
    }
    if (len <= 0) {
      len = ntohl(((int *)data_)[1]) + 8;
    }
    //clear any existing controlled contents
    if (master && noCopy) {
      null();
    }
    //set control flag to !noCopy
    master = !noCopy;
    //either copy the data, or only the pointer, depending on flag
    if (noCopy) {
      data = (char *)data_;
    } else {
      resize(len);
      memcpy(data, data_, len);
    }
    //check header type and store packet length
    dataLen = len;
    version = DTSC_INVALID;
    if (len > 3) {
      if (!memcmp(data, Magic_Packet2, 4)) {
        version = DTSC_V2;
      } else {
        if (!memcmp(data, Magic_Packet, 4)) {
          version = DTSC_V1;
        } else {
          if (!memcmp(data, Magic_Header, 4)) {
            version = DTSC_HEAD;
          } else {
            if (!memcmp(data, Magic_Command, 4)) {
              version = DTCM;
            } else {
            DEBUG_MSG(DLVL_FAIL, "ReInit received a packet with invalid header");
            return;
          }
        }
      }
      }
    } else {
      DEBUG_MSG(DLVL_FAIL, "ReInit received a packet with size < 4");
      return;
    }
  }
  
  /// Re-initializes this Packet to contain a generic DTSC packet with the given data fields.
  /// When given a NULL pointer, the data is reserved and memset to 0
  void Packet::genericFill(long long packTime, long long packOffset, long long packTrack, const char * packData, long long packDataSize, uint64_t packBytePos, bool isKeyframe){
    null();
    master = true;
    //time and trackID are part of the 20-byte header.
    //the container object adds 4 bytes (plus 2+namelen for each content, see below)
    //offset, if non-zero, adds 9 bytes (integer type) and 8 bytes (2+namelen)
    //bpos, if >= 0, adds 9 bytes (integer type) and 6 bytes (2+namelen)
    //keyframe, if true, adds 9 bytes (integer type) and 10 bytes (2+namelen)
    //data adds packDataSize+5 bytes (string type) and 6 bytes (2+namelen)
    if (packDataSize < 1){
      FAIL_MSG("Attempted to fill a packet with %lli bytes!", packDataSize);
      return;
    }
    unsigned int sendLen = 24 + (packOffset?17:0) + (packBytePos?15:0) + (isKeyframe?19:0) + packDataSize+11;
    resize(sendLen);
    //set internal variables
    version = DTSC_V2;
    dataLen = sendLen;
    //write the first 20 bytes
    memcpy(data, "DTP2", 4);
    unsigned int tmpLong = htonl(sendLen - 8);
    memcpy(data+4, (char *)&tmpLong, 4);
    tmpLong = htonl(packTrack);
    memcpy(data+8, (char *)&tmpLong, 4);
    tmpLong = htonl((int)(packTime >> 32));
    memcpy(data+12, (char *)&tmpLong, 4);
    tmpLong = htonl((int)(packTime & 0xFFFFFFFF));
    memcpy(data+16, (char *)&tmpLong, 4);
    data[20] = 0xE0;//start container object
    unsigned int offset = 21;
    if (packOffset){
      memcpy(data+offset, "\000\006offset\001", 9);
      tmpLong = htonl((int)(packOffset >> 32));
      memcpy(data+offset+9, (char *)&tmpLong, 4);
      tmpLong = htonl((int)(packOffset & 0xFFFFFFFF));
      memcpy(data+offset+13, (char *)&tmpLong, 4);
      offset += 17;
    }
    if (packBytePos){
      memcpy(data+offset, "\000\004bpos\001", 7);
      tmpLong = htonl((int)(packBytePos >> 32));
      memcpy(data+offset+7, (char *)&tmpLong, 4);
      tmpLong = htonl((int)(packBytePos & 0xFFFFFFFF));
      memcpy(data+offset+11, (char *)&tmpLong, 4);
      offset += 15;
    }
    if (isKeyframe){
      memcpy(data+offset, "\000\010keyframe\001\000\000\000\000\000\000\000\001", 19);
      offset += 19;
    }
    memcpy(data+offset, "\000\004data\002", 7);
    tmpLong = htonl(packDataSize);
    memcpy(data+offset+7, (char *)&tmpLong, 4);
    if (packData){
      memcpy(data+offset+11, packData, packDataSize);
    }else{
      memset(data+offset+11, 0, packDataSize);
    }
    //finish container with 0x0000EE
    memcpy(data+offset+11+packDataSize, "\000\000\356", 3);
  }

  ///clear the keyframe byte.
  void Packet::clearKeyFrame(){
    uint32_t offset = 23;
    while (data[offset] != 'd' && data[offset] != 'k'){
      switch (data[offset]){
        case 'o': offset += 17; break;
        case 'b': offset += 15; break;
        default:
          FAIL_MSG("Errrrrrr");
      }
    }

    if(data[offset] == 'k'){
      data[offset] = 'K';
      data[offset+16] = 0;
    }
  }

  void Packet::appendData(const char * appendData, uint32_t appendLen){
    resize(dataLen + appendLen);
    memcpy(data + dataLen-3, appendData, appendLen);
    memcpy(data + dataLen-3 + appendLen, "\000\000\356", 3);  //end container
    dataLen += appendLen;
    Bit::htobl(data+4, Bit::btohl(data +4)+appendLen);
    uint32_t offset = getDataStringLenOffset();
    Bit::htobl(data+offset, Bit::btohl(data+offset)+appendLen);
  }

  void Packet::appendNal(const char * appendData, uint32_t appendLen, uint32_t totalLen){
    if(totalLen ==0){
      return;
    }

//    INFO_MSG("totallen: %d, appendLen: %d",totalLen,appendLen);
    resize(dataLen + appendLen +4);
    Bit::htobl(data+dataLen -3, totalLen);
    memcpy(data + dataLen-3+4, appendData, appendLen);
    memcpy(data + dataLen-3+4 + appendLen, "\000\000\356", 3);  //end container
    dataLen += appendLen +4;
    Bit::htobl(data+4, Bit::btohl(data +4)+appendLen+4);
    uint32_t offset = getDataStringLenOffset();
    Bit::htobl(data+offset, Bit::btohl(data+offset)+appendLen+4);
  }

  uint32_t Packet::getDataStringLen(){
    return Bit::btohl(data+getDataStringLenOffset());
  }

  ///Method can only be used when using internal functions to build the data.
  uint32_t Packet::getDataStringLenOffset(){
    uint32_t offset = 23;
    while (data[offset] != 'd'){
      switch (data[offset]){
        case 'o': offset += 17; break;
        case 'b': offset += 15; break;
        case 'k': offset += 19; break;
        default:
          FAIL_MSG("Errrrrrr");
          return -1;
      }
    }
    return offset +5;
  }


  /// Helper function for skipping over whole DTSC parts
  static char * skipDTSC(char * p, char * max) {
    if (p + 1 >= max || p[0] == 0x00) {
      return 0;//out of packet! 1 == error
    }
    if (p[0] == DTSC_INT) {
      //int, skip 9 bytes to next value
      return p + 9;
    }
    if (p[0] == DTSC_STR) {
      if (p + 4 >= max) {
        return 0;//out of packet!
      }
      return p + 5 + Bit::btohl(p+1);
    }
    if (p[0] == DTSC_OBJ || p[0] == DTSC_CON) {
      p++;
      //object, scan contents
      while (p < max && p[0] + p[1] != 0) { //while not encountering 0x0000 (we assume 0x0000EE)
        if (p + 2 >= max) {
          return 0;//out of packet!
        }
        p += 2 + Bit::btohs(p);//skip size
        //otherwise, search through the contents, if needed, and continue
        p = skipDTSC(p, max);
        if (!p) {
          return 0;
        }
      }
      return p + 3;
    }
    if (p[0] == DTSC_ARR) {
      p++;
      //array, scan contents
      while (p < max && p[0] + p[1] != 0) { //while not encountering 0x0000 (we assume 0x0000EE)
        //search through contents...
        p = skipDTSC(p, max);
        if (!p) {
          return 0;
        }
      }
      return p + 3; //skip end marker
    }
    return 0;//out of packet! 1 == error
  }

  ///\brief Retrieves a single parameter as a string
  ///\param identifier The name of the parameter
  ///\param result A location on which the string will be returned
  ///\param len An integer in which the length of the string will be returned
  void Packet::getString(const char * identifier, char *& result, unsigned int & len) const {
    getScan().getMember(identifier).getString(result, len);
  }

  ///\brief Retrieves a single parameter as a string
  ///\param identifier The name of the parameter
  ///\param result The string in which to store the result
  void Packet::getString(const char * identifier, std::string & result) const {
    result = getScan().getMember(identifier).asString();
  }

  ///\brief Retrieves a single parameter as an integer
  ///\param identifier The name of the parameter
  ///\param result The result is stored in this integer
  void Packet::getInt(const char * identifier, int & result) const {
    result = getScan().getMember(identifier).asInt();
  }

  ///\brief Retrieves a single parameter as an integer
  ///\param identifier The name of the parameter
  ///\result The requested parameter as an integer
  int Packet::getInt(const char * identifier) const {
    int result;
    getInt(identifier, result);
    return result;
  }

  ///\brief Retrieves a single parameter as a boolean
  ///\param identifier The name of the parameter
  ///\param result The result is stored in this boolean
  void Packet::getFlag(const char * identifier, bool & result) const {
    int result_;
    getInt(identifier, result_);
    result = (bool)result_;
  }

  ///\brief Retrieves a single parameter as a boolean
  ///\param identifier The name of the parameter
  ///\result The requested parameter as a boolean
  bool Packet::getFlag(const char * identifier) const {
    bool result;
    getFlag(identifier, result);
    return result;
  }

  ///\brief Checks whether a parameter exists
  ///\param identifier The name of the parameter
  ///\result Whether the parameter exists or not
  bool Packet::hasMember(const char * identifier) const {
    return getScan().getMember(identifier).getType() > 0;
  }

  ///\brief Returns the timestamp of the packet.
  ///\return The timestamp of this packet.
  long long unsigned int Packet::getTime() const {
    if (version != DTSC_V2) {
      if (!data) {
        return 0;
      }
      return getInt("time");
    }
    return Bit::btohll(data + 12);
  }

  ///\brief Returns the track id of the packet.
  ///\return The track id of this packet.
  long int Packet::getTrackId() const {
    if (version != DTSC_V2) {
      return getInt("trackid");
    }
    return Bit::btohl(data+8);
  }

  ///\brief Returns a pointer to the payload of this packet.
  ///\return A pointer to the payload of this packet.
  char * Packet::getData() const {
    return data;
  }

  ///\brief Returns the size of this packet.
  ///\return The size of this packet.
  int Packet::getDataLen() const {
    return dataLen;
  }

  ///\brief Returns the size of the payload of this packet.
  ///\return The size of the payload of this packet.
  int Packet::getPayloadLen() const {
    if (version == DTSC_V2) {
      return dataLen - 20;
    } else {
      return dataLen - 8;
    }
  }

  /// Returns a DTSC::Scan instance to the contents of this packet.
  /// May return an invalid instance if this packet is invalid.
  Scan Packet::getScan() const {
    if (!*this || !getDataLen() || !getPayloadLen() || getDataLen() <= getPayloadLen()){
      return Scan();
    }
    return Scan(data + (getDataLen() - getPayloadLen()), getPayloadLen());
  }

  ///\brief Converts the packet into a JSON value
  ///\return A JSON::Value representation of this packet.
  JSON::Value Packet::toJSON() const {
    JSON::Value result;
    unsigned int i = 8;
    if (getVersion() == DTSC_V1) {
      JSON::fromDTMI((const unsigned char *)data, dataLen, i, result);
    }
    if (getVersion() == DTSC_V2) {
      JSON::fromDTMI2((const unsigned char *)data, dataLen, i, result);
    }
    return result;
  }

  std::string Packet::toSummary() const {
    std::stringstream out;
    char * res = 0;
    unsigned int len = 0;
    getString("data", res, len);
    out << getTrackId() << "@" << getTime() << ": " << len << " bytes";
    if (hasMember("keyframe")){
      out << " (keyframe)";
    }
    return out.str();
  }

  /// Create an invalid DTSC::Scan object by default.
  Scan::Scan() {
    p = 0;
    len = 0;
  }


  /// Create a DTSC::Scan object from memory pointer.
  Scan::Scan(char * pointer, size_t length) {
    p = pointer;
    len = length;
  }

  /// Returns whether the DTSC::Scan object contains valid data.
  Scan::operator bool() const {
    return (p && len);
  }

  /// Returns an object representing the named indice of this object.
  /// Returns an invalid object if this indice doesn't exist or this isn't an object type.
  Scan Scan::getMember(std::string indice) {
    return getMember(indice.data(), indice.size());
  }

  /// Returns an object representing the named indice of this object.
  /// Returns an invalid object if this indice doesn't exist or this isn't an object type.
  Scan Scan::getMember(const char * indice, const unsigned int ind_len) {
    if (getType() != DTSC_OBJ && getType() != DTSC_CON) {
      return Scan();
    }
    char * i = p + 1;
    //object, scan contents
    while (i[0] + i[1] != 0 && i < p + len) { //while not encountering 0x0000 (we assume 0x0000EE)
      if (i + 2 >= p + len) {
        return Scan();//out of packet!
      }
      unsigned int strlen = Bit::btohs(i);
      i += 2;
      if (ind_len == strlen && strncmp(indice, i, strlen) == 0) {
        return Scan(i + strlen, len - (i - p));
      } else {
        i = skipDTSC(i + strlen, p + len);
        if (!i) {
          return Scan();
        }
      }
    }
    return Scan();
  }

  /// Returns an object representing the named indice of this object.
  /// Returns an invalid object if this indice doesn't exist or this isn't an object type.
  bool Scan::hasMember(std::string indice){
    return getMember(indice.data(), indice.size());
  }

  /// Returns whether an object representing the named indice of this object exists.
  /// Returns false if this indice doesn't exist or this isn't an object type.
  bool Scan::hasMember(const char * indice, const unsigned int ind_len) {
    return getMember(indice, ind_len);
  }

  /// Returns an object representing the named indice of this object.
  /// Returns an invalid object if this indice doesn't exist or this isn't an object type.
  Scan Scan::getMember(const char * indice) {
    return getMember(indice, strlen(indice));
  }

  /// Returns the amount of indices if an array, the amount of members if an object, or zero otherwise.
  unsigned int Scan::getSize() {
    if (getType() == DTSC_ARR) {
      char * i = p + 1;
      unsigned int arr_indice = 0;
      //array, scan contents
      while (i[0] + i[1] != 0 && i < p + len) { //while not encountering 0x0000 (we assume 0x0000EE)
        //search through contents...
        arr_indice++;
        i = skipDTSC(i, p + len);
        if (!i) {
          return arr_indice;
        }
      }
      return arr_indice;
    }
    if (getType() == DTSC_OBJ || getType() == DTSC_CON) {
      char * i = p + 1;
      unsigned int arr_indice = 0;
      //object, scan contents
      while (i[0] + i[1] != 0 && i < p + len) { //while not encountering 0x0000 (we assume 0x0000EE)
        if (i + 2 >= p + len) {
          return Scan();//out of packet!
        }
        unsigned int strlen = Bit::btohs(i);
        i += 2;
        arr_indice++;
        i = skipDTSC(i + strlen, p + len);
        if (!i) {
          return arr_indice;
        }
      }
      return arr_indice;
    }
    return 0;
  }

  /// Returns an object representing the num-th indice of this array.
  /// If not an array but an object, it returns the num-th member, instead.
  /// Returns an invalid object if this indice doesn't exist or this isn't an array or object type.
  Scan Scan::getIndice(unsigned int num) {
    if (getType() == DTSC_ARR) {
      char * i = p + 1;
      unsigned int arr_indice = 0;
      //array, scan contents
      while (i[0] + i[1] != 0 && i < p + len) { //while not encountering 0x0000 (we assume 0x0000EE)
        //search through contents...
        if (arr_indice == num) {
          return Scan(i, len - (i - p));
        } else {
          arr_indice++;
          i = skipDTSC(i, p + len);
          if (!i) {
            return Scan();
          }
        }
      }
    }
    if (getType() == DTSC_OBJ || getType() == DTSC_CON) {
      char * i = p + 1;
      unsigned int arr_indice = 0;
      //object, scan contents
      while (i[0] + i[1] != 0 && i < p + len) { //while not encountering 0x0000 (we assume 0x0000EE)
        if (i + 2 >= p + len) {
          return Scan();//out of packet!
        }
        unsigned int strlen = Bit::btohs(i);
        i += 2;
        if (arr_indice == num) {
          return Scan(i + strlen, len - (i - p));
        } else {
          arr_indice++;
          i = skipDTSC(i + strlen, p + len);
          if (!i) {
            return Scan();
          }
        }
      }
    }
    return Scan();
  }

  /// Returns the name of the num-th member of this object.
  /// Returns an empty string on error or when not an object.
  std::string Scan::getIndiceName(unsigned int num) {
    if (getType() == DTSC_OBJ || getType() == DTSC_CON) {
      char * i = p + 1;
      unsigned int arr_indice = 0;
      //object, scan contents
      while (i[0] + i[1] != 0 && i < p + len) { //while not encountering 0x0000 (we assume 0x0000EE)
        if (i + 2 >= p + len) {
          return "";//out of packet!
        }
        unsigned int strlen = Bit::btohs(i);
        i += 2;
        if (arr_indice == num) {
          return std::string(i, strlen);
        } else {
          arr_indice++;
          i = skipDTSC(i + strlen, p + len);
          if (!i) {
            return "";
          }
        }
      }
    }
    return "";
  }
  
  /// Returns the first byte of this DTSC value, or 0 on error.
  char Scan::getType() {
    if (!p) {
      return 0;
    }
    return p[0];
  }

  /// Returns the boolean value of this DTSC value.
  /// Numbers are compared to 0.
  /// Strings are checked for non-zero length.
  /// Objects and arrays are checked for content.
  /// Returns false on error or in other cases.
  bool Scan::asBool() {
    switch (getType()) {
      case DTSC_STR:
        return (p[1] | p[2] | p[3] | p[4]);
      case DTSC_INT:
        return (asInt() != 0);
      case DTSC_OBJ:
      case DTSC_CON:
      case DTSC_ARR:
        return (p[1] | p[2]);
      default:
        return false;
    }
  }

  /// Returns the long long value of this DTSC number value.
  /// Will convert string values to numbers, taking octal and hexadecimal types into account.
  /// Illegal or invalid values return 0.
  long long Scan::asInt() {
    switch (getType()) {
      case DTSC_INT:
        return Bit::btohll(p+1);
      case DTSC_STR:
        char * str;
        unsigned int strlen;
        getString(str, strlen);
        if (!strlen) {
          return 0;
        }
        return strtoll(str, 0, 0);
      default:
        return 0;
    }
  }

  /// Returns the string value of this DTSC string value.
  /// Uses getString internally, if a string.
  /// Converts integer values to strings.
  /// Returns an empty string on error.
  std::string Scan::asString() {
    switch (getType()) {
      case DTSC_INT:{
        std::stringstream st;
        st << asInt();
        return st.str();
      }
      break;
      case DTSC_STR:{
        char * str;
        unsigned int strlen;
        getString(str, strlen);
        return std::string(str, strlen);
      }
      break;
    }
    return "";
  }

  /// Sets result to a pointer to the string, and strlen to the length of it.
  /// Sets both to zero if this isn't a DTSC string value.
  /// Attempts absolutely no conversion.
  void Scan::getString(char *& result, unsigned int & strlen) {
    switch (getType()) {
      case DTSC_STR:
        result = p + 5;
        strlen = Bit::btohl(p+1);
        return;
      default:
        result = 0;
        strlen = 0;
        return;
    }
  }

  /// Returns the DTSC scan object as a JSON value
  /// Returns an empty object on error.
  JSON::Value Scan::asJSON(){
    JSON::Value result;
    unsigned int i = 0;
    JSON::fromDTMI((const unsigned char*)p, len, i, result);
    return result;
  }

  /// \todo Move this function to some generic area. Duplicate from json.cpp
  static inline char hex2c(char c) {
    if (c < 10) {
      return '0' + c;
    }
    if (c < 16) {
      return 'A' + (c - 10);
    }
    return '0';
  }

  /// \todo Move this function to some generic area. Duplicate from json.cpp
  static std::string string_escape(const std::string val) {
    std::stringstream out;
    out << "\"";
    for (unsigned int i = 0; i < val.size(); ++i) {
      switch (val.data()[i]) {
        case '"':
          out << "\\\"";
          break;
        case '\\':
          out << "\\\\";
          break;
        case '\n':
          out << "\\n";
          break;
        case '\b':
          out << "\\b";
          break;
        case '\f':
          out << "\\f";
          break;
        case '\r':
          out << "\\r";
          break;
        case '\t':
          out << "\\t";
          break;
        default:
          if (val.data()[i] < 32 || val.data()[i] > 126) {
            out << "\\u00";
            out << hex2c((val.data()[i] >> 4) & 0xf);
            out << hex2c(val.data()[i] & 0xf);
          } else {
            out << val.data()[i];
          }
          break;
      }
    }
    out << "\"";
    return out.str();
  }

  std::string Scan::toPrettyString(unsigned int indent) {
    switch (getType()) {
      case DTSC_STR: {
          unsigned int strlen = Bit::btohl(p+1);
          if (strlen > 250) {
            std::stringstream ret;
            ret << "\"" << strlen << " bytes of data\"";
            return ret.str();
          }
          return string_escape(asString());
        }
      case DTSC_INT: {
          std::stringstream ret;
          ret << asInt();
          return ret.str();
        }
      case DTSC_OBJ:
      case DTSC_CON: {
          std::stringstream ret;
          ret << "{" << std::endl;
          indent += 2;
          char * i = p + 1;
          bool first = true;
          //object, scan contents
          while (i[0] + i[1] != 0 && i < p + len) { //while not encountering 0x0000 (we assume 0x0000EE)
            if (i + 2 >= p + len) {
              indent -= 2;
              ret << std::string((size_t)indent, ' ') << "} //walked out of object here";
              return ret.str();
            }
            if (!first){
              ret << "," << std::endl;
            }
            first = false;
            unsigned int strlen = Bit::btohs(i);
            i += 2;
            ret << std::string((size_t)indent, ' ') << "\"" << std::string(i, strlen) << "\": " << Scan(i + strlen, len - (i - p)).toPrettyString(indent);
            i = skipDTSC(i + strlen, p + len);
            if (!i) {
              indent -= 2;
              ret << std::string((size_t)indent, ' ') << "} //could not locate next object";
              return ret.str();
            }
          }
          indent -= 2;
          ret << std::endl << std::string((size_t)indent, ' ') << "}";
          return ret.str();
        }
      case DTSC_ARR: {
          std::stringstream ret;
          ret << "[" << std::endl;
          indent += 2;
          Scan tmpScan;
          unsigned int i = 0;
          bool first = true;
          do {
            tmpScan = getIndice(i++);
            if (tmpScan.getType()) {
              if (!first){
                ret << "," << std::endl;
              }
              first = false;
              ret << std::string((size_t)indent, ' ') << tmpScan.toPrettyString(indent);
            }
          } while (tmpScan.getType());
          indent -= 2;
          ret << std::endl << std::string((size_t)indent, ' ') << "]";
          return ret.str();
        }
      default:
        return "Error";
    }
  }



  ///\brief Returns the payloadsize of a part
  uint32_t Part::getSize() {
    return Bit::btoh24(data);
  }

  ///\brief Sets the payloadsize of a part
  void Part::setSize(uint32_t newSize) {
    Bit::htob24(data, newSize);
  }

  ///\brief Returns the duration of a part
  uint32_t Part::getDuration() {
    return Bit::btoh24(data + 3);
  }

  ///\brief Sets the duration of a part
  void Part::setDuration(uint32_t newDuration) {
    Bit::htob24(data + 3, newDuration);
  }

  ///\brief returns the offset of a part
  ///Assumes the offset is actually negative if bit 0x800000 is set.
  uint32_t Part::getOffset() {
    uint32_t ret = Bit::btoh24(data + 6);
    if (ret & 0x800000){
      return ret | 0xff000000ul;
    }else{
      return ret;
    }
  }

  ///\brief Sets the offset of a part
  void Part::setOffset(uint32_t newOffset) {
    Bit::htob24(data + 6, newOffset);
  }

  ///\brief Returns the data of a part
  char * Part::getData() {
    return data;
  }

  ///\brief Converts a part to a human readable string
  ///\param str The stringstream to append to
  ///\param indent the amount of indentation needed
  void Part::toPrettyString(std::ostream & str, int indent) {
    str << std::string(indent, ' ') << "Part: Size(" << getSize() << "), Dur(" << getDuration() << "), Offset(" << getOffset() << ")" << std::endl;
  }

  ///\brief Returns the byteposition of a keyframe
  unsigned long long Key::getBpos() {
    return Bit::btohll(data);
  }

  void Key::setBpos(unsigned long long newBpos) {
    Bit::htobll(data, newBpos);
  }

  unsigned long Key::getLength() {
    return Bit::btoh24(data+8);
  }

  void Key::setLength(unsigned long newLength) {
    Bit::htob24(data+8, newLength);
  }

  ///\brief Returns the number of a keyframe
  unsigned long Key::getNumber() {
    return Bit::btohl(data + 11);
  }

  ///\brief Sets the number of a keyframe
  void Key::setNumber(unsigned long newNumber) {
    Bit::htobl(data + 11, newNumber);
  }

  ///\brief Returns the number of parts of a keyframe
  unsigned short Key::getParts() {
    return Bit::btohs(data + 15);
  }

  ///\brief Sets the number of parts of a keyframe
  void Key::setParts(unsigned short newParts) {
    Bit::htobs(data + 15, newParts);
  }

  ///\brief Returns the timestamp of a keyframe
  unsigned long long Key::getTime() {
    return Bit::btohll(data + 17);
  }

  ///\brief Sets the timestamp of a keyframe
  void Key::setTime(unsigned long long newTime) {
    Bit::htobll(data + 17, newTime);
  }

  ///\brief Returns the data of this keyframe struct
  char * Key::getData() {
    return data;
  }

  ///\brief Converts a keyframe to a human readable string
  ///\param str The stringstream to append to
  ///\param indent the amount of indentation needed
  void Key::toPrettyString(std::ostream & str, int indent) {
    str << std::string(indent, ' ') << "Key " << getNumber() << ": Pos(" << getBpos() << "), Dur(" << getLength() << "), Parts(" << getParts() <<  "), Time(" << getTime() << ")" << std::endl;
  }

  ///\brief Returns the duration of this fragment
  unsigned long Fragment::getDuration() {
    return Bit::btohl(data);
  }

  ///\brief Sets the duration of this fragment
  void Fragment::setDuration(unsigned long newDuration) {
    Bit::htobl(data, newDuration);
  }

  ///\brief Returns the length of this fragment
  char Fragment::getLength() {
    return data[4];
  }

  ///\brief Sets the length of this fragment
  void Fragment::setLength(char newLength) {
    data[4] = newLength;
  }

  ///\brief Returns the number of the first keyframe in this fragment
  unsigned long Fragment::getNumber() {
    return Bit::btohl(data + 5);
  }

  ///\brief Sets the number of the first keyframe in this fragment
  void Fragment::setNumber(unsigned long newNumber) {
    Bit::htobl(data + 5, newNumber);
  }

  ///\brief Returns the size of a fragment
  unsigned long Fragment::getSize() {
    return Bit::btohl(data + 9);
  }

  ///\brief Sets the size of a fragement
  void Fragment::setSize(unsigned long newSize) {
    Bit::htobl(data + 9, newSize);
  }

  ///\brief Returns thte data of this fragment structure
  char * Fragment::getData() {
    return data;
  }

  ///\brief Converts a fragment to a human readable string
  ///\param str The stringstream to append to
  ///\param indent the amount of indentation needed
  void Fragment::toPrettyString(std::ostream & str, int indent) {
    str << std::string(indent, ' ') << "Fragment " << getNumber() << ": Dur(" << getDuration() << "), Len(" << (int)getLength() << "), Size(" << getSize() << ")" << std::endl;
  }

  ///\brief Constructs an empty track
  Track::Track() {
    trackID = 0;
    firstms = 0;
    lastms = 0;
    bps = 0;
    missedFrags = 0;
    rate = 0;
    size = 0;
    channels = 0;
    width = 0;
    height = 0;
    fpks = 0;
  }

  ///\brief Constructs a track from a JSON::Value
  Track::Track(JSON::Value & trackRef) {
    if (trackRef.isMember("fragments") && trackRef["fragments"].isString()) {
      Fragment * tmp = (Fragment *)trackRef["fragments"].asStringRef().data();
      fragments = std::deque<Fragment>(tmp, tmp + (trackRef["fragments"].asStringRef().size() / PACKED_FRAGMENT_SIZE));
    }
    if (trackRef.isMember("keys") && trackRef["keys"].isString()) {
      Key * tmp = (Key *)trackRef["keys"].asStringRef().data();
      keys = std::deque<Key>(tmp, tmp + (trackRef["keys"].asStringRef().size() / PACKED_KEY_SIZE));
    }
    if (trackRef.isMember("parts") && trackRef["parts"].isString()) {
      Part * tmp = (Part *)trackRef["parts"].asStringRef().data();
      parts = std::deque<Part>(tmp, tmp + (trackRef["parts"].asStringRef().size() / 9));
    }
    trackID = trackRef["trackid"].asInt();
    firstms = trackRef["firstms"].asInt();
    lastms = trackRef["lastms"].asInt();
    bps = trackRef["bps"].asInt();
    missedFrags = trackRef["missed_frags"].asInt();
    codec = trackRef["codec"].asStringRef();
    type = trackRef["type"].asStringRef();
    init = trackRef["init"].asStringRef();
    if (trackRef.isMember("lang") && trackRef["lang"].asStringRef().size()){
      lang = trackRef["lang"].asStringRef();
    }
    if (type == "audio") {
      rate = trackRef["rate"].asInt();
      size = trackRef["size"].asInt();
      channels = trackRef["channels"].asInt();
    }
    if (type == "video") {
      width = trackRef["width"].asInt();
      height = trackRef["height"].asInt();
      fpks = trackRef["fpks"].asInt();
    }
    if (trackRef.isMember("keysizes") && trackRef["keysizes"].isString()) {
      std::string tmp = trackRef["keysizes"].asStringRef();
      for (unsigned int i = 0; i < tmp.size(); i += 4){
        keySizes.push_back((((long unsigned)tmp[i]) << 24) | (((long unsigned)tmp[i+1]) << 16) | (((long unsigned int)tmp[i+2]) << 8) | tmp[i+3]);
      }
    }
  }

  ///\brief Constructs a track from a JSON::Value
  Track::Track(Scan & trackRef) {
    if (trackRef.getMember("fragments").getType() == DTSC_STR) {
      char * tmp = 0;
      unsigned int tmplen = 0;
      trackRef.getMember("fragments").getString(tmp, tmplen);
      fragments = std::deque<Fragment>((Fragment *)tmp, ((Fragment *)tmp) + (tmplen / PACKED_FRAGMENT_SIZE));
    }
    if (trackRef.getMember("keys").getType() == DTSC_STR) {
      char * tmp = 0;
      unsigned int tmplen = 0;
      trackRef.getMember("keys").getString(tmp, tmplen);
      keys = std::deque<Key>((Key *)tmp, ((Key *)tmp) + (tmplen / PACKED_KEY_SIZE));
    }
    if (trackRef.getMember("parts").getType() == DTSC_STR) {
      char * tmp = 0;
      unsigned int tmplen = 0;
      trackRef.getMember("parts").getString(tmp, tmplen);
      parts = std::deque<Part>((Part *)tmp, ((Part *)tmp) + (tmplen / 9));
    }
    trackID = trackRef.getMember("trackid").asInt();
    firstms = trackRef.getMember("firstms").asInt();
    lastms = trackRef.getMember("lastms").asInt();
    bps = trackRef.getMember("bps").asInt();
    missedFrags = trackRef.getMember("missed_frags").asInt();
    codec = trackRef.getMember("codec").asString();
    type = trackRef.getMember("type").asString();
    init = trackRef.getMember("init").asString();
    if (trackRef.getMember("lang")){
      lang = trackRef.getMember("lang").asString();
    }
    if (type == "audio") {
      rate = trackRef.getMember("rate").asInt();
      size = trackRef.getMember("size").asInt();
      channels = trackRef.getMember("channels").asInt();
    }
    if (type == "video") {
      width = trackRef.getMember("width").asInt();
      height = trackRef.getMember("height").asInt();
      fpks = trackRef.getMember("fpks").asInt();
    }
    if (trackRef.getMember("keysizes").getType() == DTSC_STR) {
      char * tmp = 0;
      unsigned int tmplen = 0;
      trackRef.getMember("keysizes").getString(tmp, tmplen);
      for (unsigned int i = 0; i < tmplen; i += 4){
        keySizes.push_back((((long unsigned)tmp[i]) << 24) | (((long unsigned)tmp[i+1]) << 16) | (((long unsigned int)tmp[i+2]) << 8) | tmp[i+3]);
      }
    }
  }

  ///\brief Updates a track and its metadata given new packet properties.
  ///Will also insert keyframes on non-video tracks, and creates fragments
  void Track::update(long long packTime, long long packOffset, long long packDataSize, uint64_t packBytePos, bool isKeyframe, long long packSendSize, unsigned long segment_size) {
    if ((unsigned long long)packTime < lastms) {
      static bool warned = false;
      if (!warned){
        ERROR_MSG("Received packets for track %u in wrong order (%lld < %llu) - ignoring! Further messages on HIGH level.", trackID, packTime, lastms);
        warned = true;
      }else{
        HIGH_MSG("Received packets for track %u in wrong order (%lld < %llu) - ignoring! Further messages on HIGH level.", trackID, packTime, lastms);
      }
      return;
    }
    Part newPart;
    newPart.setSize(packDataSize);
    newPart.setOffset(packOffset);
    if (parts.size()) {
      parts[parts.size() - 1].setDuration(packTime - lastms);
      newPart.setDuration(packTime - lastms);
    } else {
      newPart.setDuration(0);
    }
    parts.push_back(newPart);
    lastms = packTime;
    if (isKeyframe || !keys.size() || (type != "video" && packTime >= AUDIO_KEY_INTERVAL && packTime - (unsigned long long)keys[keys.size() - 1].getTime() >= AUDIO_KEY_INTERVAL)){
      Key newKey;
      newKey.setTime(packTime);
      newKey.setParts(0);
      newKey.setLength(0);
      if (keys.size()) {
        newKey.setNumber(keys[keys.size() - 1].getNumber() + 1);
        keys[keys.size() - 1].setLength(packTime - keys[keys.size() - 1].getTime());
      } else {
        newKey.setNumber(1);
      }
      if (packBytePos >= 0) { //For VoD
        newKey.setBpos(packBytePos);
      } else {
        newKey.setBpos(0);
      }
      keys.push_back(newKey);
      keySizes.push_back(0);
      firstms = keys[0].getTime();
      if (!fragments.size() || ((unsigned long long)packTime > segment_size && (unsigned long long)packTime - segment_size >= (unsigned long long)getKey(fragments.rbegin()->getNumber()).getTime())) {
        //new fragment
        Fragment newFrag;
        newFrag.setDuration(0);
        newFrag.setLength(1);
        newFrag.setNumber(keys[keys.size() - 1].getNumber());
        if (fragments.size()) {
          fragments[fragments.size() - 1].setDuration(packTime - getKey(fragments[fragments.size() - 1].getNumber()).getTime());
          unsigned int newBps = (fragments[fragments.size() - 1].getSize() * 1000) / fragments[fragments.size() - 1].getDuration();
          if (newBps > bps){
            bps = newBps;
          }
        }
        newFrag.setDuration(0);
        newFrag.setSize(0);
        fragments.push_back(newFrag);
        //We set the insert time lastms-firstms in the future, to prevent unstable playback
        fragInsertTime.push_back(Util::bootSecs() + ((lastms - firstms)/1000));
      } else {
        Fragment & lastFrag = fragments[fragments.size() - 1];
        lastFrag.setLength(lastFrag.getLength() + 1);
      }
    }
    keys.rbegin()->setParts(keys.rbegin()->getParts() + 1);
    (*keySizes.rbegin()) += packSendSize;
    fragments.rbegin()->setSize(fragments.rbegin()->getSize() + packDataSize);
  }

  /// Removes the first buffered key, including any fragments it was part of
  void Track::removeFirstKey(){
    HIGH_MSG("Erasing key %d:%lu", trackID, keys[0].getNumber());
    //remove all parts of this key
    for (int i = 0; i < keys[0].getParts(); i++) {
      parts.pop_front();
    }
    //remove the key itself
    keys.pop_front();
    keySizes.pop_front();
    //update firstms
    firstms = keys[0].getTime();
    //delete any fragments no longer fully buffered
    while (fragments.size() && keys.size() && fragments[0].getNumber() < keys[0].getNumber()) {
      fragments.pop_front();
      fragInsertTime.pop_front();
      //and update the missed fragment counter
      ++missedFrags;
    }
  }

  /// Returns the amount of whole seconds since the first fragment was inserted into the buffer.
  /// This assumes playback from the start of the buffer at time of insert, meaning that
  /// the time is offset by that difference. E.g.: if a buffer is 50s long, the newest fragment
  /// will have a value of 0 until 50s have passed, after which it will increase at a rate of
  /// 1 per second.
  uint32_t Track::secsSinceFirstFragmentInsert(){
    uint32_t bs = Util::bootSecs();
    if (bs > fragInsertTime.front()){
      return bs - fragInsertTime.front();
    }else{
      return 0;
    }
  }
  
  void Track::finalize(){
    keys.rbegin()->setLength(lastms - keys.rbegin()->getTime() + parts.rbegin()->getDuration());
  }

  /// Returns the duration in ms of the longest-duration fragment.
  uint32_t Track::biggestFragment(){
    uint32_t ret = 0;
    for (unsigned int i = 0; i<fragments.size(); i++){
      if (fragments[i].getDuration() > ret){
        ret = fragments[i].getDuration();
      }
    }
    return ret;
  }
  
  ///\brief Returns a key given its number, or an empty key if the number is out of bounds
  Key & Track::getKey(unsigned int keyNum) {
    static Key empty;
    if (!keys.size() || keyNum < keys[0].getNumber()) {
      return empty;
    }
    if ((keyNum - keys[0].getNumber()) > keys.size()) {
      return empty;
    }
    return keys[keyNum - keys[0].getNumber()];
  }

  /// Returns the number of the key containing timestamp, or last key if nowhere.
  unsigned int Track::timeToKeynum(unsigned int timestamp){
    unsigned int result = 0;
    for (std::deque<Key>::iterator it = keys.begin(); it != keys.end(); it++){
      if (it->getTime() > timestamp){
        break;
      }
      result = it->getNumber();
    }
    return result;
  }

  /// Gets indice of the fragment containing timestamp, or last fragment if nowhere.
  uint32_t Track::timeToFragnum(uint64_t timestamp){
    uint32_t i = 0;
    for (std::deque<Fragment>::iterator it = fragments.begin(); it != fragments.end(); ++it){
      if (timestamp < getKey(it->getNumber()).getTime() + it->getDuration()){
        return i;
      }
      ++i;
    }
    return fragments.size()-1;
  }

  ///\brief Resets a track, clears all meta values
  void Track::reset() {
    fragments.clear();
    fragInsertTime.clear();
    parts.clear();
    keySizes.clear();
    keys.clear();
    bps = 0;
    firstms = 0;
    lastms = 0;
  }

  ///\brief Creates an empty meta object
  Meta::Meta() {
    vod = false;
    live = false;
    version = DTSH_VERSION;
    moreheader = 0;
    merged = false;
    bufferWindow = 0;
  }

  Meta::Meta(const DTSC::Packet & source) {
    reinit(source);
  }

  void Meta::reinit(const DTSC::Packet & source) {
    tracks.clear();
    vod = source.getFlag("vod");
    live = source.getFlag("live");
    version = source.getInt("version");
    merged = source.getFlag("merged");
    bufferWindow = source.getInt("buffer_window");
    moreheader = source.getInt("moreheader");
    source.getString("source", sourceURI);
    Scan tmpTracks = source.getScan().getMember("tracks");
    unsigned int num = 0;
    Scan tmpTrack;
    do {
      tmpTrack = tmpTracks.getIndice(num);
      if (tmpTrack.asBool()) {
        unsigned int trackId = tmpTrack.getMember("trackid").asInt();
        if (trackId) {
          tracks[trackId] = Track(tmpTrack);
        }
        num++;
      }
    } while (tmpTrack.asBool());
  }

  ///\brief Creates a meta object from a JSON::Value
  Meta::Meta(JSON::Value & meta) {
    vod = meta.isMember("vod") && meta["vod"];
    live = meta.isMember("live") && meta["live"];
    sourceURI = meta.isMember("source") ? meta["source"].asStringRef() : "";
    version = meta.isMember("version") ? meta["version"].asInt() : 0;
    merged = meta.isMember("merged") && meta["merged"];
    bufferWindow = 0;
    if (meta.isMember("buffer_window")) {
      bufferWindow = meta["buffer_window"].asInt();
    }
    //for (JSON::ObjIter it = meta["tracks"].ObjBegin(); it != meta["tracks"].ObjEnd(); it++) {
    jsonForEach(meta["tracks"], it) {
      if ((*it)["trackid"].asInt()) {
        tracks[(*it)["trackid"].asInt()] = Track((*it));
      }
    }
    if (meta.isMember("moreheader")) {
      moreheader = meta["moreheader"].asInt();
    } else {
      moreheader = 0;
    }
  }

  ///\brief Updates a meta object given a JSON::Value
  void Meta::update(JSON::Value & pack, unsigned long segment_size) {
    update(pack["time"].asInt(), pack.isMember("offset")?pack["offset"].asInt():0, pack["trackid"].asInt(), pack["data"].asStringRef().size(), pack.isMember("bpos")?pack["bpos"].asInt():0, pack.isMember("keyframe"), pack.packedSize(), segment_size);
  }

  ///\brief Updates a meta object given a DTSC::Packet
  void Meta::update(DTSC::Packet & pack, unsigned long segment_size) {
    char * data;
    unsigned int dataLen;
    pack.getString("data", data, dataLen);
    update(pack.getTime(), pack.hasMember("offset")?pack.getInt("offset"):0, pack.getTrackId(), dataLen, pack.hasMember("bpos")?pack.getInt("bpos"):0, pack.hasMember("keyframe"), pack.getDataLen(), segment_size);
  }

  ///\brief Updates a meta object given a DTSC::Packet with byte position override.
  void Meta::updatePosOverride(DTSC::Packet & pack, uint64_t bpos) {
    char * data;
    unsigned int dataLen;
    pack.getString("data", data, dataLen);
    update(pack.getTime(), pack.hasMember("offset")?pack.getInt("offset"):0, pack.getTrackId(), dataLen, bpos, pack.hasMember("keyframe"), pack.getDataLen());
  }

  void Meta::update(long long packTime, long long packOffset, long long packTrack, long long packDataSize, uint64_t packBytePos, bool isKeyframe, long long packSendSize, unsigned long segment_size){
    if (!packSendSize){
      //time and trackID are part of the 20-byte header.
      //the container object adds 4 bytes (plus 2+namelen for each content, see below)
      //offset, if non-zero, adds 9 bytes (integer type) and 8 bytes (2+namelen)
      //bpos, if >= 0, adds 9 bytes (integer type) and 6 bytes (2+namelen)
      //keyframe, if true, adds 9 bytes (integer type) and 10 bytes (2+namelen)
      //data adds packDataSize+5 bytes (string type) and 6 bytes (2+namelen)
      packSendSize = 24 + (packOffset?17:0) + (packBytePos>=0?15:0) + (isKeyframe?19:0) + packDataSize+11;
    }
    if (vod != (packBytePos > 0)){
      INFO_MSG("Changing stream from %s to %s (bPos=%lld)", vod?"VoD":"live", (packBytePos >= 0)?"Vod":"live", packBytePos);
    }
    vod = (packBytePos > 0);
    live = !vod;
    EXTREME_MSG("Updating meta with %lld@%lld+%lld", packTrack, packTime, packOffset);
    if (packTrack > 0 && tracks.count(packTrack)){
      tracks[packTrack].update(packTime, packOffset, packDataSize, packBytePos, isKeyframe, packSendSize, segment_size);
    }
  }

  /// Returns a reference to the first video track, or the first track.
  /// Beware: returns a reference to invalid memory if there are no tracks!
  /// Will print a WARN-level message if this is the case.
  Track & Meta::mainTrack(){
    if (!tracks.size()){
      WARN_MSG("Returning nonsense reference - crashing is likely");
      return tracks.begin()->second;
    }
    for (std::map<unsigned int, Track>::iterator it = tracks.begin(); it != tracks.end(); it++) {
      if (it->second.type == "video"){
        return it->second;
      }
    }
    return tracks.begin()->second;
  }

  /// Returns 0 if there are no tracks, otherwise calls mainTrack().biggestFragment().
  uint32_t Meta::biggestFragment(){
    if (!tracks.size()){return 0;}
    return mainTrack().biggestFragment();
  }

  ///\brief Converts a track to a human readable string
  ///\param str The stringstream to append to
  ///\param indent the amount of indentation needed
  ///\param verbosity How verbose the output needs to be
  void Track::toPrettyString(std::ostream & str, int indent, int verbosity) {
    str << std::string(indent, ' ') << "Track " << getWritableIdentifier() << std::endl;
    str << std::string(indent + 2, ' ') << "ID: " << trackID << std::endl;
    str << std::string(indent + 2, ' ') << "Firstms: " << firstms << std::endl;
    str << std::string(indent + 2, ' ') << "Lastms: " << lastms << std::endl;
    str << std::string(indent + 2, ' ') << "Bps: " << bps << std::endl;
    if (missedFrags) {
      str << std::string(indent + 2, ' ') << "missedFrags: " << missedFrags << std::endl;
    }
    str << std::string(indent + 2, ' ') << "Codec: " << codec << std::endl;
    str << std::string(indent + 2, ' ') << "Type: " << type << std::endl;
    str << std::string(indent + 2, ' ') << "Init: ";
    for (unsigned int i = 0; i < init.size(); ++i) {
      str << std::hex << std::setw(2) << std::setfill('0') << (int)init[i];
    }
    str << std::dec << std::endl;
    if (lang.size()){
      str << std::string(indent + 2, ' ') << "Language: " << lang << std::endl;
    }
    if (type == "audio") {
      str << std::string(indent + 2, ' ') << "Rate: " << rate << std::endl;
      str << std::string(indent + 2, ' ') << "Size: " << size << std::endl;
      str << std::string(indent + 2, ' ') << "Channel: " << channels << std::endl;
    } else if (type == "video") {
      str << std::string(indent + 2, ' ') << "Width: " << width << std::endl;
      str << std::string(indent + 2, ' ') << "Height: " << height << std::endl;
      str << std::string(indent + 2, ' ') << "Fpks: " << fpks << std::endl;
    }
    str << std::string(indent + 2, ' ') << "Fragments: " << fragments.size() << std::endl;
    if (verbosity & 0x01) {
      for (unsigned int i = 0; i < fragments.size(); i++) {
        fragments[i].toPrettyString(str, indent + 4);
      }
    }
    str << std::string(indent + 2, ' ') << "Keys: " << keys.size() << std::endl;
    if (verbosity & 0x02) {
      for (unsigned int i = 0; i < keys.size(); i++) {
        keys[i].toPrettyString(str, indent + 4);
      }
    }
    str << std::string(indent + 2, ' ') << "KeySizes: " << keySizes.size() << std::endl;
    if (keySizes.size() && verbosity & 0x02){
      for (unsigned int i = 0; i < keySizes.size(); i++){
        str << std::string(indent + 4, ' ') << "[" << i << "] " << keySizes[i] << std::endl;
      }
    }
    str << std::string(indent + 2, ' ') << "Parts: " << parts.size() << std::endl;
    if (verbosity & 0x04) {
      for (unsigned int i = 0; i < parts.size(); i++) {
        parts[i].toPrettyString(str, indent + 4);
      }
    }
  }

  ///\brief Converts a short to a char*
  char * convertShort(short input) {
    static char result[2];
    result[0] = (input >> 8) & 0xFF;
    result[1] = (input) & 0xFF;
    return result;
  }

  ///\brief Converts an integer to a char*
  char * convertInt(int input) {
    static char result[4];
    result[0] = (input >> 24) & 0xFF;
    result[1] = (input >> 16) & 0xFF;
    result[2] = (input >> 8) & 0xFF;
    result[3] = (input) & 0xFF;
    return result;
  }

  ///\brief Converts a long long to a char*
  char * convertLongLong(long long int input) {
    static char result[8];
    result[0] = (input >> 56) & 0xFF;
    result[1] = (input >> 48) & 0xFF;
    result[2] = (input >> 40) & 0xFF;
    result[3] = (input >> 32) & 0xFF;
    result[4] = (input >> 24) & 0xFF;
    result[5] = (input >> 16) & 0xFF;
    result[6] = (input >> 8) & 0xFF;
    result[7] = (input) & 0xFF;
    return result;
  }


  ///\brief Returns a unique identifier for a track
  std::string Track::getIdentifier() {
    std::stringstream result;
    if (type == "") {
      result << "metadata_" << trackID;
      return result.str();
    }else{
      result << type << "_";
    }
    result << codec << "_";
    if (type == "audio") {
      result << channels << "ch_";
      result << rate << "hz";
    } else if (type == "video") {
      result << width << "x" << height << "_";
      result << (double)fpks / 1000 << "fps";
    }
    if (lang.size() && lang != "und"){
      result << "_" << lang;
    }
    return result.str();
  }

  ///\brief Returns a writable identifier for a track, to prevent overwrites on readout
  std::string Track::getWritableIdentifier() {
    if (cachedIdent.size()){return cachedIdent;}
    std::stringstream result;
    result << getIdentifier() << "_" << trackID;
    cachedIdent = result.str();
    return cachedIdent;
  }

  ///\brief Determines the "packed" size of a track
  int Track::getSendLen(bool skipDynamic) {
    int result = 107 + init.size() + codec.size() + type.size() + getWritableIdentifier().size();
    if (!skipDynamic){
      result += fragments.size() * PACKED_FRAGMENT_SIZE + 16;
      result += keys.size() * PACKED_KEY_SIZE + 11;
    if (keySizes.size()){
        result += (keySizes.size() * 4) + 15;
    }
      result += parts.size() * 9 + 12;
    }
    if (lang.size() && lang != "und"){
      result += 11 + lang.size();
    }
    if (type == "audio") {
      result += 49;
    } else if (type == "video") {
      result += 48;
    }
    if (!skipDynamic && missedFrags) {
      result += 23;
    }
    return result;
  }

  ///\brief Writes a pointer to the specified destination
  ///
  ///Does a memcpy and increases the destination pointer accordingly
  static void writePointer(char *& p, const char * src, unsigned int len) {
    memcpy(p, src, len);
    p += len;
  }

  ///\brief Writes a pointer to the specified destination
  ///
  ///Does a memcpy and increases the destination pointer accordingly
  static void writePointer(char *& p, const std::string & src) {
    writePointer(p, src.data(), src.size());
  }

  ///\brief Writes a track to a pointer
  void Track::writeTo(char *& p) {
    std::deque<Fragment>::iterator firstFrag = fragments.begin(); 
    if (fragments.size() && (&firstFrag) == 0){
      return;
    }
    std::string trackIdent = getWritableIdentifier();
    writePointer(p, convertShort(trackIdent.size()), 2);
    writePointer(p, trackIdent);
    writePointer(p, "\340", 1);//Begin track object
    writePointer(p, "\000\011fragments\002", 12);
    writePointer(p, convertInt(fragments.size() * PACKED_FRAGMENT_SIZE), 4);
    for (; firstFrag != fragments.end(); ++firstFrag) {
      writePointer(p, firstFrag->getData(), PACKED_FRAGMENT_SIZE);
    }
    writePointer(p, "\000\004keys\002", 7);
    writePointer(p, convertInt(keys.size() * PACKED_KEY_SIZE), 4);
    for (std::deque<Key>::iterator it = keys.begin(); it != keys.end(); it++) {
      writePointer(p, it->getData(), PACKED_KEY_SIZE);
    }
    writePointer(p, "\000\010keysizes\002,", 11);
    writePointer(p, convertInt(keySizes.size() * 4), 4);
    std::string tmp;
    tmp.reserve(keySizes.size() * 4);
    for (unsigned int i = 0; i < keySizes.size(); i++){
      tmp += (char)(keySizes[i] >> 24);
      tmp += (char)(keySizes[i] >> 16);
      tmp += (char)(keySizes[i] >> 8);
      tmp += (char)(keySizes[i]);
    }
    writePointer(p, tmp.data(), tmp.size());
    writePointer(p, "\000\005parts\002", 8);
    writePointer(p, convertInt(parts.size() * 9), 4);
    for (std::deque<Part>::iterator it = parts.begin(); it != parts.end(); it++) {
      writePointer(p, it->getData(), 9);
    }
    writePointer(p, "\000\007trackid\001", 10);
    writePointer(p, convertLongLong(trackID), 8);
    if (missedFrags) {
      writePointer(p, "\000\014missed_frags\001", 15);
      writePointer(p, convertLongLong(missedFrags), 8);
    }
    writePointer(p, "\000\007firstms\001", 10);
    writePointer(p, convertLongLong(firstms), 8);
    writePointer(p, "\000\006lastms\001", 9);
    writePointer(p, convertLongLong(lastms), 8);
    writePointer(p, "\000\003bps\001", 6);
    writePointer(p, convertLongLong(bps), 8);
    writePointer(p, "\000\004init\002", 7);
    writePointer(p, convertInt(init.size()), 4);
    writePointer(p, init);
    writePointer(p, "\000\005codec\002", 8);
    writePointer(p, convertInt(codec.size()), 4);
    writePointer(p, codec);
    writePointer(p, "\000\004type\002", 7);
    writePointer(p, convertInt(type.size()), 4);
    writePointer(p, type);
    if (lang.size() && lang != "und"){
      writePointer(p, "\000\004lang\002", 7);
      writePointer(p, convertInt(lang.size()), 4);
      writePointer(p, lang);
    }
    if (type == "audio") {
      writePointer(p, "\000\004rate\001", 7);
      writePointer(p, convertLongLong(rate), 8);
      writePointer(p, "\000\004size\001", 7);
      writePointer(p, convertLongLong(size), 8);
      writePointer(p, "\000\010channels\001", 11);
      writePointer(p, convertLongLong(channels), 8);
    } else if (type == "video") {
      writePointer(p, "\000\005width\001", 8);
      writePointer(p, convertLongLong(width), 8);
      writePointer(p, "\000\006height\001", 9);
      writePointer(p, convertLongLong(height), 8);
      writePointer(p, "\000\004fpks\001", 7);
      writePointer(p, convertLongLong(fpks), 8);
    }
    writePointer(p, "\000\000\356", 3);//End this track Object
  }

  ///\brief Writes a track to a socket
  void Track::send(Socket::Connection & conn, bool skipDynamic) {
    conn.SendNow(convertShort(getWritableIdentifier().size()), 2);
    conn.SendNow(getWritableIdentifier());
    conn.SendNow("\340", 1);//Begin track object
    if (!skipDynamic){
    conn.SendNow("\000\011fragments\002", 12);
      conn.SendNow(convertInt(fragments.size() * PACKED_FRAGMENT_SIZE), 4);
    for (std::deque<Fragment>::iterator it = fragments.begin(); it != fragments.end(); it++) {
        conn.SendNow(it->getData(), PACKED_FRAGMENT_SIZE);
    }
    conn.SendNow("\000\004keys\002", 7);
      conn.SendNow(convertInt(keys.size() * PACKED_KEY_SIZE), 4);
    for (std::deque<Key>::iterator it = keys.begin(); it != keys.end(); it++) {
        conn.SendNow(it->getData(), PACKED_KEY_SIZE);
    }
    conn.SendNow("\000\010keysizes\002,", 11);
    conn.SendNow(convertInt(keySizes.size() * 4), 4);
    std::string tmp;
    tmp.reserve(keySizes.size() * 4);
    for (unsigned int i = 0; i < keySizes.size(); i++){
      tmp += (char)(keySizes[i] >> 24);
      tmp += (char)(keySizes[i] >> 16);
      tmp += (char)(keySizes[i] >> 8);
      tmp += (char)(keySizes[i]);
    }
    conn.SendNow(tmp.data(), tmp.size());
    conn.SendNow("\000\005parts\002", 8);
    conn.SendNow(convertInt(parts.size() * 9), 4);
    for (std::deque<Part>::iterator it = parts.begin(); it != parts.end(); it++) {
      conn.SendNow(it->getData(), 9);
    }
    }
    conn.SendNow("\000\007trackid\001", 10);
    conn.SendNow(convertLongLong(trackID), 8);
    if (!skipDynamic && missedFrags) {
      conn.SendNow("\000\014missed_frags\001", 15);
      conn.SendNow(convertLongLong(missedFrags), 8);
    }
    conn.SendNow("\000\007firstms\001", 10);
    conn.SendNow(convertLongLong(firstms), 8);
    conn.SendNow("\000\006lastms\001", 9);
    conn.SendNow(convertLongLong(lastms), 8);
    conn.SendNow("\000\003bps\001", 6);
    conn.SendNow(convertLongLong(bps), 8);
    conn.SendNow("\000\004init\002", 7);
    conn.SendNow(convertInt(init.size()), 4);
    conn.SendNow(init);
    conn.SendNow("\000\005codec\002", 8);
    conn.SendNow(convertInt(codec.size()), 4);
    conn.SendNow(codec);
    conn.SendNow("\000\004type\002", 7);
    conn.SendNow(convertInt(type.size()), 4);
    conn.SendNow(type);
    if (lang.size() && lang != "und"){
    conn.SendNow("\000\004lang\002", 7);
    conn.SendNow(convertInt(lang.size()), 4);
    conn.SendNow(lang);
    }
    if (type == "audio") {
      conn.SendNow("\000\004rate\001", 7);
      conn.SendNow(convertLongLong(rate), 8);
      conn.SendNow("\000\004size\001", 7);
      conn.SendNow(convertLongLong(size), 8);
      conn.SendNow("\000\010channels\001", 11);
      conn.SendNow(convertLongLong(channels), 8);
    } else if (type == "video") {
      conn.SendNow("\000\005width\001", 8);
      conn.SendNow(convertLongLong(width), 8);
      conn.SendNow("\000\006height\001", 9);
      conn.SendNow(convertLongLong(height), 8);
      conn.SendNow("\000\004fpks\001", 7);
      conn.SendNow(convertLongLong(fpks), 8);
    }
    conn.SendNow("\000\000\356", 3);//End this track Object
  }

  ///\brief Determines the "packed" size of a meta object
  unsigned int Meta::getSendLen(bool skipDynamic, std::set<unsigned long> selectedTracks) {
    unsigned int dataLen = 16 + (vod ? 14 : 0) + (live ? 15 : 0) + (merged ? 17 : 0) + (bufferWindow ? 24 : 0) + 21;
    for (std::map<unsigned int, Track>::iterator it = tracks.begin(); it != tracks.end(); it++) {
      if (!selectedTracks.size() || selectedTracks.count(it->first)){
        dataLen += it->second.getSendLen(skipDynamic);
      }
    }
    if (version){dataLen += 18;}
    if (sourceURI.size()){dataLen += 13+sourceURI.size();}
    return dataLen + 8; //add 8 bytes header
  }

  ///\brief Writes a meta object to a pointer
  void Meta::writeTo(char * p) {
    int dataLen = getSendLen() - 8; //strip 8 bytes header
    writePointer(p, DTSC::Magic_Header, 4);
    writePointer(p, convertInt(dataLen), 4);
    writePointer(p, "\340\000\006tracks\340", 10);
    for (std::map<unsigned int, Track>::iterator it = tracks.begin(); it != tracks.end(); it++) {
      it->second.writeTo(p);
    }
    writePointer(p, "\000\000\356", 3);//End tracks object
    if (vod) {
      writePointer(p, "\000\003vod\001", 6);
      writePointer(p, convertLongLong(1), 8);
    }
    if (live) {
      writePointer(p, "\000\004live\001", 7);
      writePointer(p, convertLongLong(1), 8);
    }
    if (merged) {
      writePointer(p, "\000\006merged\001", 9);
      writePointer(p, convertLongLong(1), 8);
    }
    if (version) {
      writePointer(p, "\000\007version\001", 10);
      writePointer(p, convertLongLong(version), 8);
    }
    if (sourceURI.size()) {
      writePointer(p, "\000\006source\002", 9);
      writePointer(p, convertInt(sourceURI.size()), 4);
      writePointer(p, sourceURI);
    }
    if (bufferWindow) {
      writePointer(p, "\000\015buffer_window\001", 16);
      writePointer(p, convertLongLong(bufferWindow), 8);
    }
    writePointer(p, "\000\012moreheader\001", 13);
    writePointer(p, convertLongLong(moreheader), 8);
    writePointer(p, "\000\000\356", 3);//End global object
  }

  ///\brief Writes a meta object to a socket
  void Meta::send(Socket::Connection & conn, bool skipDynamic, std::set<unsigned long> selectedTracks) {
    int dataLen = getSendLen(skipDynamic, selectedTracks) - 8; //strip 8 bytes header
    conn.SendNow(DTSC::Magic_Header, 4);
    conn.SendNow(convertInt(dataLen), 4);
    conn.SendNow("\340\000\006tracks\340", 10);
    for (std::map<unsigned int, Track>::iterator it = tracks.begin(); it != tracks.end(); it++) {
      if (!selectedTracks.size() || selectedTracks.count(it->first)){
        it->second.send(conn, skipDynamic);
      }
    }
    conn.SendNow("\000\000\356", 3);//End tracks object
    if (vod) {
      conn.SendNow("\000\003vod\001", 6);
      conn.SendNow(convertLongLong(1), 8);
    }
    if (live) {
      conn.SendNow("\000\004live\001", 7);
      conn.SendNow(convertLongLong(1), 8);
    }
    if (merged) {
      conn.SendNow("\000\006merged\001", 9);
      conn.SendNow(convertLongLong(1), 8);
    }
    if (version) {
      conn.SendNow("\000\007version\001", 10);
      conn.SendNow(convertLongLong(version), 8);
    }
    if (sourceURI.size()) {
      conn.SendNow("\000\006source\002", 9);
      conn.SendNow(convertInt(sourceURI.size()), 4);
      conn.SendNow(sourceURI);
    }
    if (bufferWindow) {
      conn.SendNow("\000\015buffer_window\001", 16);
      conn.SendNow(convertLongLong(bufferWindow), 8);
    }
    conn.SendNow("\000\012moreheader\001", 13);
    conn.SendNow(convertLongLong(moreheader), 8);
    conn.SendNow("\000\000\356", 3);//End global object
  }

  ///\brief Converts a track to a JSON::Value
  JSON::Value Track::toJSON(bool skipDynamic) {
    JSON::Value result;
    std::string tmp;
    if (!skipDynamic) {
      tmp.reserve(fragments.size() * PACKED_FRAGMENT_SIZE);
      for (std::deque<Fragment>::iterator it = fragments.begin(); it != fragments.end(); it++) {
        tmp.append(it->getData(), PACKED_FRAGMENT_SIZE);
      }
      result["fragments"] = tmp;
      tmp = "";
      tmp.reserve(keys.size() * PACKED_KEY_SIZE);
      for (std::deque<Key>::iterator it = keys.begin(); it != keys.end(); it++) {
        tmp.append(it->getData(), PACKED_KEY_SIZE);
      }
      result["keys"] = tmp;
      tmp = "";
      tmp.reserve(keySizes.size() * 4);
      for (unsigned int i = 0; i < keySizes.size(); i++){
        tmp += (char)((keySizes[i] >> 24));
        tmp += (char)((keySizes[i] >> 16));
        tmp += (char)((keySizes[i] >> 8));
        tmp += (char)(keySizes[i]);
      }
      result["keysizes"] = tmp;
      tmp = "";
      tmp.reserve(parts.size() * 9);
      for (std::deque<Part>::iterator it = parts.begin(); it != parts.end(); it++) {
        tmp.append(it->getData(), 9);
      }
      result["parts"] = tmp;
    }
    result["init"] = init;
    if (lang.size() && lang != "und"){
      result["lang"] = lang;
    }
    result["trackid"] = trackID;
    result["firstms"] = (long long)firstms;
    result["lastms"] = (long long)lastms;
    result["bps"] = bps;
    if (missedFrags) {
      result["missed_frags"] = missedFrags;
    }
    result["codec"] = codec;
    result["type"] = type;
    if (type == "audio") {
      result["rate"] = rate;
      result["size"] = size;
      result["channels"] = channels;
    } else if (type == "video") {
      result["width"] = width;
      result["height"] = height;
      result["fpks"] = fpks;
    }
    return result;
  }

  ///\brief Converts a meta object to a JSON::Value
  JSON::Value Meta::toJSON() {
    JSON::Value result;
    for (std::map<unsigned int, Track>::iterator it = tracks.begin(); it != tracks.end(); it++) {
      result["tracks"][it->second.getWritableIdentifier()] = it->second.toJSON();
    }
    if (vod) {
      result["vod"] = 1ll;
    }
    if (live) {
      result["live"] = 1ll;
    }
    if (merged) {
      result["merged"] = 1ll;
    }
    if (bufferWindow) {
      result["buffer_window"] = bufferWindow;
    }
    if (version) {
      result["version"] = (long long)version;
    }
    if (sourceURI.size()){
      result["source"] = sourceURI;
    }
    result["moreheader"] = moreheader;
    return result;
  }

  ///\brief Writes metadata to a filename. Wipes existing contents, if any.
  bool Meta::toFile(const std::string & fileName){
    std::ofstream oFile(fileName.c_str());
    oFile << toJSON().toNetPacked();
    oFile.close();
  }

  ///\brief Converts a meta object to a human readable string
  ///\param str The stringstream to append to
  ///\param indent the amount of indentation needed
  ///\param verbosity How verbose the output needs to be
  void Meta::toPrettyString(std::ostream & str, int indent, int verbosity) {
    for (std::map<unsigned int, Track>::iterator it = tracks.begin(); it != tracks.end(); it++) {
      it->second.toPrettyString(str, indent, verbosity);
    }
    if (vod) {
      str << std::string(indent, ' ') << "Video on Demand" << std::endl;
    }
    if (live) {
      str << std::string(indent, ' ') << "Live" << std::endl;
    }
    if (merged) {
      str << std::string(indent, ' ') << "Merged file" << std::endl;
    }
    if (bufferWindow) {
      str << std::string(indent, ' ') << "Buffer Window: " << bufferWindow << std::endl;
    }
    if (sourceURI.size()){
      str << std::string(indent, ' ') << "Source: " << sourceURI << std::endl;
    }
    str << std::string(indent, ' ') << "More Header: " << moreheader << std::endl;
  }

  ///\brief Resets a meta object, removes all unimportant meta values
  void Meta::reset() {
    for (std::map<unsigned int, Track>::iterator it = tracks.begin(); it != tracks.end(); it++) {
      it->second.reset();
    }
  }


  PartIter::PartIter(Track & Trk, Fragment & frag){
    tRef = &Trk;
    pIt = tRef->parts.begin();
    kIt = tRef->keys.begin();
    uint32_t fragNum = frag.getNumber();
    while (kIt->getNumber() < fragNum && kIt != tRef->keys.end()){
      uint32_t kParts = kIt->getParts();
      for (uint32_t pCount = 0; pCount < kParts && pIt != tRef->parts.end(); ++pCount){
        ++pIt;
      }
      ++kIt;
    }
    if (kIt == tRef->keys.end()){tRef = 0;}
    currInKey = 0;
    lastKey = fragNum + frag.getLength();
  }

  /// Dereferences into a Value reference.
  /// If invalid iterator, returns an empty reference and prints a warning message.
  Part & PartIter::operator*() const{
    if (tRef && pIt != tRef->parts.end()){
      return *pIt;
    }
    static Part error;
    WARN_MSG("Dereferenced invalid Part iterator");
    return error;
  }

  /// Dereferences into a Value reference.
  /// If invalid iterator, returns an empty reference and prints a warning message.
  Part* PartIter::operator->() const{
    return &(operator*());
  }

  /// True if not done iterating.
  PartIter::operator bool() const{
    return (tRef && pIt != tRef->parts.end());
  }

  PartIter & PartIter::operator++(){
    if (*this){
      ++pIt;
      if (++currInKey >= kIt->getParts()){
        currInKey = 0;
        //check if we're done iterating - we assume done if past the last key or arrived past the fragment
        if (++kIt == tRef->keys.end() || kIt->getNumber() >= lastKey){
          tRef = 0;
        }
      }
    }
    return *this;
  }



}



