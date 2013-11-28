/// \file dtsc.cpp
/// Holds all code for DDVTECH Stream Container parsing/generation.

#include "dtsc.h"
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
      JSON::Value meta = JSON::fromDTMI((unsigned char*)buffer.c_str() + 8, len, i);
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
        newPack = JSON::fromDTMI((unsigned char*)buffer.c_str() + 8, len, i);
      }
      if (version == 2){
        newPack = JSON::fromDTMI2((unsigned char*)buffer.c_str() + 8, len, i);
      }
      buffer.erase(0, len + 8);
      addPacket(newPack);
      syncing = false;
      return true;
    }
#if DEBUG >= 2
    if (!syncing){
      std::cerr << "Error: Invalid DTMI data detected - re-syncing" << std::endl;
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
      JSON::Value meta = JSON::fromDTMI((unsigned char*)wholepacket.c_str() + 8, len, i);
      metadata = Meta(meta);
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
        newPack = JSON::fromDTMI((unsigned char*)wholepacket.c_str() + 8, len, i);
      }
      if (version == 2){
        newPack = JSON::fromDTMI2((unsigned char*)wholepacket.c_str() + 8, len, i);
      }
      addPacket(newPack);
      syncing = false;
      return true;
    }
#if DEBUG >= 2
    if (!syncing){
      std::cerr << "Error: Invalid DTMI data detected - syncing" << std::endl;
      syncing = true;
    }
#endif
    buffer.get().clear();
  }
  return false;
}

/// Adds a keyframe packet to all tracks, so the stream can be fully played.
void DTSC::Stream::endStream(){
  for (std::map<int,Track>::iterator it = metadata.tracks.begin(); it != metadata.tracks.end(); it++){
    JSON::Value newPack;
    newPack["time"] = it->second.lastms;
    newPack["trackid"] = it->second.trackID;
    newPack["keyframe"] = 1ll;
    newPack["data"] = "";
    addPacket(newPack);
  }
}

/// Blocks until either the stream has metadata available or the sourceSocket errors.
/// This function is intended to be run before any commands are sent and thus will not throw away anything important.
void DTSC::Stream::waitForMeta(Socket::Connection & sourceSocket){
  while ( !metadata && sourceSocket.connected()){
    //we have data? attempt to read header
    if (sourceSocket.Received().size()){
      //return value is ignore because we're not interested in data packets, just metadata.
      parsePacket(sourceSocket.Received());
    }
    //still no header? check for more data
    if ( !metadata){
      if (sourceSocket.spool()){
        //more received? attempt to read
        //return value is ignore because we're not interested in data packets, just metadata.
        parsePacket(sourceSocket.Received());
      }else{
        //nothing extra to receive? wait a bit and retry
        Util::sleep(5);
      }
    }
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

void DTSC::Stream::addPacket(JSON::Value & newPack){
  long long unsigned int now = Util::getMS();
  livePos newPos;
  newPos.trackID = newPack["trackid"].asInt();
  newPos.seekTime = newPack["time"].asInt();
  if (buffercount > 1 && buffers.size() > 0){
    livePos lastPos = buffers.rbegin()->first;
    if (newPos < lastPos){
      if ((lastPos.seekTime > 1000) && newPos.seekTime < lastPos.seekTime - 1000){
        resetStream();
      }else{
        newPos.seekTime = lastPos.seekTime+1;
      }
    }
  }else{
    resetStream();
  }
  std::string newTrack = metadata.tracks[newPos.trackID].getIdentifier();
  while (buffers.count(newPos) > 0){
    newPos.seekTime++;
  }
  buffers[newPos] = newPack;
  if (buffercount > 1){
    buffers[newPos].toNetPacked();//make sure package is packed and ready
  }
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
    metadata.live = true;
  }
  
  //increase buffer size if too little time available
  unsigned int timeBuffered = buffers.rbegin()->second["time"].asInt() - buffers.begin()->second["time"].asInt();
  if (buffercount > 1){
    if (timeBuffered < buffertime){
      buffercount = buffers.size();
      if (buffercount < 2){buffercount = 2;}
    }
    if (metadata.bufferWindow < timeBuffered){
      metadata.bufferWindow = timeBuffered;
    }
  }

  while (buffers.size() > buffercount){
    cutOneBuffer();
  }
}

/// Deletes a the first part of the buffer, updating the keyframes list and metadata as required.
/// Will print a warning to std::cerr if a track has less than 2 keyframes left because of this.
void DTSC::Stream::cutOneBuffer(){
  int trid = buffers.begin()->first.trackID;
  if (buffercount > 1 && keyframes[trid].count(buffers.begin()->first)){
    //if there are < 3 keyframes, throwing one away would mean less than 2 left.
    if (keyframes[trid].size() < 3){
      std::cerr << "Warning - track " << trid << " doesn't have enough keyframes to be reliably served." << std::endl;
    }
    keyframes[trid].erase(buffers.begin()->first);
    int keySize = metadata.tracks[trid].keys.size();
    for (int i = 0; i < metadata.tracks[trid].keys[0].getParts(); i++){
      metadata.tracks[trid].parts.pop_front();
    }
    metadata.tracks[trid].keys.pop_front();
    // delete fragments of which the beginning can no longer be reached
    while (metadata.tracks[trid].fragments.size() && metadata.tracks[trid].fragments[0].getNumber() < metadata.tracks[trid].keys[0].getNumber()){
      metadata.tracks[trid].firstms += metadata.tracks[trid].fragments[0].getDuration();
      metadata.tracks[trid].fragments.pop_front();
      // increase the missed fragments counter
      metadata.tracks[trid].missedFrags ++;
    }
  }
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
  endPos = 0;
}

DTSC::File::File(const File & rhs){
  *this = rhs;
}

DTSC::File & DTSC::File::operator =(const File & rhs){
  created = rhs.created;
  if (rhs.F){
    int tmpFd = fileno(rhs.F);
    int newFd = dup(tmpFd);
    F = fdopen( newFd, (created ? "w+b": "r+b"));
  }else{
    F = 0;
  }
  endPos = rhs.endPos;
  strbuffer = rhs.strbuffer;
  jsonbuffer = rhs.jsonbuffer;
  metadata = rhs.metadata;
  currtime = rhs.currtime;
  lastreadpos = rhs.lastreadpos;
  headerSize = rhs.headerSize;
  trackMapping = rhs.trackMapping;
  memcpy(buffer, rhs.buffer, 4);
}

DTSC::File::operator bool() const{
  return F;
}

/// Open a filename for DTSC reading/writing.
/// If create is true and file does not exist, attempt to create.
DTSC::File::File(std::string filename, bool create){
  if (create){
    F = fopen(filename.c_str(), "w+b");
    if(!F){
      std::cerr << "Could not create file" << filename << ": " << strerror(errno) << std::endl;
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
    fprintf(stderr, "Could not open file %s\n", filename.c_str());
    return;
  }
  fseek(F, 0, SEEK_END);
  endPos = ftell(F);

  //we now know the first 4 bytes are DTSC::Magic_Header and we have a valid file
  fseek(F, 4, SEEK_SET);
  if (fread(buffer, 4, 1, F) != 1){
    fseek(F, 4, SEEK_SET);
    memset(buffer, 0, 4);
    fwrite(buffer, 4, 1, F); //write 4 zero-bytes
  }else{
    uint32_t * ubuffer = (uint32_t *)buffer;
    headerSize = ntohl(ubuffer[0]);
  }
  readHeader(0);
  fseek(F, 8 + headerSize, SEEK_SET);
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
    fprintf(stderr, "Could not overwrite header - not equal size\n");
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
/// If the packet could not be read for any reason, the reason is printed to stderr.
/// Reading the header means the file position is moved to after the header.
void DTSC::File::readHeader(int pos){
  fseek(F, pos, SEEK_SET);
  if (fread(buffer, 4, 1, F) != 1){
    if (feof(F)){
#if DEBUG >= 4
      fprintf(stderr, "End of file reached (H%i)\n", pos);
#endif
    }else{
      fprintf(stderr, "Could not read header (H%i)\n", pos);
    }
    strbuffer = "";
    metadata = readOnlyMeta();
    return;
  }
  if (memcmp(buffer, DTSC::Magic_Header, 4) != 0){
    fprintf(stderr, "Invalid header - %.4s != %.4s  (H%i)\n", buffer, DTSC::Magic_Header, pos);
    strbuffer = "";
    metadata = readOnlyMeta();
    return;
  }
  if (fread(buffer, 4, 1, F) != 1){
    fprintf(stderr, "Could not read size (H%i)\n", pos);
    strbuffer = "";
    metadata = readOnlyMeta();
    return;
  }
  uint32_t * ubuffer = (uint32_t *)buffer;
  long packSize = ntohl(ubuffer[0]);
  strbuffer.resize(packSize);
  if (packSize){
    if (fread((void*)strbuffer.c_str(), packSize, 1, F) != 1){
      fprintf(stderr, "Could not read packet (H%i)\n", pos);
      strbuffer = "";
      metadata = readOnlyMeta();
      return;
    }
    metaStorage = JSON::fromDTMI(strbuffer);
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
/// If the packet could not be read for any reason, the reason is printed to stderr.
/// Reading the packet means the file position is increased to the next packet.
void DTSC::File::seekNext(){
  if ( !currentPositions.size()){
    strbuffer = "";
    jsonbuffer.null();
    return;
  }
  fseek(F,currentPositions.begin()->bytePos, SEEK_SET);
  if ( reachedEOF()){
    strbuffer = "";
    jsonbuffer.null();
    return;
  }
  clearerr(F);
  if ( !metadata.merged){
    seek_time(currentPositions.begin()->seekTime + 1, currentPositions.begin()->trackID);
  }
  fseek(F,currentPositions.begin()->bytePos, SEEK_SET);
  currentPositions.erase(currentPositions.begin());
  lastreadpos = ftell(F);
  if (fread(buffer, 4, 1, F) != 1){
    if (feof(F)){
#if DEBUG >= 4
      fprintf(stderr, "End of file reached.\n");
#endif
    }else{
      fprintf(stderr, "Could not read header\n");
    }
    strbuffer = "";
    jsonbuffer.null();
    return;
  }
  if (memcmp(buffer, DTSC::Magic_Header, 4) == 0){
    readHeader(lastreadpos);
    jsonbuffer = metadata.toJSON();
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
    fprintf(stderr, "Invalid packet header @ %#x - %.4s != %.4s\n", lastreadpos, buffer, DTSC::Magic_Packet2);
    strbuffer = "";
    jsonbuffer.null();
    return;
  }
  if (fread(buffer, 4, 1, F) != 1){
    fprintf(stderr, "Could not read size\n");
    strbuffer = "";
    jsonbuffer.null();
    return;
  }
  uint32_t * ubuffer = (uint32_t *)buffer;
  long packSize = ntohl(ubuffer[0]);
  strbuffer.resize(packSize);
  if (fread((void*)strbuffer.c_str(), packSize, 1, F) != 1){
    fprintf(stderr, "Could not read packet\n");
    strbuffer = "";
    jsonbuffer.null();
    return;
  }
  if (version == 2){
    jsonbuffer = JSON::fromDTMI2(strbuffer);
  }else{
    jsonbuffer = JSON::fromDTMI(strbuffer);
  }
  if ( metadata.merged){
    int tempLoc = getBytePos();
    char newHeader[20];
    if (fread((void*)newHeader, 20, 1, F) == 1){
      if (memcmp(newHeader, DTSC::Magic_Packet2, 4) == 0){
        seekPos tmpPos;
        tmpPos.bytePos = tempLoc;
        tmpPos.trackID = ntohl(((int*)newHeader)[2]);
        if (selectedTracks.find(tmpPos.trackID) != selectedTracks.end()){
          tmpPos.seekTime = ((long long unsigned int)ntohl(((int*)newHeader)[3])) << 32;
          tmpPos.seekTime += ntohl(((int*)newHeader)[4]);
        }else{
          tmpPos.seekTime = -1;
          long tid = jsonbuffer["trackid"].asInt();
          for (int i = 0; i != metadata.tracks[tid].keyLen; i++){
            if (metadata.tracks[tid].keys[i].getTime() > jsonbuffer["time"].asInt()){
              tmpPos.seekTime = metadata.tracks[tid].keys[i].getTime();
              tmpPos.bytePos = metadata.tracks[tid].keys[i].getBpos();
              tmpPos.trackID = tid;
              break;
            }
          }
        }
        if (tmpPos.seekTime != -1){
          bool insert = true;
          for (std::set<seekPos>::iterator curPosIter = currentPositions.begin(); curPosIter != currentPositions.end(); curPosIter++){
            if ((*curPosIter).trackID == tmpPos.trackID && (*curPosIter).seekTime >= tmpPos.seekTime){
              insert = false;
              break;
            }
          }
          if (insert){
            currentPositions.insert(tmpPos);
          }else{
            seek_time(jsonbuffer["time"].asInt() + 1, jsonbuffer["trackid"].asInt(), true);
          }
        }
      }
    }
  }
}


void DTSC::File::parseNext(){
  lastreadpos = ftell(F);
  if (fread(buffer, 4, 1, F) != 1){
    if (feof(F)){
#if DEBUG >= 4
      fprintf(stderr, "End of file reached.\n");
#endif
    }else{
      fprintf(stderr, "Could not read header\n");
    }
    strbuffer = "";
    jsonbuffer.null();
    return;
  }
  if (memcmp(buffer, DTSC::Magic_Header, 4) == 0){
    if (lastreadpos != 0){
      readHeader(lastreadpos);
      jsonbuffer = metadata.toJSON();
    }else{
      if (fread(buffer, 4, 1, F) != 1){
        fprintf(stderr, "Could not read size\n");
        strbuffer = "";
        jsonbuffer.null();
        return;
      }
      uint32_t * ubuffer = (uint32_t *)buffer;
      long packSize = ntohl(ubuffer[0]);
      strbuffer.resize(packSize);
      if (fread((void*)strbuffer.c_str(), packSize, 1, F) != 1){
        fprintf(stderr, "Could not read packet\n");
        strbuffer = "";
        jsonbuffer.null();
        return;
      }
      jsonbuffer = JSON::fromDTMI(strbuffer);
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
    fprintf(stderr, "Invalid packet header @ %#x - %.4s != %.4s\n", lastreadpos, buffer, DTSC::Magic_Packet2);
    strbuffer = "";
    jsonbuffer.null();
    return;
  }
  if (fread(buffer, 4, 1, F) != 1){
    fprintf(stderr, "Could not read size\n");
    strbuffer = "";
    jsonbuffer.null();
    return;
  }
  uint32_t * ubuffer = (uint32_t *)buffer;
  long packSize = ntohl(ubuffer[0]);
  strbuffer.resize(packSize);
  if (fread((void*)strbuffer.c_str(), packSize, 1, F) != 1){
    fprintf(stderr, "Could not read packet\n");
    strbuffer = "";
    jsonbuffer.null();
    return;
  }
  if (version == 2){
    jsonbuffer = JSON::fromDTMI2(strbuffer);
  }else{
    jsonbuffer = JSON::fromDTMI(strbuffer);
  }
}

/// Returns the byte positon of the start of the last packet that was read.
long long int DTSC::File::getLastReadPos(){
  return lastreadpos;
}

/// Returns the internal buffer of the last read packet in raw binary format.
std::string & DTSC::File::getPacket(){
  return strbuffer;
}

/// Returns the internal buffer of the last read packet in JSON format.
JSON::Value & DTSC::File::getJSON(){
  return jsonbuffer;
}

bool DTSC::File::seek_time(int ms, int trackNo, bool forceSeek){
  seekPos tmpPos;
  tmpPos.trackID = trackNo;
  if (!forceSeek && jsonbuffer && ms > jsonbuffer["time"].asInt() && trackNo >= jsonbuffer["trackid"].asInt()){
    tmpPos.seekTime = jsonbuffer["time"].asInt();
    tmpPos.bytePos = getBytePos();
  }else{
    tmpPos.seekTime = 0;
    tmpPos.bytePos = 0;
  }
  for (int i = 0; i < metadata.tracks[trackNo].keyLen; i++){
    if (metadata.tracks[trackNo].keys[i].getTime() > ms){
      break;
    }
    if (metadata.tracks[trackNo].keys[i].getTime() > tmpPos.seekTime){
      tmpPos.seekTime = metadata.tracks[trackNo].keys[i].getTime();
      tmpPos.bytePos = metadata.tracks[trackNo].keys[i].getBpos();
    }
  }
  bool foundPacket = false;
  while ( !foundPacket){
    lastreadpos = ftell(F);
    if (reachedEOF()){
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
  currentPositions.insert(tmpPos);
}

/// Attempts to seek to the given time in ms within the file.
/// Returns true if successful, false otherwise.
bool DTSC::File::seek_time(int ms){
  currentPositions.clear();
  seekPos tmpPos;
  for (std::set<int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
    seek_bpos(0);
    seek_time(ms,(*it));
  }
  return true;
}

bool DTSC::File::seek_bpos(int bpos){
  if (fseek(F, bpos, SEEK_SET) == 0){
    return true;
  }
  return false;
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
  if (getJSON().isMember("keyframe")){
    return true;
  }
  long long int bTime = jsonbuffer["time"].asInt();
  int trackid = jsonbuffer["trackid"].asInt();
  for (int i = 0; i < metadata.tracks[trackid].keyLen; i++){
    if (metadata.tracks[trackid].keys[i].getTime() >= bTime){
      return (metadata.tracks[trackid].keys[i].getTime() == bTime);
    }
  }
  return false;
}

void DTSC::File::selectTracks(std::set<int> & tracks){
  selectedTracks = tracks;
  if ( !currentPositions.size()){
    seek_time(0);
  }else{
    currentPositions.clear();
  }
}

/// Close the file if open
DTSC::File::~File(){
  if (F){
    fclose(F);
    F = 0;
  }
}


bool DTSC::isFixed(JSON::Value & metadata){
  DTSC::Meta testFixed(metadata);
  return testFixed.isFixed();
}
