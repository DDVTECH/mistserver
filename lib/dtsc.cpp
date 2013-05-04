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
  datapointer = 0;
  buffercount = 1;
  buffertime = 0;
}

/// Initializes a DTSC::Stream with a minimum of rbuffers packet buffers.
/// The actual buffer count may not at all times be the requested amount.
DTSC::Stream::Stream(unsigned int rbuffers, unsigned int bufferTime){
  datapointertype = DTSC::INVALID;
  datapointer = 0;
  if (rbuffers < 1){
    rbuffers = 1;
  }
  buffercount = rbuffers;
  buffertime = bufferTime;
}

/// Returns the time in milliseconds of the last received packet.
/// This is _not_ the time this packet was received, only the stored time.
unsigned int DTSC::Stream::getTime(){
  return buffers.front()["time"].asInt();
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
      metadata = JSON::fromDTMI((unsigned char*)buffer.c_str() + 8, len, i);
      metadata.removeMember("moreheader");
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
      buffers.push_front(JSON::Value());
      unsigned int i = 0;
      if (version == 1){
        buffers.front() = JSON::fromDTMI((unsigned char*)buffer.c_str() + 8, len, i);
      }
      if (version == 2){
        buffers.front() = JSON::fromDTMI2(buffer.substr(8));
      }
      datapointertype = INVALID;
      if (buffers.front().isMember("data")){
        datapointer = &(buffers.front()["data"].strVal);
      }else{
        datapointer = 0;
      }
      if (buffers.front().isMember("datatype")){
        std::string tmp = buffers.front()["datatype"].asString();
        if (tmp == "video"){
          datapointertype = VIDEO;
        }
        if (tmp == "audio"){
          datapointertype = AUDIO;
        }
        if (tmp == "meta"){
          datapointertype = META;
        }
        if (tmp == "pause_marker"){
          datapointertype = PAUSEMARK;
        }
      }
      buffer.erase(0, len + 8);
      while (buffers.size() > buffercount){
        buffers.pop_back();
      }
      advanceRings();
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
      metadata = JSON::fromDTMI((unsigned char*)wholepacket.c_str() + 8, len, i);
      metadata.removeMember("moreheader");
      metadata.netPrepare();
      if ( !buffer.available(8)){
        return false;
      }
      header_bytes = buffer.copy(8);
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
      buffers.push_front(JSON::Value());
      unsigned int i = 0;
      std::string wholepacket = buffer.remove(len + 8);
      if (version == 1){
        buffers.front() = JSON::fromDTMI((unsigned char*)wholepacket.c_str() + 8, len, i);
      }
      if (version == 2){
        buffers.front() = JSON::fromDTMI2(wholepacket.substr(8));
      }
      datapointertype = INVALID;
      if (buffers.front().isMember("data")){
        datapointer = &(buffers.front()["data"].strVal);
      }else{
        datapointer = 0;
      }
      if (buffers.front().isMember("datatype")){
        std::string tmp = buffers.front()["datatype"].asString();
        if (tmp == "video"){
          datapointertype = VIDEO;
        }
        if (tmp == "audio"){
          datapointertype = AUDIO;
        }
        if (tmp == "meta"){
          datapointertype = META;
        }
        if (tmp == "pause_marker"){
          datapointertype = PAUSEMARK;
        }
      }
      while (buffers.size() > buffercount){
        buffers.pop_back();
      }
      advanceRings();
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

/// Returns a direct pointer to the data attribute of the last received packet, if available.
/// Returns NULL if no valid pointer or packet is available.
std::string & DTSC::Stream::lastData(){
  return *datapointer;
}

/// Returns the packet in this buffer number.
/// \arg num Buffer number.
JSON::Value & DTSC::Stream::getPacket(unsigned int num){
  static JSON::Value empty;
  if (num >= buffers.size()){
    return empty;
  }
  return buffers[num];
}

/// Returns the type of the last received packet.
DTSC::datatype DTSC::Stream::lastType(){
  return datapointertype;
}

/// Returns true if the current stream contains at least one video track.
bool DTSC::Stream::hasVideo(){
  return metadata.isMember("video");
}

/// Returns true if the current stream contains at least one audio track.
bool DTSC::Stream::hasAudio(){
  return metadata.isMember("audio");
}

void DTSC::Stream::setBufferTime(unsigned int ms){
  buffertime = ms;
}

/// Returns a packed DTSC packet, ready to sent over the network.
std::string & DTSC::Stream::outPacket(unsigned int num){
  static std::string emptystring;
  if (num >= buffers.size() || !buffers[num].isObject()) return emptystring;
  return buffers[num].toNetPacked();
}

/// Returns a packed DTSC header, ready to sent over the network.
std::string & DTSC::Stream::outHeader(){
  return metadata.toNetPacked();
}

/// advances all given out and internal Ring classes to point to the new buffer, after one has been added.
/// Also updates the internal keyframes ring, as well as marking rings as starved if they are.
/// Unsets waiting rings, updating them with their new buffer number.
void DTSC::Stream::advanceRings(){
  std::deque<DTSC::Ring>::iterator dit;
  std::set<DTSC::Ring *>::iterator sit;
  if (rings.size()){
    for (sit = rings.begin(); sit != rings.end(); sit++){
      ( *sit)->b++;
      if (( *sit)->waiting){
        ( *sit)->waiting = false;
        ( *sit)->b = 0;
      }
      if (( *sit)->starved || (( *sit)->b >= buffers.size())){
        ( *sit)->starved = true;
        ( *sit)->b = 0;
      }
    }
  }
  if (keyframes.size()){
    for (dit = keyframes.begin(); dit != keyframes.end(); dit++){
      dit->b++;
    }
    bool repeat;
    do{
      repeat = false;
      for (dit = keyframes.begin(); dit != keyframes.end(); dit++){
        if (dit->b >= buffers.size()){
          keyframes.erase(dit);
          repeat = true;
          break;
        }
      }
    }while (repeat);
  }
  static int fragNum = 1;
  static unsigned int lastkeytime = 4242;
  if ((lastType() == VIDEO && buffers.front().isMember("keyframe")) || (!metadata.isMember("video") && buffers.front()["time"].asInt() / 2000 != lastkeytime)){
    keyframes.push_front(DTSC::Ring(0));
    if ( !buffers.front().isMember("fragnum")){
      buffers.front()["fragnum"] = fragNum++;
    }
    lastkeytime = buffers.front()["time"].asInt() / 2000;
  }
  unsigned int timeBuffered = 0;
  if (keyframes.size() > 1){
    //increase buffer size if no keyframes available or too little time available
    timeBuffered = buffers[keyframes[0].b]["time"].asInt() - buffers[keyframes[keyframes.size() - 1].b]["time"].asInt();
  }
  if (buffercount > 1 && (keyframes.size() < 2 || timeBuffered < buffertime)){
    buffercount++;
  }
}

/// Constructs a new Ring, at the given buffer position.
/// \arg v Position for buffer.
DTSC::Ring::Ring(unsigned int v){
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
  DTSC::Ring * tmp;
  if (keyframes.size() == 0){
    tmp = new DTSC::Ring(0);
  }else{
    tmp = new DTSC::Ring(keyframes[0].b);
  }
  rings.insert(tmp);
  return tmp;
}

/// Deletes a given out Ring class from memory and internal Ring list.
/// Checks for NULL pointers and invalid pointers, silently discarding them.
void DTSC::Stream::dropRing(DTSC::Ring * ptr){
  if (rings.find(ptr) != rings.end()){
    rings.erase(ptr);
    delete ptr;
  }
}

/// Updates the headers for a live stream, keeping track of all available
/// keyframes and their media times. The function MAY NOT be run at any other
/// time than right after receiving a new keyframe, or there'll be raptors.
void DTSC::Stream::updateHeaders(){
  if (keyframes.size() > 2){
    if (buffers[keyframes[0].b]["time"].asInt() < buffers[keyframes[keyframes.size() - 1].b]["time"].asInt()){
      std::cerr << "Detected new video - resetting all buffers and metadata - hold on, this ride might get bumpy!" << std::endl;
      keyframes.clear();
      buffers.clear();
      std::set<DTSC::Ring *>::iterator sit;
      if (rings.size()){
        for (sit = rings.begin(); sit != rings.end(); sit++){
          ( *sit)->updated = true;
          ( *sit)->b = 0;
          ( *sit)->starved = true;
        }
      }
      metadata.removeMember("keytime");
      metadata.removeMember("keynum");
      metadata.removeMember("keylen");
      metadata.removeMember("frags");
      metadata.removeMember("lastms");
      metadata.removeMember("missed_frags");
      metadata.netPrepare();
      return;
    }
    metadata["keytime"].shrink(keyframes.size() - 2);
    metadata["keynum"].shrink(keyframes.size() - 2);
    metadata["keylen"].shrink(keyframes.size() - 2);
    metadata["keytime"].append(buffers[keyframes[1].b]["time"].asInt());
    metadata["keynum"].append(buffers[keyframes[1].b]["fragnum"].asInt());
    metadata["keylen"].append(buffers[keyframes[0].b]["time"].asInt() - buffers[keyframes[1].b]["time"].asInt());
    unsigned int fragStart = 0;
    if ( !metadata["frags"]){
      // this means that if we have < ~10 seconds in the buffer, fragmenting goes horribly wrong.
      if ( !metadata.isMember("missed_frags")){
        metadata["missed_frags"] = 0ll;
      }
    }else{
      // delete fragments of which the beginning can no longer be reached
      while (metadata["frags"][0u]["num"].asInt() < metadata["keynum"][0u].asInt()){
        metadata["frags"].shrink(metadata["frags"].size() - 1);
        // increase the missed fragments counter
        metadata["missed_frags"] = metadata["missed_frags"].asInt() + 1;
      }
      if (metadata["frags"].size() > 0){
        // set oldestFrag to the first keynum outside any current fragment
        long long unsigned int oldestFrag = metadata["frags"][metadata["frags"].size() - 1]["num"].asInt() + metadata["frags"][metadata["frags"].size() - 1]["len"].asInt();
        // seek fragStart to the first keynum >= oldestFrag
        while (metadata["keynum"][fragStart].asInt() < oldestFrag){
          fragStart++;
        }
      }
    }
    for (unsigned int i = fragStart; i < metadata["keytime"].size(); i++){
      if (i == fragStart){
        long long int currFrag = metadata["keytime"][i].asInt() / 10000;
        long long int fragLen = 1;
        long long int fragDur = metadata["keylen"][i].asInt();
        for (unsigned int j = i + 1; j < metadata["keytime"].size(); j++){
          // if we are now 10+ seconds, finish the fragment
          if (fragDur >= 10000){
            // construct and append the fragment
            JSON::Value thisFrag;
            thisFrag["num"] = metadata["keynum"][i];
            thisFrag["len"] = fragLen;
            thisFrag["dur"] = fragDur;
            metadata["frags"].append(thisFrag);
            // next fragment starts fragLen fragments up
            fragStart += fragLen;
            // skip that many - no unneeded looping
            i += fragLen - 1;
            break;
          }
          // otherwise, +1 the length and add up the duration
          fragLen++;
          fragDur += metadata["keylen"][j].asInt();
        }
      }
    }
    metadata["lastms"] = buffers[keyframes[0].b]["time"].asInt();
    metadata["buffer_window"] = (long long int)buffertime;
    metadata["live"] = true;
    metadata.netPrepare();
    updateRingHeaders();
  }
}

void DTSC::Stream::updateRingHeaders(){
  std::set<DTSC::Ring *>::iterator sit;
  if ( !rings.size()){
    return;
  }
  for (sit = rings.begin(); sit != rings.end(); sit++){
    ( *sit)->updated = true;
  }
}

/// Returns 0 if seeking is possible, -1 if the wanted frame is too old, 1 if the wanted frame is too new.
int DTSC::Stream::canSeekms(unsigned int ms){
  if ( !metadata["keytime"].size()){
    return 1;
  }
  if (ms > metadata["keytime"][metadata["keytime"].size() - 1].asInt()){
    return 1;
  }
  if (ms < metadata["keytime"][0u].asInt()){
    return -1;
  }
  return 0;
}

/// Returns 0 if seeking is possible, -1 if the wanted frame is too old, 1 if the wanted frame is too new.
int DTSC::Stream::canSeekFrame(unsigned int frameno){
  if ( !metadata["keynum"].size()){
    return 1;
  }
  if (frameno > metadata["keynum"][metadata["keynum"].size() - 1].asInt()){
    return 1;
  }
  if (frameno < metadata["keynum"][0u].asInt()){
    return -1;
  }
  return 0;
}

unsigned int DTSC::Stream::msSeek(unsigned int ms){
  if (ms > buffers[keyframes[0u].b]["time"].asInt()){
    std::cerr << "Warning: seeking past ingest! (" << ms << "ms > " << buffers[keyframes[0u].b]["time"].asInt() << "ms)" << std::endl;
    return keyframes[0u].b;
  }
  for (std::deque<DTSC::Ring>::iterator it = keyframes.begin(); it != keyframes.end(); it++){
    if (buffers[it->b]["time"].asInt() <= ms){
      return it->b;
    }
  }
  std::cerr << "Warning: seeking past buffer size! (" << ms << "ms < " << buffers[keyframes[keyframes.size() - 1].b]["time"].asInt() << "ms)" << std::endl;
  return keyframes[keyframes.size() - 1].b;
}

unsigned int DTSC::Stream::frameSeek(unsigned int frameno){
  if (frameno > buffers[keyframes[0u].b]["fragnum"].asInt()){
    std::cerr << "Warning: seeking past ingest! (F" << frameno << " > F" << buffers[keyframes[0u].b]["fragnum"].asInt() << ")" << std::endl;
    return keyframes[0u].b;
  }
  for (std::deque<DTSC::Ring>::iterator it = keyframes.begin(); it != keyframes.end(); it++){
    if (buffers[it->b]["fragnum"].asInt() == frameno){
      return it->b;
    }
  }
  std::cerr << "Warning: seeking past buffer size! (F" << frameno << " < F" << buffers[keyframes[keyframes.size() - 1].b]["fragnum"].asInt() << ")" << std::endl;
  return keyframes[keyframes.size() - 1].b;
}

/// Properly cleans up the object for erasing.
/// Drops all Ring classes that have been given out.
DTSC::Stream::~Stream(){
  std::set<DTSC::Ring *>::iterator sit;
  for (sit = rings.begin(); sit != rings.end(); sit++){
    delete ( *sit);
  }
}

DTSC::File::File(){
  F = 0;
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
  strbuffer = rhs.strbuffer;
  jsonbuffer = rhs.jsonbuffer;
  metadata = rhs.metadata;
  firstmetadata = rhs.firstmetadata;
  frames = rhs.frames;
  msframes = rhs.msframes;
  currtime = rhs.currtime;
  lastreadpos = rhs.lastreadpos;
  headerSize = rhs.headerSize;
  memcpy(buffer, rhs.buffer, 4);
}

/// Open a filename for DTSC reading/writing.
/// If create is true and file does not exist, attempt to create.
DTSC::File::File(std::string filename, bool create){
  if (create){
    F = fopen(filename.c_str(), "w+b");
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
  //currframe = 1;
  //frames[1] = 8 + headerSize;
  //msframes[1] = 0;
}

/// Returns the header metadata for this file as JSON::Value.
JSON::Value & DTSC::File::getMeta(){
  return metadata;
}

/// Returns the header metadata for this file as JSON::Value.
JSON::Value & DTSC::File::getFirstMeta(){
  return firstmetadata;
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
    metadata.null();
    return;
  }
  if (memcmp(buffer, DTSC::Magic_Header, 4) != 0){
    fprintf(stderr, "Invalid header - %.4s != %.4s  (H%i)\n", buffer, DTSC::Magic_Header, pos);
    strbuffer = "";
    metadata.null();
    return;
  }
  if (fread(buffer, 4, 1, F) != 1){
    fprintf(stderr, "Could not read size (H%i)\n", pos);
    strbuffer = "";
    metadata.null();
    return;
  }
  uint32_t * ubuffer = (uint32_t *)buffer;
  long packSize = ntohl(ubuffer[0]);
  strbuffer.resize(packSize);
  if (packSize){
    if (fread((void*)strbuffer.c_str(), packSize, 1, F) != 1){
      fprintf(stderr, "Could not read packet (H%i)\n", pos);
      strbuffer = "";
      metadata.null();
      return;
    }
    metadata = JSON::fromDTMI(strbuffer);
  }
  if (pos == 0){
    firstmetadata = metadata;
  }
  //if there is another header, read it and replace metadata with that one.
  if (metadata.isMember("moreheader") && metadata["moreheader"].asInt() > 0){
    readHeader(metadata["moreheader"].asInt());
    return;
  }
  if (metadata.isMember("keytime")){
    msframes.clear();
    for (int i = 0; i < metadata["keytime"].size(); ++i){
      msframes[i + 1] = metadata["keytime"][i].asInt();
    }
  }
  if (metadata.isMember("keybpos")){
    frames.clear();
    for (int i = 0; i < metadata["keybpos"].size(); ++i){
      frames[i + 1] = metadata["keybpos"][i].asInt();
    }
  }
  metadata["vod"] = true;
  metadata.netPrepare();
}

long int DTSC::File::getBytePosEOF(){
  fseek(F, 0, SEEK_END);
  return ftell(F);
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
    jsonbuffer = metadata;
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
    fprintf(stderr, "Invalid packet header @ %#x - %.4s != %.4s\n", getBytePos(), buffer, DTSC::Magic_Packet2);
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
  long packSize = ntohl(ubuffer[0]) + (version == 2 ? 12 : 0);
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
  if (jsonbuffer.isMember("keyframe")){
    if (frames[currframe] != lastreadpos){
      currframe++;
      currtime = jsonbuffer["time"].asInt();
#if DEBUG >= 6
      if (frames[currframe] != lastreadpos){
        std::cerr << "Found a new frame " << currframe << " @ " << lastreadpos << "b/" << currtime << "ms" << std::endl;
      } else{
        std::cerr << "Passing frame " << currframe << " @ " << lastreadpos << "b/" << currtime << "ms" << std::endl;
      }
#endif
      frames[currframe] = lastreadpos;
      msframes[currframe] = currtime;
    }
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

/// Attempts to seek to the given frame number within the file.
/// Returns true if successful, false otherwise.
bool DTSC::File::seek_frame(int frameno){
  if (frames.count(frameno) > 0){
    if (fseek(F, frames[frameno], SEEK_SET) == 0){
#if DEBUG >= 5
      std::cerr << "Seek direct from " << currframe << " @ " << frames[currframe] << " to " << frameno << " @ " << frames[frameno] << std::endl;
#endif
      currframe = frameno;
      return true;
    }
  }else{
    for (int i = frameno; i >= 1; --i){
      if (frames.count(i) > 0){
        currframe = i;
        break;
      }
    }
    if (fseek(F, frames[currframe], SEEK_SET) == 0){
#if DEBUG >= 5
      std::cerr << "Seeking from frame " << currframe << " @ " << frames[currframe] << " to " << frameno << std::endl;
#endif
      while (currframe < frameno){
        seekNext();
        if (jsonbuffer.isNull()){
          return false;
        }
      }
      seek_frame(frameno);
      return true;
    }
  }
  return false;
}

/// Attempts to seek to the given time in ms within the file.
/// Returns true if successful, false otherwise.
bool DTSC::File::seek_time(int ms){
  std::map<int, long>::iterator it;
  currtime = 0;
  currframe = 1;
  for (it = msframes.begin(); it != msframes.end(); ++it){
    if (it->second > ms){
      break;
    }
    if (it->second > currtime){
      currtime = it->second;
      currframe = it->first;
    }
  }
  if (fseek(F, frames[currframe], SEEK_SET) == 0){
#if DEBUG >= 5
    std::cerr << "Seeking from frame " << currframe << " @ " << msframes[currframe] << "ms to " << ms << "ms" << std::endl;
#endif
    while (currtime < ms){
      seekNext();
      if (jsonbuffer.isNull()){
        return false;
      }
    }
    if (currtime > ms){
      return seek_frame(currframe - 1);
    }
    return true;
  }
  return false;
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
}

void DTSC::File::writePacket(JSON::Value & newPacket){
  writePacket(newPacket.toNetPacked());
}

/// Close the file if open
DTSC::File::~File(){
  if (F){
    fclose(F);
    F = 0;
  }
}
