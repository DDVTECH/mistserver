#include "dtsc.h"
#include "defines.h"
#include <cstdlib>
#include <cstring>
#include <iomanip>


/// Retrieves a short in network order from the pointer p.
static short btohs(char * p) {
  return (p[0] << 8) + p[1];
}

/// Stores a short value of val in network order to the pointer p.
static void htobs(char * p, short val) {
  p[0] = (val >> 8) & 0xFF;
  p[1] = val & 0xFF;
}

/// Retrieves a long in network order from the pointer p.
static long btohl(char * p) {
  return (p[0] << 24) + (p[1] << 16) + (p[2] << 8) + p[3];
}

/// Stores a long value of val in network order to the pointer p.
static void htobl(char * p, long val) {
  p[0] = (val >> 24) & 0xFF;
  p[1] = (val >> 16) & 0xFF;
  p[2] = (val >> 8) & 0xFF;
  p[3] = val & 0xFF;
}

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
    Packet(rhs.data, rhs.dataLen, !rhs.master);
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
    if (master && !rhs.master){
      null();
    }
    if (rhs){
      reInit(rhs.data, rhs.dataLen, !rhs.master);
    }else{
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
      DEBUG_MSG(DLVL_DONTEVEN, "Datalen < 8");
      return false;
    }
    if (version == DTSC_INVALID){
      DEBUG_MSG(DLVL_DONTEVEN, "No valid version");
      return false;
    }
    if (ntohl(((int *)data)[1]) + 8 != dataLen) {
      DEBUG_MSG(DLVL_DONTEVEN, "Length mismatch");
      return false;
    }
    return true;
  }

  /// Returns the recognized packet type.
  /// This type is set by reInit and all constructors, and then just referenced from there on. 
  packType Packet::getVersion(){
    return version;
  }

  /// Resets this packet back to the same state as if it had just been freshly constructed.
  /// If needed, this frees the data pointer.
  void Packet::null() {
    if (master && data){
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

  ///\brief Initializes a packet with new data
  ///\param data_ The new data for the packet
  ///\param len The length of the data pointed to by data_
  ///\param noCopy Determines whether to make a copy or not
  void Packet::reInit(const char * data_, unsigned int len, bool noCopy) {
    if (!data_){
      DEBUG_MSG(DLVL_DEVEL, "ReInit received a null pointer with len %d, ignoring", len);
      null();
      return;
    }
    if (data_[0] != 'D' || data_[1] != 'T'){
      DEBUG_MSG(DLVL_HIGH, "ReInit received a pointer that didn't start with 'DT' - data corruption?");
      null();
      return;
    }
    if (len <= 0) {
      len = ntohl(((int *)data_)[1]) + 8;
    }
    //clear any existing controlled contents
    if (master && noCopy){
      null();
    }
    //set control flag to !noCopy
    master = !noCopy;
    //either copy the data, or only the pointer, depending on flag
    if (noCopy){
      data = (char*)data_;
    }else{
      resize(len);
      memcpy(data, data_, len);
    }
    //check header type and store packet length
    dataLen = len;
    version = DTSC_INVALID;
    if (len > 3){
      if (!memcmp(data, Magic_Packet2, 4)){
        version = DTSC_V2;
      }else{
        if (!memcmp(data, Magic_Packet, 4)){
          version = DTSC_V1;
        }else{
          if (!memcmp(data, Magic_Header, 4)){
            version = DTSC_HEAD;
          }else{
            DEBUG_MSG(DLVL_FAIL, "ReInit received a packet with invalid header");
            return;
          }
        }
      }
    }else{
      DEBUG_MSG(DLVL_FAIL, "ReInit received a packet with size < 4");
      return;
    }
  }
  
  /// Helper function for findIdentifier
  static char * findInside(const char * identifier, char *& p, char * max){
    if (p+1 >= max || p[0] == 0x00){
      return (char*)1;//out of packet! 1 == error
    }
    if (p[0] == 0x01){
      //int, skip 9 bytes to next value
      p+=9;
      return 0;
    }
    if (p[0] == 0x02){
      if (p+4 >= max){
        return (char*)1;//out of packet! 1 == error
      }
      //string, read size and skip to next value
      unsigned int tmpi = p[1] * 256 * 256 * 256 + p[2] * 256 * 256 + p[3] * 256 + p[4];
      p += tmpi + 5;
      return 0;
    }
    if (p[0] == 0xE0 || p[0] == 0xFF){
      p++;
      unsigned int id_len = strlen(identifier);
      //object, scan contents
      while (p[0] + p[1] != 0 && p < max){ //while not encountering 0x0000 (we assume 0x0000EE)
        if (p+2 >= max){
          return (char*)1;//out of packet! 1 == error
        }
        unsigned int tmpi = p[0] * 256 + p[1]; //set tmpi to the UTF-8 length
        //compare the name, if match, return contents
        if (tmpi == id_len){
          if (memcmp(p+2, identifier, tmpi) == 0){
            return p+2+tmpi;
          }
        }
        p += 2+tmpi;//skip size
        //otherwise, search through the contents, if needed, and continue
        char * tmp_ret = findInside(identifier, p, max);
        if (tmp_ret){
          return tmp_ret;
        }
      }
      p += 3;//skip end marker
      return 0;
    }
    if (p[0] == 0x0A){
      p++;
      //array, scan contents
      while (p[0] + p[1] != 0 && p < max){ //while not encountering 0x0000 (we assume 0x0000EE)
        //search through contents...
        char * tmp_ret = findInside(identifier, p, max);
        if (tmp_ret){
          return tmp_ret;
        }
        //no match, continue search
      }
      p += 3; //skip end marker
      return 0;
    }
    DEBUG_MSG(DLVL_FAIL, "Unimplemented DTMI type %hhx, @ %p / %p - returning.", p[0], p, max);
    return (char*)1;//out of packet! 1 == error
  }

  ///\brief Locates an identifier within the payload
  ///\param identifier The identifier to find
  ///\return A pointer to the location of the identifier
  char * Packet::findIdentifier(const char * identifier){
    char * p = data;
    if (version == DTSC_V2){
      p += 20;
    }else{
      p += 8;
    }
    char * ret = findInside(identifier, p, data+dataLen);
    return ret;
  }
  
  ///\brief Retrieves a single parameter as a string
  ///\param identifier The name of the parameter
  ///\param result A location on which the string will be returned
  ///\param len An integer in which the length of the string will be returned
  void Packet::getString(const char * identifier, char *& result, int & len) {
    char * pos = findIdentifier(identifier);
    if (pos < (char*)2) {
      result = NULL;
      len = 0;
      return;
    }
    if (pos[0] != 0x02) {
      result = NULL;
      len = 0;
      return;
    }
    result = pos + 5;
    len = ntohl(((int *)(pos + 1))[0]);
  }

  ///\brief Retrieves a single parameter as a string
  ///\param identifier The name of the parameter
  ///\param result The string in which to store the result
  void Packet::getString(const char * identifier, std::string & result) {
    char * data = NULL;
    int len = 0;
    getString(identifier, data, len);
    result = std::string(data, len);
  }

  ///\brief Retrieves a single parameter as an integer
  ///\param identifier The name of the parameter
  ///\param result The result is stored in this integer
  void Packet::getInt(const char * identifier, int & result) {
    char * pos = findIdentifier(identifier);
    if (pos < (char*)2) {
      result = 0;
      return;
    }
    if (pos[0] != 0x01) {
      result = 0;
      return;
    }
    result = ((long long int)pos[1] << 56) | ((long long int)pos[2] << 48) | ((long long int)pos[3] << 40) | ((long long int)pos[4] << 32) | ((long long int)pos[5] << 24) | ((long long int)pos[6] << 16) | ((long long int)pos[7] << 8) | pos[8];
  }

  ///\brief Retrieves a single parameter as an integer
  ///\param identifier The name of the parameter
  ///\result The requested parameter as an integer
  int Packet::getInt(const char * identifier) {
    int result;
    getInt(identifier, result);
    return result;
  }

  ///\brief Retrieves a single parameter as a boolean
  ///\param identifier The name of the parameter
  ///\param result The result is stored in this boolean
  void Packet::getFlag(const char * identifier, bool & result) {
    int result_;
    getInt(identifier, result_);
    result = (bool)result_;
  }

  ///\brief Retrieves a single parameter as a boolean
  ///\param identifier The name of the parameter
  ///\result The requested parameter as a boolean
  bool Packet::getFlag(const char * identifier) {
    bool result;
    getFlag(identifier, result);
    return result;
  }

  ///\brief Checks whether a parameter exists
  ///\param identifier The name of the parameter
  ///\result Whether the parameter exists or not
  bool Packet::hasMember(const char * identifier) {
    return findIdentifier(identifier) > (char*)2;
  }

  ///\brief Returns the timestamp of the packet.
  ///\return The timestamp of this packet.
  long long unsigned int Packet::getTime() {
    if (version != DTSC_V2){
      if (!data){return 0;}
      return getInt("time");
    }
    return ((long long int)ntohl(((int *)(data + 12))[0]) << 32) | ntohl(((int *)(data + 12))[1]);
  }

  ///\brief Returns the track id of the packet.
  ///\return The track id of this packet.
  long int Packet::getTrackId() {
    if (version != DTSC_V2){
      return getInt("trackid");
    }
    return ntohl(((int *)data)[2]);
  }

  ///\brief Returns a pointer to the payload of this packet.
  ///\return A pointer to the payload of this packet.
  char * Packet::getData() {
    return data;
  }

  ///\brief Returns the size of the payload of this packet.
  ///\return The size of the payload of this packet.
  int Packet::getDataLen() {
    return dataLen;
  
  }

  ///\brief Converts the packet into a JSON value
  ///\return A JSON::Value representation of this packet.
  JSON::Value Packet::toJSON(){
    JSON::Value result;
    unsigned int i = 8;
    if (getVersion() == DTSC_V1){
      JSON::fromDTMI((const unsigned char *)data, dataLen, i, result);
    }
    if (getVersion() == DTSC_V2){
      JSON::fromDTMI2((const unsigned char *)data, dataLen, i, result);
    }
    return result;
  }


  ///\brief Returns the payloadsize of a part
  long Part::getSize() {
    return ((long)data[0] << 16) | ((long)data[1] << 8) | data[2];
  }

  ///\brief Sets the payloadsize of a part
  void Part::setSize(long newSize) {
    data[0] = (newSize & 0xFF0000) >> 16;
    data[1] = (newSize & 0x00FF00) >> 8;
    data[2] = (newSize & 0x0000FF);
  }

  ///\brief Retruns the duration of a part
  short Part::getDuration() {
    return btohs(data + 3);
  }

  ///\brief Sets the duration of a part
  void Part::setDuration(short newDuration) {
    htobs(data + 3, newDuration);
  }

  ///\brief returns the offset of a part
  long Part::getOffset() {
    return btohl(data + 5);
  }

  ///\brief Sets the offset of a part
  void Part::setOffset(long newOffset) {
    htobl(data + 5, newOffset);
  }

  ///\brief Returns the data of a part
  char * Part::getData() {
    return data;
  }

  ///\brief Converts a part to a human readable string
  ///\param str The stringstream to append to
  ///\param indent the amount of indentation needed
  void Part::toPrettyString(std::ostream & str, int indent){
    str << std::string(indent, ' ') << "Part: Size(" << getSize() << "), Dur(" << getDuration() << "), Offset(" << getOffset() << ")" << std::endl;
  }

  ///\brief Returns the byteposition of a keyframe
  long long unsigned int Key::getBpos() {
    return (((long long unsigned int)data[0] << 32) | (data[1] << 24) | (data[2] << 16) | (data[3] << 8) | data[4]);
  }

  ///\brief Returns the byteposition of a keyframe
  void Key::setBpos(long long unsigned int newBpos) {
    data[4] = newBpos & 0xFF;
    data[3] = (newBpos >> 8) & 0xFF;
    data[2] = (newBpos >> 16) & 0xFF;
    data[1] = (newBpos >> 24) & 0xFF;
    data[0] = (newBpos >> 32) & 0xFF;
  }

  ///\brief Returns the byteposition of a keyframe
  long Key::getLength() {
    return ((data[5] << 16) | (data[6] << 8) | data[7]);
  }

  ///\brief Sets the byteposition of a keyframe
  void Key::setLength(long newLength) {
    data[7] = newLength & 0xFF;
    data[6] = (newLength >> 8) & 0xFF;
    data[5] = (newLength >> 16) & 0xFF;
  }

  ///\brief Returns the number of a keyframe
  unsigned short Key::getNumber() {
    return btohs(data + 8);
  }

  ///\brief Sets the number of a keyframe
  void Key::setNumber(unsigned short newNumber) {
    htobs(data + 8, newNumber);
  }

  ///\brief Returns the number of parts of a keyframe
  short Key::getParts() {
    return btohs(data + 10);
  }

  ///\brief Sets the number of parts of a keyframe
  void Key::setParts(short newParts) {
    htobs(data + 10, newParts);
  }

  ///\brief Returns the timestamp of a keyframe
  long Key::getTime() {
    return btohl(data + 12);
  }

  ///\brief Sets the timestamp of a keyframe
  void Key::setTime(long newTime) {
    htobl(data + 12, newTime);
  }

  ///\brief Returns the data of this keyframe struct
  char * Key::getData() {
    return data;
  }

  ///\brief Converts a keyframe to a human readable string
  ///\param str The stringstream to append to
  ///\param indent the amount of indentation needed
  void Key::toPrettyString(std::ostream & str, int indent){
    str << std::string(indent, ' ') << "Key " << getNumber() << ": Pos(" << getBpos() << "), Dur(" << getLength() << "), Parts(" << getParts() <<  "), Time(" << getTime() << ")" << std::endl;
  }

  ///\brief Returns the duration of this fragment
  long Fragment::getDuration() {
    return btohl(data);
  }

  ///\brief Sets the duration of this fragment
  void Fragment::setDuration(long newDuration) {
    htobl(data, newDuration);
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
  short Fragment::getNumber() {
    return btohs(data + 5);
  }

  ///\brief Sets the number of the first keyframe in this fragment
  void Fragment::setNumber(short newNumber) {
    htobs(data + 5, newNumber);
  }

  ///\brief Returns the size of a fragment
  long Fragment::getSize() {
    return btohl(data + 7);
  }

  ///\brief Sets the size of a fragement
  void Fragment::setSize(long newSize) {
    htobl(data + 7, newSize);
  }

  ///\brief Returns thte data of this fragment structure
  char * Fragment::getData() {
    return data;
  }

  ///\brief Converts a fragment to a human readable string
  ///\param str The stringstream to append to
  ///\param indent the amount of indentation needed
  void Fragment::toPrettyString(std::ostream & str, int indent){
    str << std::string(indent, ' ') << "Fragment " << getNumber() << ": Dur(" << getDuration() << "), Len(" << (int)getLength() << "), Size(" << getSize() << ")" << std::endl;
  }

  ///\brief Constructs an empty readOnlyTrack
  readOnlyTrack::readOnlyTrack() {
    fragments = NULL;
    fragLen = 0;
    keys = NULL;
    keyLen = 0;
    parts = NULL;
    partLen = 0;
    missedFrags = 0;
    firstms = 0;
    lastms = 0;
    bps = 0;
    rate = 0;
    size = 0;
    channels = 0;
    width = 0;
    height = 0;
    fpks = 0;
  }

  ///\brief Constructs a readOnlyTrack from a JSON::Value
  readOnlyTrack::readOnlyTrack(JSON::Value & trackRef) {
    if (trackRef.isMember("fragments") && trackRef["fragments"].isString()) {
      fragments = (Fragment *)trackRef["fragments"].asStringRef().data();
      fragLen = trackRef["fragments"].asStringRef().size() / 11;
    } else {
      fragments = 0;
      fragLen = 0;
    }
    if (trackRef.isMember("keys") && trackRef["keys"].isString()) {
      keys = (Key *)trackRef["keys"].asStringRef().data();
      keyLen = trackRef["keys"].asStringRef().size() / 16;
    } else {
      keys = 0;
      keyLen = 0;
    }
    if (trackRef.isMember("parts") && trackRef["parts"].isString()) {
      parts = (Part *)trackRef["parts"].asStringRef().data();
      partLen = trackRef["parts"].asStringRef().size() / 9;
    } else {
      parts = 0;
      partLen = 0;
    }
    trackID = trackRef["trackid"].asInt();
    firstms = trackRef["firstms"].asInt();
    lastms = trackRef["lastms"].asInt();
    bps = trackRef["bps"].asInt();
    missedFrags = trackRef["missed_frags"].asInt();
    codec = trackRef["codec"].asStringRef();
    type = trackRef["type"].asStringRef();
    init = trackRef["init"].asStringRef();
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
    if (codec == "vorbis" || codec == "theora") {
      idHeader = trackRef["idheader"].asStringRef();
      commentHeader = trackRef["commentheader"].asStringRef();
    }
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

  ///\brief Constructs a track from a readOnlyTrack
  Track::Track(const readOnlyTrack & rhs) {
    trackID = rhs.trackID;
    firstms = rhs.firstms;
    lastms = rhs.lastms;
    bps = rhs.bps;
    missedFrags = rhs.missedFrags;
    init = rhs.init;
    codec = rhs.codec;
    type = rhs.type;
    rate = rhs.rate;
    size = rhs.size;
    channels = rhs.channels;
    width = rhs.width;
    height = rhs.height;
    fpks = rhs.fpks;
    idHeader = rhs.idHeader;
    commentHeader = rhs.commentHeader;
    if (rhs.fragments && rhs.fragLen) {
      fragments = std::deque<Fragment>(rhs.fragments, rhs.fragments + rhs.fragLen);
    }
    if (rhs.keys && rhs.keyLen) {
      keys = std::deque<Key>(rhs.keys, rhs.keys + rhs.keyLen);
    }
    if (rhs.parts && rhs.partLen) {
      parts = std::deque<Part>(rhs.parts, rhs.parts + rhs.partLen);
    }
  }

  ///\brief Constructs a track from a JSON::Value
  Track::Track(JSON::Value & trackRef) {
    if (trackRef.isMember("fragments") && trackRef["fragments"].isString()) {
      Fragment * tmp = (Fragment *)trackRef["fragments"].asStringRef().data();
      fragments = std::deque<Fragment>(tmp, tmp + (trackRef["fragments"].asStringRef().size() / 11));
    }
    if (trackRef.isMember("keys") && trackRef["keys"].isString()) {
      Key * tmp = (Key *)trackRef["keys"].asStringRef().data();
      keys = std::deque<Key>(tmp, tmp + (trackRef["keys"].asStringRef().size() / 16));
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
    if (codec == "vorbis" || codec == "theora") {
      idHeader = trackRef["idheader"].asStringRef();
      commentHeader = trackRef["commentheader"].asStringRef();
    }
  }

  ///\brief Updates a track and its metadata given a DTSC::Packet.
  ///
  ///Will also insert keyframes on non-video tracks, and creates fragments
  void Track::update(DTSC::Packet & pack) {
    if (pack.getTime() < lastms) {
      DEBUG_MSG(DLVL_WARN, "Received packets for track %d in wrong order (%d < %d) - ignoring!", (int)trackID, (int)pack.getTime(), (int)lastms);
      return;
    }
    Part newPart;
    char * data;
    int dataLen;
    pack.getString("data", data, dataLen);
    newPart.setSize(dataLen);
    newPart.setOffset(pack.getInt("offset"));
    if (parts.size()) {
      parts[parts.size() - 1].setDuration(pack.getTime() - lastms);
      newPart.setDuration(pack.getTime() - lastms);
    } else {
      newPart.setDuration(0);
    }
    parts.push_back(newPart);
    lastms = pack.getTime();
    if (pack.getFlag("keyframe") || !keys.size() || (type != "video" && pack.getTime() > 5000 && pack.getTime() - 5000 > keys[keys.size() - 1].getTime())) {
      Key newKey;
      newKey.setTime(pack.getTime());
      newKey.setParts(0);
      newKey.setLength(0);
      if (keys.size()) {
        newKey.setNumber(keys[keys.size() - 1].getNumber() + 1);
        keys[keys.size() - 1].setLength(pack.getTime() - keys[keys.size() - 1].getTime());
      } else {
        newKey.setNumber(1);
      }
      if (pack.hasMember("bpos")) { //For VoD
        newKey.setBpos(pack.getInt("bpos"));
      } else {
        newKey.setBpos(0);
      }
      keys.push_back(newKey);
      firstms = keys[0].getTime();
      if (!fragments.size() || pack.getTime() - 5000 >= getKey(fragments.rbegin()->getNumber()).getTime()) {
        //new fragment
        Fragment newFrag;
        newFrag.setDuration(0);
        newFrag.setLength(1);
        newFrag.setNumber(keys[keys.size() - 1].getNumber());
        if (fragments.size()) {
          fragments[fragments.size() - 1].setDuration(pack.getTime() - getKey(fragments[fragments.size() - 1].getNumber()).getTime());
          if (!bps && fragments[fragments.size() - 1].getDuration() > 1000) {
            bps = (fragments[fragments.size() - 1].getSize() * 1000) / fragments[fragments.size() - 1].getDuration();
          }
        }
        newFrag.setDuration(0);
        newFrag.setSize(0);
        fragments.push_back(newFrag);
      } else {
        Fragment & lastFrag = fragments[fragments.size() - 1];
        lastFrag.setLength(lastFrag.getLength() + 1);
      }
    }
    keys.rbegin()->setParts(keys.rbegin()->getParts() + 1);
    fragments.rbegin()->setSize(fragments.rbegin()->getSize() + dataLen);
  }

  ///\brief Updates a track and its metadata given a JSON::Value
  ///
  ///Will also insert keyframes on non-video tracks, and creates fragments
  void Track::update(JSON::Value & pack) {
    if (pack["time"].asInt() < lastms) {
      DEBUG_MSG(DLVL_WARN, "Received packets for track %d in wrong order (%d < %d) - ignoring!", (int)trackID, (int)pack["time"].asInt(), (int)lastms);
      return;
    }
    Part newPart;
    newPart.setSize(pack["data"].asStringRef().size());
    newPart.setOffset(pack["offset"].asInt());
    if (parts.size()) {
      parts[parts.size() - 1].setDuration(pack["time"].asInt() - lastms);
      newPart.setDuration(pack["time"].asInt() - lastms);
    } else {
      newPart.setDuration(0);
    }
    parts.push_back(newPart);
    lastms = pack["time"].asInt();
    if (pack.isMember("keyframe") || !keys.size() || (type != "video" && pack["time"].asInt() - 5000 > keys[keys.size() - 1].getTime())) {
      Key newKey;
      newKey.setTime(pack["time"].asInt());
      newKey.setParts(0);
      newKey.setLength(0);
      if (keys.size()) {
        newKey.setNumber(keys[keys.size() - 1].getNumber() + 1);
        keys[keys.size() - 1].setLength(pack["time"].asInt() - keys[keys.size() - 1].getTime());
      } else {
        newKey.setNumber(1);
      }
      if (pack.isMember("bpos")) { //For VoD
        newKey.setBpos(pack["bpos"].asInt());
      } else {
        newKey.setBpos(0);
      }
      keys.push_back(newKey);
      firstms = keys[0].getTime();
      if (!fragments.size() || pack["time"].asInt() - 5000 >= getKey(fragments.rbegin()->getNumber()).getTime()) {
        //new fragment
        Fragment newFrag;
        newFrag.setDuration(0);
        newFrag.setLength(1);
        newFrag.setNumber(keys[keys.size() - 1].getNumber());
        if (fragments.size()) {
          fragments[fragments.size() - 1].setDuration(pack["time"].asInt() - getKey(fragments[fragments.size() - 1].getNumber()).getTime());
          if (!bps && fragments[fragments.size() - 1].getDuration() > 1000) {
            bps = (fragments[fragments.size() - 1].getSize() * 1000) / fragments[fragments.size() - 1].getDuration();
          }
        }
        newFrag.setDuration(0);
        newFrag.setSize(0);
        fragments.push_back(newFrag);
      } else {
        Fragment & lastFrag = fragments[fragments.size() - 1];
        lastFrag.setLength(lastFrag.getLength() + 1);
      }
    }
    keys.rbegin()->setParts(keys.rbegin()->getParts() + 1);
    fragments.rbegin()->setSize(fragments.rbegin()->getSize() + pack["data"].asStringRef().size());
  }

  ///\brief Returns a key given its number, or an empty key if the number is out of bounds
  Key & Track::getKey(unsigned int keyNum) {
    static Key empty;
    if (keyNum < keys[0].getNumber()) {
      return empty;
    }
    if ((keyNum - keys[0].getNumber()) > keys.size()) {
      return empty;
    }
    return keys[keyNum - keys[0].getNumber()];
  }

  ///\brief Returns a unique identifier for a track
  std::string readOnlyTrack::getIdentifier() {
    std::stringstream result;
    if (type == "") {
      result << "metadata_" << trackID;
      return result.str();
    }
    result << type << "_";
    result << codec << "_";
    if (type == "audio") {
      result << channels << "ch_";
      result << rate << "hz";
    } else if (type == "video") {
      result << width << "x" << height << "_";
      result << (double)fpks / 1000 << "fps";
    }
    return result.str();
  }

  ///\brief Returns a writable identifier for a track, to prevent overwrites on readout
  std::string readOnlyTrack::getWritableIdentifier() {
    std::stringstream result;
    result << getIdentifier() << "_" << trackID;
    return result.str();
  }

  ///\brief Resets a track, clears all meta values
  void Track::reset() {
    fragments.clear();
    parts.clear();
    keys.clear();
    bps = 0;
    firstms = 0;
    lastms = 0;
  }

  ///\brief Creates an empty read-only meta object
  readOnlyMeta::readOnlyMeta() {
    vod = false;
    live = false;
    merged = false;
    moreheader = 0;
    merged = false;
    bufferWindow = 0;
  }

  ///\brief Creates a read-only meta object from a given JSON::Value
  readOnlyMeta::readOnlyMeta(JSON::Value & meta) {
    vod = meta.isMember("vod") && meta["vod"];
    live = meta.isMember("live") && meta["live"];
    merged = meta.isMember("merged") && meta["merged"];
    bufferWindow = 0;
    if (meta.isMember("buffer_window")) {
      bufferWindow = meta["buffer_window"].asInt();
    }
    for (JSON::ObjIter it = meta["tracks"].ObjBegin(); it != meta["tracks"].ObjEnd(); it++) {
      if (it->second.isMember("trackid") && it->second["trackid"]) {
        tracks[it->second["trackid"].asInt()] = readOnlyTrack(it->second);
      }
    }
    if (meta.isMember("moreheader")) {
      moreheader = meta["moreheader"].asInt();
    } else {
      moreheader = 0;
    }
  }

  ///\brief Converts a read-only track to a human readable string
  ///\param str The stringstream to append to
  ///\param indent the amount of indentation needed
  ///\param verbosity How verbose the output needs to be
  void readOnlyTrack::toPrettyString(std::ostream & str, int indent, int verbosity){
    str << std::string(indent, ' ') << "Track " << getWritableIdentifier() << std::endl;
    str << std::string(indent + 2, ' ') << "ID: " << trackID << std::endl;
    str << std::string(indent + 2, ' ') << "Firstms: " << firstms << std::endl;
    str << std::string(indent + 2, ' ') << "Lastms: " << lastms << std::endl;
    str << std::string(indent + 2, ' ') << "Bps: " << bps << std::endl;
    if (missedFrags){
      str << std::string(indent + 2, ' ') << "missedFrags: " << missedFrags << std::endl;
    }
    str << std::string(indent + 2, ' ') << "Codec: " << codec << std::endl;
    str << std::string(indent + 2, ' ') << "Type: " << type << std::endl;
    str << std::string(indent + 2, ' ') << "Init: ";
    for (unsigned int i = 0; i < init.size(); ++i){
      str << std::hex << std::setw(2) << std::setfill('0') << (int)init[i];
    }
    str << std::dec << std::endl;
    if (type == "audio") {
      str << std::string(indent + 2, ' ') << "Rate: " << rate << std::endl;
      str << std::string(indent + 2, ' ') << "Size: " << size << std::endl;
      str << std::string(indent + 2, ' ') << "Channel: " << channels << std::endl;
    } else if (type == "video") {
      str << std::string(indent + 2, ' ') << "Width: " << width << std::endl;
      str << std::string(indent + 2, ' ') << "Height: " << height << std::endl;
      str << std::string(indent + 2, ' ') << "Fpks: " << fpks << std::endl;
    }
    if (codec == "vorbis" || codec == "theora") {
      str << std::string(indent + 2, ' ') << "IdHeader: " << idHeader << std::endl;
      str << std::string(indent + 2, ' ') << "CommentHeader: " << commentHeader << std::endl;
    }
    str << std::string(indent + 2, ' ') << "Fragments: " << fragLen << std::endl;
    if (fragments && verbosity & 0x01){
      for (unsigned int i = 0; i < fragLen; i++){
        fragments[i].toPrettyString(str, indent + 4);
      }
    }
    str << std::string(indent + 2, ' ') << "Keys: " << keyLen << std::endl;
    if (keys && verbosity & 0x02) {
      for (unsigned int i = 0; i < keyLen; i++){
        keys[i].toPrettyString(str, indent + 4);
      }
    }
    str << std::string(indent + 2, ' ') << "Parts: " << partLen << std::endl;
    if (parts && verbosity & 0x04) {
      for (unsigned int i = 0; i < partLen; i++){
        parts[i].toPrettyString(str, indent + 4);
      }
    }
  }

  ///\brief Creates an empty meta object
  Meta::Meta() {
    vod = false;
    live = false;
    moreheader = 0;
    merged = false;
    bufferWindow = 0;
  }

  ///\brief Creates a meta object from a read-only meta object
  Meta::Meta(const readOnlyMeta & rhs) {
    vod = rhs.vod;
    live = rhs.live;
    merged = rhs.merged;
    bufferWindow = rhs.bufferWindow;
    for (std::map<int, readOnlyTrack>::const_iterator it = rhs.tracks.begin(); it != rhs.tracks.end(); it++) {
      tracks[it->first] = it->second;
    }
    moreheader = rhs.moreheader;
  }

  ///\brief Creates a meta object from a JSON::Value
  Meta::Meta(JSON::Value & meta) {
    vod = meta.isMember("vod") && meta["vod"];
    live = meta.isMember("live") && meta["live"];
    merged = meta.isMember("merged") && meta["merged"];
    bufferWindow = 0;
    if (meta.isMember("buffer_window")) {
      bufferWindow = meta["buffer_window"].asInt();
    }
    for (JSON::ObjIter it = meta["tracks"].ObjBegin(); it != meta["tracks"].ObjEnd(); it++) {
      if (it->second["trackid"].asInt()) {
        tracks[it->second["trackid"].asInt()] = Track(it->second);
      }
    }
    if (meta.isMember("moreheader")) {
      moreheader = meta["moreheader"].asInt();
    } else {
      moreheader = 0;
    }
  }

  ///\brief Updates a meta object given a JSON::Value
  void Meta::update(JSON::Value & pack) {
    vod = pack.isMember("bpos");
    live = !vod;
    if (pack["trackid"].asInt() && tracks.count(pack["trackid"].asInt())) {
      tracks[pack["trackid"].asInt()].update(pack);
    }
  }

  ///\brief Updates a meta object given a DTSC::Packet
  void Meta::update(DTSC::Packet & pack) {
    vod = pack.hasMember("bpos");
    live = !vod;
    if (pack.getTrackId() && tracks.count(pack.getTrackId())) {
      tracks[pack.getTrackId()].update(pack);
    }
  }

  ///\brief Converts a track to a human readable string
  ///\param str The stringstream to append to
  ///\param indent the amount of indentation needed
  ///\param verbosity How verbose the output needs to be
  void Track::toPrettyString(std::ostream & str, int indent, int verbosity){
    str << std::string(indent, ' ') << "Track " << getWritableIdentifier() << std::endl;
    str << std::string(indent + 2, ' ') << "ID: " << trackID << std::endl;
    str << std::string(indent + 2, ' ') << "Firstms: " << firstms << std::endl;
    str << std::string(indent + 2, ' ') << "Lastms: " << lastms << std::endl;
    str << std::string(indent + 2, ' ') << "Bps: " << bps << std::endl;
    if (missedFrags){
      str << std::string(indent + 2, ' ') << "missedFrags: " << missedFrags << std::endl;
    }
    str << std::string(indent + 2, ' ') << "Codec: " << codec << std::endl;
    str << std::string(indent + 2, ' ') << "Type: " << type << std::endl;
    str << std::string(indent + 2, ' ') << "Init: ";
    for (unsigned int i = 0; i < init.size(); ++i){
      str << std::hex << std::setw(2) << std::setfill('0') << (int)init[i];
    }
    str << std::dec << std::endl;
    if (type == "audio") {
      str << std::string(indent + 2, ' ') << "Rate: " << rate << std::endl;
      str << std::string(indent + 2, ' ') << "Size: " << size << std::endl;
      str << std::string(indent + 2, ' ') << "Channel: " << channels << std::endl;
    } else if (type == "video") {
      str << std::string(indent + 2, ' ') << "Width: " << width << std::endl;
      str << std::string(indent + 2, ' ') << "Height: " << height << std::endl;
      str << std::string(indent + 2, ' ') << "Fpks: " << fpks << std::endl;
    }
    if (codec == "vorbis" || codec == "theora") {
      str << std::string(indent + 2, ' ') << "IdHeader: " << idHeader << std::endl;
      str << std::string(indent + 2, ' ') << "CommentHeader: " << commentHeader << std::endl;
    }
    str << std::string(indent + 2, ' ') << "Fragments: " << fragments.size() << std::endl;
    if (verbosity & 0x01){
      for (unsigned int i = 0; i < fragments.size(); i++){
        fragments[i].toPrettyString(str, indent + 4);
      }
    }
    str << std::string(indent + 2, ' ') << "Keys: " << keys.size() << std::endl;
    if (verbosity & 0x02) {
      for (unsigned int i = 0; i < keys.size(); i++){
        keys[i].toPrettyString(str, indent + 4);
      }
    }
    str << std::string(indent + 2, ' ') << "Parts: " << parts.size() << std::endl;
    if (verbosity & 0x04) {
      for (unsigned int i = 0; i < parts.size(); i++){
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

  ///\brief Determines the "packed" size of a read-only track
  int readOnlyTrack::getSendLen() {
    int result = 146 + init.size() + codec.size() + type.size() + getWritableIdentifier().size();
    result += fragLen * 11;
    result += keyLen * 16;
    result += partLen * 9;
    if (type == "audio") {
      result += 49;
    } else if (type == "video") {
      result += 48;
    }
    if (codec == "vorbis" || codec == "theora") {
      result += 15 + idHeader.size();//idheader
      result += 20 + commentHeader.size();//commentheader
    }
    if (missedFrags) {
      result += 23;
    }
    return result;
  }

  ///\brief Determines the "packed" size of a track
  int Track::getSendLen() {
    int result = 146 + init.size() + codec.size() + type.size() + getWritableIdentifier().size();
    result += fragments.size() * 11;
    result += keys.size() * 16;
    result += parts.size() * 9;
    if (type == "audio") {
      result += 49;
    } else if (type == "video") {
      result += 48;
    }
    if (codec == "vorbis" || codec == "theora") {
      result += 15 + idHeader.size();//idheader
      result += 20 + commentHeader.size();//commentheader
    }
    if (missedFrags) {
      result += 23;
    }
    return result;
  }
  
  ///\brief Writes a pointer to the specified destination
  ///
  ///Does a memcpy and increases the destination pointer accordingly
  static void writePointer(char *& p, const char * src, unsigned int len){
    memcpy(p, src, len);
    p += len;
  }

  ///\brief Writes a pointer to the specified destination
  ///
  ///Does a memcpy and increases the destination pointer accordingly
  static void writePointer(char *& p, const std::string & src){
    writePointer(p, src.data(), src.size());
  }

  ///\brief Writes a read-only track to a pointer
  void readOnlyTrack::writeTo(char *& p){
    std::string iden = getWritableIdentifier();
    writePointer(p, convertShort(iden.size()), 2);
    writePointer(p, iden);
    writePointer(p, "\340", 1);//Begin track object
    writePointer(p, "\000\011fragments\002", 12);
    writePointer(p, convertInt(fragLen * 11), 4);
    writePointer(p, (char *)fragments, fragLen * 11);
    writePointer(p, "\000\004keys\002", 7);
    writePointer(p, convertInt(keyLen * 16), 4);
    writePointer(p, (char *)keys, keyLen * 16);
    writePointer(p, "\000\005parts\002", 8);
    writePointer(p, convertInt(partLen * 9), 4);
    writePointer(p, (char *)parts, partLen * 9);
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
    if (codec == "vorbis" || codec == "theora") {
      writePointer(p, "\000\010idheader\002", 11);
      writePointer(p, convertInt(idHeader.size()), 4);
      writePointer(p, idHeader);
      writePointer(p, "\000\015commentheader\002", 16);
      writePointer(p, convertInt(commentHeader.size()), 4);
      writePointer(p, commentHeader);
    }
    writePointer(p, "\000\000\356", 3);//End this track Object
  }
  
  ///\brief Writes a read-only track to a socket
  void readOnlyTrack::send(Socket::Connection & conn) {
    conn.SendNow(convertShort(getWritableIdentifier().size()), 2);
    conn.SendNow(getWritableIdentifier());
    conn.SendNow("\340", 1);//Begin track object
    conn.SendNow("\000\011fragments\002", 12);
    conn.SendNow(convertInt(fragLen * 11), 4);
    conn.SendNow((char *)fragments, fragLen * 11);
    conn.SendNow("\000\004keys\002", 7);
    conn.SendNow(convertInt(keyLen * 16), 4);
    conn.SendNow((char *)keys, keyLen * 16);
    conn.SendNow("\000\005parts\002", 8);
    conn.SendNow(convertInt(partLen * 9), 4);
    conn.SendNow((char *)parts, partLen * 9);
    conn.SendNow("\000\007trackid\001", 10);
    conn.SendNow(convertLongLong(trackID), 8);
    if (missedFrags) {
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
    if (codec == "vorbis" || codec == "theora") {
      conn.SendNow("\000\010idheader\002", 11);
      conn.SendNow(convertInt(idHeader.size()), 4);
      conn.SendNow(idHeader);
      conn.SendNow("\000\015commentheader\002", 16);
      conn.SendNow(convertInt(commentHeader.size()), 4);
      conn.SendNow(commentHeader);
    }
    conn.SendNow("\000\000\356", 3);//End this track Object
  }
  
  ///\brief Writes a track to a pointer
  void Track::writeTo(char *& p){
    writePointer(p, convertShort(getWritableIdentifier().size()), 2);
    writePointer(p, getWritableIdentifier());
    writePointer(p, "\340", 1);//Begin track object
    writePointer(p, "\000\011fragments\002", 12);
    writePointer(p, convertInt(fragments.size() * 11), 4);
    for (std::deque<Fragment>::iterator it = fragments.begin(); it != fragments.end(); it++) {
      writePointer(p, it->getData(), 11);
    }
    writePointer(p, "\000\004keys\002", 7);
    writePointer(p, convertInt(keys.size() * 16), 4);
    for (std::deque<Key>::iterator it = keys.begin(); it != keys.end(); it++) {
      writePointer(p, it->getData(), 16);
    }
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
    if (codec == "vorbis" || codec == "theora") {
      writePointer(p, "\000\010idheader\002", 11);
      writePointer(p, convertInt(idHeader.size()), 4);
      writePointer(p, idHeader);
      writePointer(p, "\000\015commentheader\002", 16);
      writePointer(p, convertInt(commentHeader.size()), 4);
      writePointer(p, commentHeader);
    }
    writePointer(p, "\000\000\356", 3);//End this track Object
  }

  ///\brief Writes a track to a socket
  void Track::send(Socket::Connection & conn) {
    conn.SendNow(convertShort(getWritableIdentifier().size()), 2);
    conn.SendNow(getWritableIdentifier());
    conn.SendNow("\340", 1);//Begin track object
    conn.SendNow("\000\011fragments\002", 12);
    conn.SendNow(convertInt(fragments.size() * 11), 4);
    for (std::deque<Fragment>::iterator it = fragments.begin(); it != fragments.end(); it++) {
      conn.SendNow(it->getData(), 11);
    }
    conn.SendNow("\000\004keys\002", 7);
    conn.SendNow(convertInt(keys.size() * 16), 4);
    for (std::deque<Key>::iterator it = keys.begin(); it != keys.end(); it++) {
      conn.SendNow(it->getData(), 16);
    }
    conn.SendNow("\000\005parts\002", 8);
    conn.SendNow(convertInt(parts.size() * 9), 4);
    for (std::deque<Part>::iterator it = parts.begin(); it != parts.end(); it++) {
      conn.SendNow(it->getData(), 9);
    }
    conn.SendNow("\000\007trackid\001", 10);
    conn.SendNow(convertLongLong(trackID), 8);
    if (missedFrags) {
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
    if (codec == "vorbis" || codec == "theora") {
      conn.SendNow("\000\010idheader\002", 11);
      conn.SendNow(convertInt(idHeader.size()), 4);
      conn.SendNow(idHeader);
      conn.SendNow("\000\015commentheader\002", 16);
      conn.SendNow(convertInt(commentHeader.size()), 4);
      conn.SendNow(commentHeader);
    }
    conn.SendNow("\000\000\356", 3);//End this track Object
  }

  ///\brief Determines the "packed" size of a read-only meta object 
  unsigned int readOnlyMeta::getSendLen(){
    unsigned int dataLen = 16 + (vod ? 14 : 0) + (live ? 15 : 0) + (merged ? 17 : 0) + (bufferWindow ? 24 : 0) + 21;
    for (std::map<int, readOnlyTrack>::iterator it = tracks.begin(); it != tracks.end(); it++) {
      dataLen += it->second.getSendLen();
    }
    return dataLen;
  }
  
  ///\brief Writes a read-only meta object to a pointer
  void readOnlyMeta::writeTo(char * p){
    int dataLen = getSendLen();
    writePointer(p, DTSC::Magic_Header, 4);
    writePointer(p, convertInt(dataLen), 4);
    writePointer(p, "\340\000\006tracks\340", 10);
    for (std::map<int, readOnlyTrack>::iterator it = tracks.begin(); it != tracks.end(); it++) {
      it->second.writeTo(p);
    }
    writePointer(p, "\000\000\356", 3);
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
    if (bufferWindow) {
      writePointer(p, "\000\015buffer_window\001", 16);
      writePointer(p, convertLongLong(bufferWindow), 8);
    }
    writePointer(p, "\000\012moreheader\001", 13);
    writePointer(p, convertLongLong(moreheader), 8);
    writePointer(p, "\000\000\356", 3);//End global object
  }
  
  ///\brief Writes a read-only meta object to a socket
  void readOnlyMeta::send(Socket::Connection & conn) {
    int dataLen = getSendLen();
    conn.SendNow(DTSC::Magic_Header, 4);
    conn.SendNow(convertInt(dataLen), 4);
    conn.SendNow("\340\000\006tracks\340", 10);
    for (std::map<int, readOnlyTrack>::iterator it = tracks.begin(); it != tracks.end(); it++) {
      it->second.send(conn);
    }
    conn.SendNow("\000\000\356", 3);
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
    if (bufferWindow) {
      conn.SendNow("\000\015buffer_window\001", 16);
      conn.SendNow(convertLongLong(bufferWindow), 8);
    }
    conn.SendNow("\000\012moreheader\001", 13);
    conn.SendNow(convertLongLong(moreheader), 8);
    conn.SendNow("\000\000\356", 3);//End global object
  }
  
  ///\brief Determines the "packed" size of a meta object 
  unsigned int Meta::getSendLen(){
    unsigned int dataLen = 16 + (vod ? 14 : 0) + (live ? 15 : 0) + (merged ? 17 : 0) + (bufferWindow ? 24 : 0) + 21;
    for (std::map<int, Track>::iterator it = tracks.begin(); it != tracks.end(); it++) {
      dataLen += it->second.getSendLen();
    }
    return dataLen;
  }
  
  ///\brief Writes a meta object to a pointer
  void Meta::writeTo(char * p){
    int dataLen = getSendLen();
    writePointer(p, DTSC::Magic_Header, 4);
    writePointer(p, convertInt(dataLen), 4);
    writePointer(p, "\340\000\006tracks\340", 10);
    for (std::map<int, Track>::iterator it = tracks.begin(); it != tracks.end(); it++) {
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
    if (bufferWindow) {
      writePointer(p, "\000\015buffer_window\001", 16);
      writePointer(p, convertLongLong(bufferWindow), 8);
    }
    writePointer(p, "\000\012moreheader\001", 13);
    writePointer(p, convertLongLong(moreheader), 8);
    writePointer(p, "\000\000\356", 3);//End global object
  }

  ///\brief Writes a meta object to a socket
  void Meta::send(Socket::Connection & conn) {
    int dataLen = getSendLen();
    conn.SendNow(DTSC::Magic_Header, 4);
    conn.SendNow(convertInt(dataLen), 4);
    conn.SendNow("\340\000\006tracks\340", 10);
    for (std::map<int, Track>::iterator it = tracks.begin(); it != tracks.end(); it++) {
      it->second.send(conn);
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
    if (bufferWindow) {
      conn.SendNow("\000\015buffer_window\001", 16);
      conn.SendNow(convertLongLong(bufferWindow), 8);
    }
    conn.SendNow("\000\012moreheader\001", 13);
    conn.SendNow(convertLongLong(moreheader), 8);
    conn.SendNow("\000\000\356", 3);//End global object
  }

  ///\brief Converts a read-only track to a JSON::Value
  JSON::Value readOnlyTrack::toJSON() {
    JSON::Value result;
    if (fragments) {
      result["fragments"] = std::string((char *)fragments, fragLen * 11);
    }
    if (keys) {
      result["keys"] = std::string((char *)keys, keyLen * 16);
    }
    if (parts) {
      result["parts"] = std::string((char *)parts, partLen * 9);
    }
    result["trackid"] = trackID;
    result["firstms"] = firstms;
    result["lastms"] = lastms;
    result["bps"] = bps;
    if (missedFrags) {
      result["missed_frags"] = missedFrags;
    }
    result["codec"] = codec;
    result["type"] = type;
    result["init"] = init;
    if (type == "audio") {
      result["rate"] = rate;
      result["size"] = size;
      result["channels"] = channels;
    } else if (type == "video") {
      result["width"] = width;
      result["height"] = height;
      result["fpks"] = fpks;
    }
    if (codec == "vorbis" || codec == "theora") {
      result["idheader"] = idHeader;
      result["commentheader"] = commentHeader;
    }
    return result;
  }

  ///\brief Converts a track to a JSON::Value
  JSON::Value Track::toJSON() {
    JSON::Value result;
    std::string tmp;
    tmp.reserve(fragments.size() * 11);
    for (std::deque<Fragment>::iterator it = fragments.begin(); it != fragments.end(); it++) {
      tmp.append(it->getData(), 11);
    }
    result["fragments"] = tmp;
    tmp = "";
    tmp.reserve(keys.size() * 16);
    for (std::deque<Key>::iterator it = keys.begin(); it != keys.end(); it++) {
      tmp.append(it->getData(), 16);
    }
    result["keys"] = tmp;
    tmp = "";
    tmp.reserve(parts.size() * 9);
    for (std::deque<Part>::iterator it = parts.begin(); it != parts.end(); it++) {
      tmp.append(it->getData(), 9);
    }
    result["parts"] = tmp;
    result["trackid"] = trackID;
    result["firstms"] = firstms;
    result["lastms"] = lastms;
    result["bps"] = bps;
    if (missedFrags) {
      result["missed_frags"] = missedFrags;
    }
    result["codec"] = codec;
    result["type"] = type;
    result["init"] = init;
    if (type == "audio") {
      result["rate"] = rate;
      result["size"] = size;
      result["channels"] = channels;
    } else if (type == "video") {
      result["width"] = width;
      result["height"] = height;
      result["fpks"] = fpks;
    }
    if (codec == "vorbis" || codec == "theora") {
      result["idheader"] = idHeader;
      result["commentheader"] = commentHeader;
    }
    return result;
  }

  ///\brief Converts a meta object to a JSON::Value
  JSON::Value Meta::toJSON() {
    JSON::Value result;
    for (std::map<int, Track>::iterator it = tracks.begin(); it != tracks.end(); it++) {
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
    result["moreheader"] = moreheader;
    return result;
  }

  ///\brief Converts a read-only meta object to a human readable string
  ///\param str The stringstream to append to
  ///\param indent the amount of indentation needed
  ///\param verbosity How verbose the output needs to be
  void readOnlyMeta::toPrettyString(std::ostream & str, int indent, int verbosity){
    for (std::map<int, readOnlyTrack>::iterator it = tracks.begin(); it != tracks.end(); it++) {
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
    str << std::string(indent, ' ') << "More Header: " << moreheader << std::endl;
  }

  ///\brief Converts a meta object to a human readable string
  ///\param str The stringstream to append to
  ///\param indent the amount of indentation needed
  ///\param verbosity How verbose the output needs to be
  void Meta::toPrettyString(std::ostream & str, int indent, int verbosity){
    for (std::map<int, Track>::iterator it = tracks.begin(); it != tracks.end(); it++) {
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
    str << std::string(indent, ' ') << "More Header: " << moreheader << std::endl;
  }
  
  ///\brief Converts a read-only meta object to a JSON::Value
  JSON::Value readOnlyMeta::toJSON() {
    JSON::Value result;
    for (std::map<int, readOnlyTrack>::iterator it = tracks.begin(); it != tracks.end(); it++) {
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
    result["moreheader"] = moreheader;
    if (bufferWindow) {
      result["buffer_window"] = bufferWindow;
    }
    return result;
  }

  ///\brief Resets a meta object, removes all unimportant meta values
  void Meta::reset() {
    for (std::map<int, Track>::iterator it = tracks.begin(); it != tracks.end(); it++) {
      it->second.reset();
    }
  }

  ///\brief Returns whether a read-only meta object is fixed or not
  bool readOnlyMeta::isFixed() {
    for (std::map<int, readOnlyTrack>::iterator it = tracks.begin(); it != tracks.end(); it++) {
      if (!it->second.keyLen || !(it->second.keys[it->second.keyLen - 1].getBpos())) {
        return false;
      }
    }
    return true;
  }

  ///\brief Returns whether a meta object is fixed or not
  bool Meta::isFixed() {
    for (std::map<int, Track>::iterator it = tracks.begin(); it != tracks.end(); it++) {
      if (it->second.type == "meta" || it->second.type == "") {
        continue;
      }
      if (!it->second.keys.size() || !(it->second.keys.rbegin()->getBpos())) {
        return false;
      }
    }
    return true;
  }
}



