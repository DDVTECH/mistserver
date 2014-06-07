/// \file dtsc.cpp
/// Holds all code for DDVTECH Stream Container parsing/generation.

#include "dtsc.h"
#include "defines.h"
#include <stdlib.h>
#include <string.h> //for memcmp
#include <arpa/inet.h> //for htonl/ntohl
char DTSC::Magic_Header[] = "DTSC";
char DTSC::Magic_Packet[] = "DTPD";
char DTSC::Magic_Packet2[] = "DTP2";

/// Initializes a DTSC::Stream with only one packet buffer.
DTSC::Stream::Stream(){
  datapointertype = DTSC::INVALID;
  buffercount = 1;
  buffertime = 0;
}

/// Initializes a DTSC::Stream with a minimum of rbuffers packet buffers.
/// The actual buffer count may not at all times be the requested amount.
DTSC::Stream::Stream(unsigned int rbuffers, unsigned int bufferTime){
  datapointertype = DTSC::INVALID;
  if (rbuffers < 1){
    rbuffers = 1;
  }
  buffercount = rbuffers;
  buffertime = bufferTime;
}

/// This function does nothing, it's supposed to be overridden.
/// It will be called right before a buffer position is deleted.
void DTSC::Stream::deletionCallback(livePos deleting){}

/// Returns the time in milliseconds of the last received packet.
/// This is _not_ the time this packet was received, only the stored time.
unsigned int DTSC::Stream::getTime(){
  if ( !buffers.size()){
    return 0;
  }
  return buffers.rbegin()->second["time"].asInt();
}

/// Attempts to parse a packet from the given std::string buffer.
/// Returns true if successful, removing the parsed part from the buffer string.
/// Returns false if invalid or not enough data is in the buffer.
/// \arg buffer The std::string buffer to attempt to parse.
bool DTSC::Stream::parsePacket(std::string & buffer){
  uint32_t len;
  static bool syncing = false;
  if (buffer.length() > 8){
    if (memcmp(buffer.c_str(), DTSC::Magic_Header, 4) == 0){
      len = ntohl(((uint32_t *)buffer.c_str())[1]);
      if (buffer.length() < len + 8){
        return false;
      }
      unsigned int i = 0;
      JSON::Value meta;
      JSON::fromDTMI((unsigned char*)buffer.c_str() + 8, len, i, meta);
      metadata = Meta(meta);
      buffer.erase(0, len + 8);
      if (buffer.length() <= 8){
        return false;
      }
    }
    int version = 0;
    if (memcmp(buffer.c_str(), DTSC::Magic_Packet, 4) == 0){
      version = 1;
    }
    if (memcmp(buffer.c_str(), DTSC::Magic_Packet2, 4) == 0){
      version = 2;
    }
    if (version){
      len = ntohl(((uint32_t *)buffer.c_str())[1]);
      if (buffer.length() < len + 8){
        return false;
      }
      JSON::Value newPack;
      unsigned int i = 0;
      if (version == 1){
        JSON::fromDTMI((unsigned char*)buffer.c_str() + 8, len, i, newPack);
      }
      if (version == 2){
        JSON::fromDTMI2((unsigned char*)buffer.c_str() + 8, len, i, newPack);
      }
      buffer.erase(0, len + 8);
      addPacket(newPack);
      syncing = false;
      return true;
    }
#if DEBUG >= DLVL_WARN
    if (!syncing){
      DEBUG_MSG(DLVL_WARN, "Invalid DTMI data detected - re-syncing");
      syncing = true;
    }
#endif
    size_t magic_search = buffer.find(Magic_Packet);
    size_t magic_search2 = buffer.find(Magic_Packet2);
    if (magic_search2 == std::string::npos){
      if (magic_search == std::string::npos){
        buffer.clear();
      }else{
        buffer.erase(0, magic_search);
      }
    }else{
      buffer.erase(0, magic_search2);
    }
  }
  return false;
}

/// Attempts to parse a packet from the given Socket::Buffer.
/// Returns true if successful, removing the parsed part from the buffer.
/// Returns false if invalid or not enough data is in the buffer.
/// \arg buffer The Socket::Buffer to attempt to parse.
bool DTSC::Stream::parsePacket(Socket::Buffer & buffer){
  uint32_t len;
  static bool syncing = false;
  if (buffer.available(8)){
    std::string header_bytes = buffer.copy(8);
    if (memcmp(header_bytes.c_str(), DTSC::Magic_Header, 4) == 0){
      len = ntohl(((uint32_t *)header_bytes.c_str())[1]);
      if ( !buffer.available(len + 8)){
        return false;
      }
      unsigned int i = 0;
      std::string wholepacket = buffer.remove(len + 8);
      JSON::Value meta;
      JSON::fromDTMI((unsigned char*)wholepacket.c_str() + 8, len, i, meta);
      addMeta(meta);
      //recursively calls itself until failure or data packet instead of header
      return parsePacket(buffer);
    }
    int version = 0;
    if (memcmp(header_bytes.c_str(), DTSC::Magic_Packet, 4) == 0){
      version = 1;
    }
    if (memcmp(header_bytes.c_str(), DTSC::Magic_Packet2, 4) == 0){
      version = 2;
    }
    if (version){
      len = ntohl(((uint32_t *)header_bytes.c_str())[1]);
      if ( !buffer.available(len + 8)){
        return false;
      }
      JSON::Value newPack;
      unsigned int i = 0;
      std::string wholepacket = buffer.remove(len + 8);
      if (version == 1){
        JSON::fromDTMI((unsigned char*)wholepacket.c_str() + 8, len, i, newPack);
      }
      if (version == 2){
        JSON::fromDTMI2((unsigned char*)wholepacket.c_str() + 8, len, i, newPack);
      }
      addPacket(newPack);
      syncing = false;
      return true;
    }
#if DEBUG >= DLVL_WARN
    if (!syncing){
      DEBUG_MSG(DLVL_WARN, "Invalid DTMI data detected - syncing");
      syncing = true;
    }
#endif
    buffer.get().clear();
  }
  return false;
}

/// Adds a keyframe packet to all tracks, so the stream can be fully played.
void DTSC::Stream::endStream(){
  if (!metadata.tracks.size()){return;}
  for (std::map<int,Track>::iterator it = metadata.tracks.begin(); it != metadata.tracks.end(); it++){
    JSON::Value newPack;
    newPack["time"] = (long long)it->second.lastms;
    newPack["trackid"] = it->first;
    newPack["keyframe"] = 1ll;
    newPack["data"] = "";
    addPacket(newPack);
  }
}

/// Blocks until either the stream has metadata available or the sourceSocket errors.
/// This function is intended to be run before any commands are sent and thus will not throw away anything important.
/// It will time out after 3 seconds, disconnecting the sourceSocket.
void DTSC::Stream::waitForMeta(Socket::Connection & sourceSocket){
  bool wasBlocking = sourceSocket.isBlocking();
  sourceSocket.setBlocking(false);
  //cancel the attempt after 5000 milliseconds
  long long int start = Util::getMS();
  while ( !metadata && sourceSocket.connected() && Util::getMS() - start < 3000){
    //we have data? attempt to read header
    if (sourceSocket.Received().size()){
      //return value is ignored because we're not interested in data packets, just metadata.
      parsePacket(sourceSocket.Received());
    }
    //still no header? check for more data
    if ( !metadata){
      if (sourceSocket.spool()){
        //more received? attempt to read
        //return value is ignored because we're not interested in data packets, just metadata.
        parsePacket(sourceSocket.Received());
      }else{
        //nothing extra to receive? wait a bit and retry
        Util::sleep(10);
      }
    }
  }
  sourceSocket.setBlocking(wasBlocking);
  //if the timeout has passed, close the socket
  if (Util::getMS() - start >= 3000){
    sourceSocket.close();
    //and optionally print a debug message that this happened
    DEBUG_MSG(DLVL_DEVEL, "Timing out while waiting for metadata");
  }
}

/// Blocks until either the stream encounters a pause mark or the sourceSocket errors.
/// This function is intended to be run after the 'q' command is sent, throwing away superfluous packets.
/// It will time out after 5 seconds, disconnecting the sourceSocket.
void DTSC::Stream::waitForPause(Socket::Connection & sourceSocket){
  bool wasBlocking = sourceSocket.isBlocking();
  sourceSocket.setBlocking(false);
  //cancel the attempt after 5000 milliseconds
  long long int start = Util::getMS();
  while (lastType() != DTSC::PAUSEMARK && sourceSocket.connected() && Util::getMS() - start < 5000){
    //we have data? parse it
    if (sourceSocket.Received().size()){
      //return value is ignored because we're not interested.
      parsePacket(sourceSocket.Received());
    }
    //still no pause mark? check for more data
    if (lastType() != DTSC::PAUSEMARK){
      if (sourceSocket.spool()){
        //more received? attempt to read
        //return value is ignored because we're not interested in data packets, just metadata.
        parsePacket(sourceSocket.Received());
      }else{
        //nothing extra to receive? wait a bit and retry
        Util::sleep(10);
      }
    }
  }
  sourceSocket.setBlocking(wasBlocking);
  //if the timeout has passed, close the socket
  if (Util::getMS() - start >= 5000){
    sourceSocket.close();
    //and optionally print a debug message that this happened
    DEBUG_MSG(DLVL_DEVEL, "Timing out while waiting for pause break");
  }
}

/// Resets the stream by clearing the buffers and keyframes, making sure to call the deletionCallback first.
void DTSC::Stream::resetStream(){
  for (std::map<livePos, JSON::Value >::iterator it = buffers.begin(); it != buffers.end(); it++){
    deletionCallback(it->first);
  }
  buffers.clear();
  keyframes.clear();
}

/// Adds a set of metadata to the steam.
/// This is implemented by simply replacing the current metadata.
/// This effectively resets the stream.
void DTSC::Stream::addMeta(JSON::Value & newMeta){
  metadata = Meta(newMeta);
}

/// Adds a single DTSC packet to the stream, updating the internal metadata if needed.
void DTSC::Stream::addPacket(JSON::Value & newPack){
  livePos newPos;
  newPos.trackID = newPack["trackid"].asInt();
  newPos.seekTime = newPack["time"].asInt();
  if (!metadata.tracks.count(newPos.trackID) && (!newPack.isMember("mark") || newPack["mark"].asStringRef() != "pause")){return;}
  if (buffercount > 1 && metadata.tracks[newPos.trackID].keys.size() > 1 && newPos.seekTime < (long long unsigned int)metadata.tracks[newPos.trackID].keys.rbegin()->getTime()){
    resetStream();
  }
  while (buffers.count(newPos) > 0){
    newPos.seekTime++;
  }
  while (buffercount == 1 && buffers.size() > 0){
    cutOneBuffer();
  }
  buffers[newPos] = newPack;
  datapointertype = INVALID;
  std::string tmp = "";
  if (newPack.isMember("trackid") && newPack["trackid"].asInt() > 0){
    tmp = metadata.tracks[newPack["trackid"].asInt()].type;
  }
  if (newPack.isMember("datatype")){
    tmp = newPack["datatype"].asStringRef();
  }
  if (tmp == "video"){
    datapointertype = VIDEO;
  }
  if (tmp == "audio"){
    datapointertype = AUDIO;
  }
  if (tmp == "meta"){
    datapointertype = META;
  }
  if (tmp == "pause_marker" || (newPack.isMember("mark") && newPack["mark"].asStringRef() == "pause")){
    datapointertype = PAUSEMARK;
  }
  if (buffercount > 1){
    metadata.update(newPack);
    if (newPack.isMember("keyframe") || (long long unsigned int)metadata.tracks[newPos.trackID].keys.rbegin()->getTime() == newPos.seekTime){
      keyframes[newPos.trackID].insert(newPos);
    }
    metadata.live = true;
    //throw away buffers if buffer time is met
    int trid = buffers.begin()->first.trackID;
    int firstTime = buffers.begin()->first.seekTime;
    int lastTime = buffers.rbegin()->first.seekTime - buffertime;
    while ((!metadata.tracks[trid].keys.size() && firstTime < lastTime) || (metadata.tracks[trid].keys.size() && metadata.tracks[trid].keys.rbegin()->getTime() < lastTime) || (metadata.tracks[trid].keys.size() > 2 && metadata.tracks[trid].keys.rbegin()->getTime() - firstTime > buffertime)){
      cutOneBuffer();
      trid = buffers.begin()->first.trackID;
      firstTime = buffers.begin()->first.seekTime;
    }
    metadata.bufferWindow = buffertime;
  }
  
}

/// Deletes a the first part of the buffer, updating the keyframes list and metadata as required.
/// Will print a warning if a track has less than 2 keyframes left because of this.
void DTSC::Stream::cutOneBuffer(){
  if ( !buffers.size()){return;}
  int trid = buffers.begin()->first.trackID;
  long long unsigned int delTime = buffers.begin()->first.seekTime;
  if (buffercount > 1){
    while (keyframes[trid].size() > 0 && keyframes[trid].begin()->seekTime <= delTime){
      keyframes[trid].erase(keyframes[trid].begin());
    }
    while (metadata.tracks[trid].keys.size() && (long long unsigned int)metadata.tracks[trid].keys[0].getTime() <= delTime){
      for (int i = 0; i < metadata.tracks[trid].keys[0].getParts(); i++){
        metadata.tracks[trid].parts.pop_front();
      }
      metadata.tracks[trid].keys.pop_front();
    }
    if (metadata.tracks[trid].keys.size()){
      metadata.tracks[trid].firstms = metadata.tracks[trid].keys[0].getTime();
      //delete fragments of which the beginning can no longer be reached
      while (metadata.tracks[trid].fragments.size() && metadata.tracks[trid].fragments[0].getNumber() < metadata.tracks[trid].keys[0].getNumber()){
        metadata.tracks[trid].fragments.pop_front();
        //increase the missed fragments counter
        metadata.tracks[trid].missedFrags++;
      }
    }else{
      metadata.tracks[trid].fragments.clear();
    }
  }
  deletionCallback(buffers.begin()->first);
  buffers.erase(buffers.begin());
}

/// Returns a direct pointer to the data attribute of the last received packet, if available.
/// Returns NULL if no valid pointer or packet is available.
std::string & DTSC::Stream::lastData(){
  return buffers.rbegin()->second["data"].strVal;
}

/// Returns the packet in this buffer number.
/// \arg num Buffer number.
JSON::Value & DTSC::Stream::getPacket(livePos num){
  static JSON::Value empty;
  if (buffers.find(num) == buffers.end()){
    return empty;
  }
  return buffers[num];
}

JSON::Value & DTSC::Stream::getPacket(){
  return buffers.begin()->second;
}

/// Returns the type of the last received packet.
DTSC::datatype DTSC::Stream::lastType(){
  return datapointertype;
}

/// Returns true if the current stream contains at least one video track.
bool DTSC::Stream::hasVideo(){
  for (std::map<int,Track>::iterator it = metadata.tracks.begin(); it != metadata.tracks.end(); it++){
    if (it->second.type == "video"){
      return true;
    }
  }
  return false;
}

/// Returns true if the current stream contains at least one audio track.
bool DTSC::Stream::hasAudio(){
  for (std::map<int,Track>::iterator it = metadata.tracks.begin(); it != metadata.tracks.end(); it++){
    if (it->second.type == "audio"){
      return true;
    }
  }
  return false;
}

void DTSC::Stream::setBufferTime(unsigned int ms){
  buffertime = ms;
}

std::string & DTSC::Stream::outPacket(){
  static std::string emptystring;
  if (!buffers.size() || !buffers.rbegin()->second.isObject()){
    return emptystring;
  }
  return buffers.rbegin()->second.toNetPacked();
}

/// Returns a packed DTSC packet, ready to sent over the network.
std::string & DTSC::Stream::outPacket(livePos num){
  static std::string emptystring;
  if (buffers.find(num) == buffers.end() || !buffers[num].isObject()) return emptystring;
  return buffers[num].toNetPacked();
}

/// Returns a packed DTSC header, ready to sent over the network.
std::string & DTSC::Stream::outHeader(){
  return metadata.toJSON().toNetPacked();
}

/// Constructs a new Ring, at the given buffer position.
/// \arg v Position for buffer.
DTSC::Ring::Ring(livePos v){
  b = v;
  waiting = false;
  starved = false;
  updated = false;
  playCount = 0;
}

/// Requests a new Ring, which will be created and added to the internal Ring list.
/// This Ring will be kept updated so it always points to valid data or has the starved boolean set.
/// Don't forget to call dropRing() for all requested Ring classes that are no longer neccessary!
DTSC::Ring * DTSC::Stream::getRing(){
  livePos tmp = buffers.begin()->first;
  std::map<int,std::set<livePos> >::iterator it;
  for (it = keyframes.begin(); it != keyframes.end(); it++){
    if ((*it->second.begin()).seekTime > tmp.seekTime){
      tmp = *it->second.begin();
    }
  }
  return new DTSC::Ring(tmp);
}

/// Deletes a given out Ring class from memory and internal Ring list.
/// Checks for NULL pointers and invalid pointers, silently discarding them.
void DTSC::Stream::dropRing(DTSC::Ring * ptr){
  if (ptr){
    delete ptr;
  }
}

/// Returns 0 if seeking is possible, -1 if the wanted frame is too old, 1 if the wanted frame is too new.
/// This function looks in the header - not in the buffered data itself.
int DTSC::Stream::canSeekms(unsigned int ms){
  bool too_late = false;
  //no tracks? Frame too new by definition.
  if ( !metadata.tracks.size()){
    return 1;
  }
  //loop trough all the tracks
  for (std::map<int,Track>::iterator it = metadata.tracks.begin(); it != metadata.tracks.end(); it++){
    if (it->second.keys.size()){
      if (it->second.keys[0].getTime() <= ms && it->second.keys[it->second.keys.size() - 1].getTime() >= ms){
        return 0;
      }
      if (it->second.keys[0].getTime() > ms){too_late = true;}
    }
  }
  //did we spot a track already past this point? return too late.
  if (too_late){return -1;}
  //otherwise, assume not available yet
  return 1;
}

DTSC::livePos DTSC::Stream::msSeek(unsigned int ms, std::set<int> & allowedTracks){
  std::set<int> seekTracks = allowedTracks;
  livePos result = buffers.begin()->first;
  for (std::set<int>::iterator it = allowedTracks.begin(); it != allowedTracks.end(); it++){
    if (metadata.tracks[*it].type == "video"){
      int trackNo = *it;
      seekTracks.clear();
      seekTracks.insert(trackNo);
      break;
    }
  }
  for (std::map<livePos,JSON::Value>::iterator bIt = buffers.begin(); bIt != buffers.end(); bIt++){
    if (seekTracks.find(bIt->first.trackID) != seekTracks.end()){
    //  if (bIt->second.isMember("keyframe")){
        result = bIt->first;
        if (bIt->first.seekTime >= ms){
          return result;
        }
    //}
    }
  }
  return result;
}

/// Returns whether the current position is the last currently available position within allowedTracks.
/// Simply returns the result of getNext(pos, allowedTracks) == pos
bool DTSC::Stream::isNewest(DTSC::livePos & pos, std::set<int> & allowedTracks){
  return getNext(pos, allowedTracks) == pos;
}

/// Returns the next available position within allowedTracks, or the current position if no next is availble.
DTSC::livePos DTSC::Stream::getNext(DTSC::livePos & pos, std::set<int> & allowedTracks){
  std::map<livePos,JSON::Value>::iterator iter = buffers.upper_bound(pos);
  while (iter != buffers.end()){
    if (allowedTracks.count(iter->first.trackID)){return iter->first;}
    iter++;
  }
  return pos;
}

/// Properly cleans up the object for erasing.
/// Drops all Ring classes that have been given out.
DTSC::Stream::~Stream(){
}

DTSC::File::File(){
  F = 0;
  buffer = malloc(4);
  endPos = 0;
}

DTSC::File::File(const File & rhs){
  buffer = malloc(4);
  *this = rhs;
}

DTSC::File & DTSC::File::operator =(const File & rhs){
  created = rhs.created;
  if (rhs.F){
    F = fdopen( dup(fileno(rhs.F)), (created ? "w+b": "r+b"));
  }else{
    F = 0;
  }
  endPos = rhs.endPos;
  if (rhs.myPack){
    myPack = rhs.myPack;
  }
  metaStorage = rhs.metaStorage;
  metadata = metaStorage;
  currtime = rhs.currtime;
  lastreadpos = rhs.lastreadpos;
  headerSize = rhs.headerSize;
  trackMapping = rhs.trackMapping;
  memcpy(buffer, rhs.buffer, 4);
  return *this;
}

DTSC::File::operator bool() const{
  return F;
}

/// Open a filename for DTSC reading/writing.
/// If create is true and file does not exist, attempt to create.
DTSC::File::File(std::string filename, bool create){
  buffer = malloc(4);
  if (create){
    F = fopen(filename.c_str(), "w+b");
    if(!F){
      DEBUG_MSG(DLVL_ERROR, "Could not create file %s: %s", filename.c_str(), strerror(errno));
      return;
    }
    //write an empty header
    fseek(F, 0, SEEK_SET);
    fwrite(DTSC::Magic_Header, 4, 1, F);
    memset(buffer, 0, 4);
    fwrite(buffer, 4, 1, F); //write 4 zero-bytes
    headerSize = 0;
  }else{
    F = fopen(filename.c_str(), "r+b");
  }
  created = create;
  if ( !F){
    DEBUG_MSG(DLVL_ERROR, "Could not open file %s", filename.c_str());
    return;
  }
  fseek(F, 0, SEEK_END);
  endPos = ftell(F);

  bool sepHeader = false;
  if (!create){
    fseek(F, 0, SEEK_SET);
    if (fread(buffer, 4, 1, F) != 1){
	  DEBUG_MSG(DLVL_ERROR, "Can't read file contents of %s", filename.c_str());
      fclose(F);
      F = 0;
      return;
    }
    if (memcmp(buffer, DTSC::Magic_Header, 4) != 0){
      if (memcmp(buffer, DTSC::Magic_Packet2, 4) != 0){
        File Fhead(filename + ".dtsh");
        if (Fhead){
          metaStorage = Fhead.metaStorage;
          metadata = metaStorage;
          sepHeader = true;
        }else{
  	      DEBUG_MSG(DLVL_ERROR, "%s is not a valid DTSC file", filename.c_str());
          fclose(F);
          F = 0;
          return;
        }
      }else{
        metadata.moreheader = -1;
      }
    }
  }
  //we now know the first 4 bytes are DTSC::Magic_Header and we have a valid file
  fseek(F, 4, SEEK_SET);
  if (fread(buffer, 4, 1, F) != 1){
    fseek(F, 4, SEEK_SET);
    memset(buffer, 0, 4);
    fwrite(buffer, 4, 1, F); //write 4 zero-bytes
  }else{
    headerSize = ntohl(((uint32_t *)buffer)[0]);
  }
  if (metadata.moreheader != -1){
    if (!sepHeader){
      readHeader(0);
      fseek(F, 8 + headerSize, SEEK_SET);
    }else{
      fseek(F, 0, SEEK_SET);
    }
  }else{
    fseek(F, 0, SEEK_SET);
    File Fhead(filename + ".dtsh");
    if (Fhead){
      metaStorage = Fhead.metaStorage;
      metadata = metaStorage;
    }
  }
  currframe = 0;
}

/// Returns the header metadata for this file as JSON::Value.
DTSC::readOnlyMeta & DTSC::File::getMeta(){
  return metadata;
}

/// (Re)writes the given string to the header area if the size is the same as the existing header.
/// Forces a write if force is set to true.
bool DTSC::File::writeHeader(std::string & header, bool force){
  if (headerSize != header.size() && !force){
    DEBUG_MSG(DLVL_ERROR, "Could not overwrite header - not equal size");
    return false;
  }
  headerSize = header.size();
  int pSize = htonl(header.size());
  fseek(F, 4, SEEK_SET);
  int tmpret = fwrite((void*)( &pSize), 4, 1, F);
  if (tmpret != 1){
    return false;
  }
  fseek(F, 8, SEEK_SET);
  int ret = fwrite(header.c_str(), headerSize, 1, F);
  fseek(F, 8 + headerSize, SEEK_SET);
  return (ret == 1);
}

/// Adds the given string as a new header to the end of the file.
/// \returns The positon the header was written at, or 0 on failure.
long long int DTSC::File::addHeader(std::string & header){
  fseek(F, 0, SEEK_END);
  long long int writePos = ftell(F);
  int hSize = htonl(header.size());
  int ret = fwrite(DTSC::Magic_Header, 4, 1, F); //write header
  if (ret != 1){
    return 0;
  }
  ret = fwrite((void*)( &hSize), 4, 1, F); //write size
  if (ret != 1){
    return 0;
  }
  ret = fwrite(header.c_str(), header.size(), 1, F); //write contents
  if (ret != 1){
    return 0;
  }
  fseek(F, 0, SEEK_END);
  endPos = ftell(F);
  return writePos; //return position written at
}

/// Reads the header at the given file position.
/// If the packet could not be read for any reason, the reason is printed.
/// Reading the header means the file position is moved to after the header.
void DTSC::File::readHeader(int pos){
  fseek(F, pos, SEEK_SET);
  if (fread(buffer, 4, 1, F) != 1){
    if (feof(F)){
      DEBUG_MSG(DLVL_DEVEL, "End of file reached while reading header @ %d", pos);
    }else{
      DEBUG_MSG(DLVL_ERROR, "Could not read header @ %d", pos);
    }
    metadata = readOnlyMeta();
    return;
  }
  if (memcmp(buffer, DTSC::Magic_Header, 4) != 0){
    DEBUG_MSG(DLVL_ERROR, "Invalid header - %.4s != %.4s  @ %i", (char*)buffer, DTSC::Magic_Header, pos);
    metadata = readOnlyMeta();
    return;
  }
  if (fread(buffer, 4, 1, F) != 1){
    DEBUG_MSG(DLVL_ERROR, "Could not read header size @ %i", pos);
    metadata = readOnlyMeta();
    return;
  }
  long packSize = ntohl(((unsigned long*)buffer)[0]);
  std::string strBuffer;
  strBuffer.resize(packSize);
  if (packSize){
    if (fread((void*)strBuffer.c_str(), packSize, 1, F) != 1){
      DEBUG_MSG(DLVL_ERROR, "Could not read header packet @ %i", pos);
      metadata = readOnlyMeta();
      return;
    }
    JSON::fromDTMI(strBuffer, metaStorage);
    metadata = readOnlyMeta(metaStorage);//make readonly
  }
  //if there is another header, read it and replace metadata with that one.
  if (metadata.moreheader){
    if (metadata.moreheader < getBytePosEOF()){
      readHeader(metadata.moreheader);
      return;
    }
  }
  metadata.vod = true;
  metadata.live = false;
}

long int DTSC::File::getBytePosEOF(){
  return endPos;
}

long int DTSC::File::getBytePos(){
  return ftell(F);
}

bool DTSC::File::reachedEOF(){
  return feof(F);
}

/// Reads the packet available at the current file position.
/// If the packet could not be read for any reason, the reason is printed.
/// Reading the packet means the file position is increased to the next packet.
void DTSC::File::seekNext(){
  if ( !currentPositions.size()){
    DEBUG_MSG(DLVL_WARN, "No seek positions set - returning empty packet.");
    myPack.null();
    return;
  }
  fseek(F,currentPositions.begin()->bytePos, SEEK_SET);
  if ( reachedEOF()){
    myPack.null();
    return;
  }
  clearerr(F);
  if ( !metadata.merged){
    seek_time(currentPositions.begin()->seekTime + 1, currentPositions.begin()->trackID);
    fseek(F,currentPositions.begin()->bytePos, SEEK_SET);
  }
  currentPositions.erase(currentPositions.begin());
  lastreadpos = ftell(F);
  if (fread(buffer, 4, 1, F) != 1){
    if (feof(F)){
      DEBUG_MSG(DLVL_DEVEL, "End of file reached while seeking @ %i", (int)lastreadpos);
    }else{
      DEBUG_MSG(DLVL_ERROR, "Could not seek to next @ %i", (int)lastreadpos);
    }
    myPack.null();
    return;
  }
  if (memcmp(buffer, DTSC::Magic_Header, 4) == 0){
    seek_time(myPack.getTime() + 1, myPack.getTrackId(), true);
    return seekNext();
  }
  long long unsigned int version = 0;
  if (memcmp(buffer, DTSC::Magic_Packet, 4) == 0){
    version = 1;
  }
  if (memcmp(buffer, DTSC::Magic_Packet2, 4) == 0){
    version = 2;
  }
  if (version == 0){
    DEBUG_MSG(DLVL_ERROR, "Invalid packet header @ %#x - %.4s != %.4s @ %d", (unsigned int)lastreadpos, (char*)buffer, DTSC::Magic_Packet2, (int)lastreadpos);
    myPack.null();
    return;
  }
  if (fread(buffer, 4, 1, F) != 1){
    DEBUG_MSG(DLVL_ERROR, "Could not read packet size @ %d", (int)lastreadpos);
    myPack.null();
    return;
  }
  long packSize = ntohl(((unsigned long*)buffer)[0]);
  char * packBuffer = (char*)malloc(packSize+8);
  if (version == 1){
    memcpy(packBuffer, "DTPD", 4);
  }else{
    memcpy(packBuffer, "DTP2", 4);
  }
  memcpy(packBuffer+4, buffer, 4);
  if (fread((void*)(packBuffer + 8), packSize, 1, F) != 1){
    DEBUG_MSG(DLVL_ERROR, "Could not read packet @ %d", (int)lastreadpos);
    myPack.null();
    free(packBuffer);
    return;
  }
  myPack.reInit(packBuffer, packSize+8);
  free(packBuffer);
  if ( metadata.merged){
    int tempLoc = getBytePos();
    char newHeader[20];
    bool insert = false;
    seekPos tmpPos;
    if (fread((void*)newHeader, 20, 1, F) == 1){
      if (memcmp(newHeader, DTSC::Magic_Packet2, 4) == 0){
        tmpPos.bytePos = tempLoc;
        tmpPos.trackID = ntohl(((int*)newHeader)[2]);
        tmpPos.seekTime = 0;
        if (selectedTracks.find(tmpPos.trackID) != selectedTracks.end()){
          tmpPos.seekTime = ((long long unsigned int)ntohl(((int*)newHeader)[3])) << 32;
          tmpPos.seekTime += ntohl(((int*)newHeader)[4]);
          insert = true;
        }else{
          long tid = myPack.getTrackId();
          for (unsigned int i = 0; i != metadata.tracks[tid].keyLen; i++){
            if ((unsigned long long)metadata.tracks[tid].keys[i].getTime() > myPack.getTime()){
              tmpPos.seekTime = metadata.tracks[tid].keys[i].getTime();
              tmpPos.bytePos = metadata.tracks[tid].keys[i].getBpos();
              tmpPos.trackID = tid;
              insert = true;
              break;
            }
          }
        }
        if (currentPositions.size()){
          for (std::set<seekPos>::iterator curPosIter = currentPositions.begin(); curPosIter != currentPositions.end(); curPosIter++){
            if ((*curPosIter).trackID == tmpPos.trackID && (*curPosIter).seekTime >= tmpPos.seekTime){
              insert = false;
              break;
            }
          }
        }
      }
    }
    if (insert){
      currentPositions.insert(tmpPos);
    }else{
      seek_time(myPack.getTime() + 1, myPack.getTrackId(), true);
    }
    seek_bpos(tempLoc);
  }
}


void DTSC::File::parseNext(){
  lastreadpos = ftell(F);
  if (fread(buffer, 4, 1, F) != 1){
    if (feof(F)){
      DEBUG_MSG(DLVL_DEVEL, "End of file reached @ %d", (int)lastreadpos);
    }else{
      DEBUG_MSG(DLVL_ERROR, "Could not read header @ %d", (int)lastreadpos);
    }
    myPack.null();
    return;
  }
  if (memcmp(buffer, DTSC::Magic_Header, 4) == 0){
    if (lastreadpos != 0){
      readHeader(lastreadpos);
      std::string tmp = metaStorage.toNetPacked();
      myPack.reInit(tmp.data(), tmp.size());
      DEBUG_MSG(DLVL_DEVEL,"Read another header");
    }else{
      if (fread(buffer, 4, 1, F) != 1){
        DEBUG_MSG(DLVL_ERROR, "Could not read header size @ %d", (int)lastreadpos);
        myPack.null();
        return;
      }
      long packSize = ntohl(((unsigned long*)buffer)[0]);
      std::string strBuffer = "DTSC";
      strBuffer.append((char*)buffer, 4);
      strBuffer.resize(packSize + 8);
      if (fread((void*)(strBuffer.c_str() + 8), packSize, 1, F) != 1){
        DEBUG_MSG(DLVL_ERROR, "Could not read header @ %d", (int)lastreadpos);
        myPack.null();
        return;
      }
      myPack.reInit(strBuffer.data(), strBuffer.size());
    }
    return;
  }
  long long unsigned int version = 0;
  if (memcmp(buffer, DTSC::Magic_Packet, 4) == 0){
    version = 1;
  }
  if (memcmp(buffer, DTSC::Magic_Packet2, 4) == 0){
    version = 2;
  }
  if (version == 0){
    DEBUG_MSG(DLVL_ERROR, "Invalid packet header @ %#x - %.4s != %.4s @ %d", (unsigned int)lastreadpos, (char*)buffer, DTSC::Magic_Packet2, (int)lastreadpos);
    myPack.null();
    return;
  }
  if (fread(buffer, 4, 1, F) != 1){
    DEBUG_MSG(DLVL_ERROR, "Could not read packet size @ %d", (int)lastreadpos);
    myPack.null();
    return;
  }
  long packSize = ntohl(((unsigned long*)buffer)[0]);
  char * packBuffer = (char*)malloc(packSize+8);
  if (version == 1){
    memcpy(packBuffer, "DTPD", 4);
  }else{
    memcpy(packBuffer, "DTP2", 4);
  }
  memcpy(packBuffer+4, buffer, 4);
  if (fread((void*)(packBuffer + 8), packSize, 1, F) != 1){
    DEBUG_MSG(DLVL_ERROR, "Could not read packet @ %d", (int)lastreadpos);
    myPack.null();
    free(packBuffer);
    return;
  }
  myPack.reInit(packBuffer, packSize+8);
  free(packBuffer);
}

/// Returns the byte positon of the start of the last packet that was read.
long long int DTSC::File::getLastReadPos(){
  return lastreadpos;
}

/// Returns the internal buffer of the last read packet in raw binary format.
DTSC::Packet & DTSC::File::getPacket(){
  return myPack;
}

bool DTSC::File::seek_time(unsigned int ms, int trackNo, bool forceSeek){
  seekPos tmpPos;
  tmpPos.trackID = trackNo;
  if (!forceSeek && myPack && ms > myPack.getTime() && trackNo >= myPack.getTrackId()){
    tmpPos.seekTime = myPack.getTime();
    tmpPos.bytePos = getBytePos();
  }else{
    tmpPos.seekTime = 0;
    tmpPos.bytePos = 0;
  }
  if (reachedEOF()){
    clearerr(F);
    seek_bpos(0);
    tmpPos.bytePos = 0;
    tmpPos.seekTime = 0;
  }
  DTSC::readOnlyTrack & trackRef = metadata.tracks[trackNo];
  for (unsigned int i = 0; i < trackRef.keyLen; i++){
    long keyTime = trackRef.keys[i].getTime();
    if (keyTime > ms){
      break;
    }
    if ((long long unsigned int)keyTime > tmpPos.seekTime){
      tmpPos.seekTime = keyTime;
      tmpPos.bytePos = trackRef.keys[i].getBpos();
    }
  }
  bool foundPacket = false;
  while ( !foundPacket){
    lastreadpos = ftell(F);
    if (reachedEOF()){
      DEBUG_MSG(DLVL_WARN, "Reached EOF during seek to %u in track %d - aborting @ %lld", ms, trackNo, lastreadpos);
      return false;
    }
    //Seek to first packet after ms.
    seek_bpos(tmpPos.bytePos);
    //read the header
    char header[20];
    fread((void*)header, 20, 1, F);
    //check if packetID matches, if not, skip size + 8 bytes.
    int packSize = ntohl(((int*)header)[1]);
    int packID = ntohl(((int*)header)[2]);
    if (memcmp(header,Magic_Packet2,4) != 0 || packID != trackNo){
      if (memcmp(header,"DT",2) != 0){
        DEBUG_MSG(DLVL_WARN, "Invalid header during seek to %u in track %d @ %lld - resetting bytePos from %lld to zero", ms, trackNo, lastreadpos, tmpPos.bytePos);
        tmpPos.bytePos = 0;
        continue;
      }
      tmpPos.bytePos += 8 + packSize;
      continue;
    }
    //get timestamp of packet, if too large, break, if not, skip size bytes.
    long long unsigned int myTime = ((long long unsigned int)ntohl(((int*)header)[3]) << 32);
    myTime += ntohl(((int*)header)[4]);
    tmpPos.seekTime = myTime;
    if (myTime >= ms){
      foundPacket = true;
    }else{
      tmpPos.bytePos += 8 + packSize;
      continue;
    }
  }
  DEBUG_MSG(DLVL_HIGH, "Seek to %d:%d resulted in %lli", trackNo, ms, tmpPos.seekTime);
  currentPositions.insert(tmpPos);
  return true;
}

/// Attempts to seek to the given time in ms within the file.
/// Returns true if successful, false otherwise.
bool DTSC::File::seek_time(unsigned int ms){
  currentPositions.clear();
  if (selectedTracks.size()){
    for (std::set<int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      seek_time(ms,(*it));
    }
  }
  return true;
}

bool DTSC::File::seek_bpos(int bpos){
  if (fseek(F, bpos, SEEK_SET) == 0){
    return true;
  }
  return false;
}

void DTSC::File::rewritePacket(std::string & newPacket, int bytePos){
  fseek(F, bytePos, SEEK_SET);
  fwrite(newPacket.c_str(), newPacket.size(), 1, F);
  fseek(F, 0, SEEK_END);
  if (ftell(F) > endPos){
    endPos = ftell(F);
  }
}

void DTSC::File::writePacket(std::string & newPacket){
  fseek(F, 0, SEEK_END);
  fwrite(newPacket.c_str(), newPacket.size(), 1, F); //write contents
  fseek(F, 0, SEEK_END);
  endPos = ftell(F);
}

void DTSC::File::writePacket(JSON::Value & newPacket){
  writePacket(newPacket.toNetPacked());
}

bool DTSC::File::atKeyframe(){
  if (myPack.getFlag("keyframe")){
    return true;
  }
  long long int bTime = myPack.getTime();
  DTSC::readOnlyTrack & trackRef = metadata.tracks[myPack.getTrackId()];
  for (unsigned int i = 0; i < trackRef.keyLen; i++){
    if (trackRef.keys[i].getTime() >= bTime){
      return (trackRef.keys[i].getTime() == bTime);
    }
  }
  return false;
}

void DTSC::File::selectTracks(std::set<int> & tracks){
  selectedTracks = tracks;
  currentPositions.clear();
  seek_time(0);
}

/// Close the file if open
DTSC::File::~File(){
  if (F){
    fclose(F);
    F = 0;
  }
  free(buffer);
}


bool DTSC::isFixed(JSON::Value & metadata){
  if (metadata.isMember("is_fixed")){return true;}
  if ( !metadata.isMember("tracks")){return false;}
  for (JSON::ObjIter it = metadata["tracks"].ObjBegin(); it != metadata["tracks"].ObjEnd(); it++){
    if (it->second["type"].asString() == "meta"){
      continue;
    }
    if (!it->second["keys"].isString()){
      return false;
    }
    //Check for bpos: last element bpos != 0
    std::string keyRef = it->second["keys"].asStringRef();
    if (keyRef.size() < 16){
      return false;
    }
    int offset = keyRef.size() - 17;
    if (!(keyRef[offset] | keyRef[offset+1] | keyRef[offset+2] | keyRef[offset+3] | keyRef[offset+4])){
      return false;
    }
  }
  return true;
}
