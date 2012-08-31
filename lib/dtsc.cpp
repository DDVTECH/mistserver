/// \file dtsc.cpp
/// Holds all code for DDVTECH Stream Container parsing/generation.

#include "dtsc.h"
#include <stdlib.h>
#include <string.h> //for memcmp
#include <arpa/inet.h> //for htonl/ntohl

char DTSC::Magic_Header[] = "DTSC";
char DTSC::Magic_Packet[] = "DTPD";

/// Initializes a DTSC::Stream with only one packet buffer.
DTSC::Stream::Stream(){
  datapointer = 0;
  buffercount = 1;
}

/// Initializes a DTSC::Stream with a minimum of rbuffers packet buffers.
/// The actual buffer count may not at all times be the requested amount.
DTSC::Stream::Stream(unsigned int rbuffers){
  datapointer = 0;
  if (rbuffers < 1){rbuffers = 1;}
  buffercount = rbuffers;
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
      if (buffer.length() < len+8){return false;}
      unsigned int i = 0;
      metadata = JSON::fromDTMI((unsigned char*)buffer.c_str() + 8, len, i);
      buffer.erase(0, len+8);
      return false;
    }
    if (memcmp(buffer.c_str(), DTSC::Magic_Packet, 4) == 0){
      len = ntohl(((uint32_t *)buffer.c_str())[1]);
      if (buffer.length() < len+8){return false;}
      buffers.push_front(JSON::Value());
      unsigned int i = 0;
      buffers.front() = JSON::fromDTMI((unsigned char*)buffer.c_str() + 8, len, i);
      datapointertype = INVALID;
      if (buffers.front().isMember("data")){
        datapointer = &(buffers.front()["data"].strVal);
        if (buffers.front().isMember("datatype")){
          std::string tmp = buffers.front()["datatype"].asString();
          if (tmp == "video"){datapointertype = VIDEO;}
          if (tmp == "audio"){datapointertype = AUDIO;}
          if (tmp == "meta"){datapointertype = META;}
        }
      }else{
        datapointer = 0;
      }
      buffer.erase(0, len+8);
      while (buffers.size() > buffercount){buffers.pop_back();}
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
    if (magic_search == std::string::npos){
      buffer.clear();
    }else{
      buffer.erase(0, magic_search);
    }
  }
  return false;
}

/// Returns a direct pointer to the data attribute of the last received packet, if available.
/// Returns NULL if no valid pointer or packet is available.
std::string & DTSC::Stream::lastData(){
  return *datapointer;
}

/// Returns the packed in this buffer number.
/// \arg num Buffer number.
JSON::Value & DTSC::Stream::getPacket(unsigned int num){
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

/// Returns a packed DTSC packet, ready to sent over the network.
std::string & DTSC::Stream::outPacket(unsigned int num){
  static std::string emptystring;
  if (num >= buffers.size()) return emptystring;
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
  for (sit = rings.begin(); sit != rings.end(); sit++){
    (*sit)->b++;
    if ((*sit)->waiting){(*sit)->waiting = false; (*sit)->b = 0;}
    if ((*sit)->starved || ((*sit)->b >= buffers.size())){(*sit)->starved = true; (*sit)->b = 0;}
  }
  for (dit = keyframes.begin(); dit != keyframes.end(); dit++){
    dit->b++;
    if (dit->b >= buffers.size()){keyframes.erase(dit); break;}
  }
  if ((lastType() == VIDEO) && (buffers.front().isMember("keyframe"))){
    keyframes.push_front(DTSC::Ring(0));
  }
  //increase buffer size if no keyframes available
  if ((buffercount > 1) && (keyframes.size() < 1)){buffercount++;}
}

/// Constructs a new Ring, at the given buffer position.
/// \arg v Position for buffer.
DTSC::Ring::Ring(unsigned int v){
  b = v;
  waiting = false;
  starved = false;
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

/// Properly cleans up the object for erasing.
/// Drops all Ring classes that have been given out.
DTSC::Stream::~Stream(){
  std::set<DTSC::Ring *>::iterator sit;
  for (sit = rings.begin(); sit != rings.end(); sit++){delete (*sit);}
}

/// Open a filename for DTSC reading/writing.
/// If create is true and file does not exist, attempt to create.
DTSC::File::File(std::string filename, bool create){
  if (create){
    F = fopen(filename.c_str(), "w+b");
  }else{
    F = fopen(filename.c_str(), "r+b");
  }
  if (!F){
    fprintf(stderr, "Could not open file %s\n", filename.c_str());
    return;
  }

  //if first 4 bytes not available, assume empty file, write header
  if (fread(buffer, 4, 1, F) != 1){
    fseek(F, 0, SEEK_SET);
    fwrite(DTSC::Magic_Header, 4, 1, F);
  }else{
    if (memcmp(buffer, DTSC::Magic_Header, 4) != 0){
      fprintf(stderr, "Not a DTSC file - aborting: %s\n", filename.c_str());
      fclose(F);
      F = 0;
      return;
    }
  }
  //we now know the first 4 bytes are DTSC::Magic_Header and we have a valid file
  fseek(F, 4, SEEK_SET);
  if (fread(buffer, 4, 1, F) != 1){
    fseek(F, 4, SEEK_SET);
    memset(buffer, 0, 4);
    fwrite(buffer, 4, 1, F);//write 4 zero-bytes
    headerSize = 0;
  }else{
    uint32_t * ubuffer = (uint32_t *)buffer;
    headerSize = ntohl(ubuffer[0]);
  }
  fseek(F, 8+headerSize, SEEK_SET);
  currframe = 1;
  frames[1] = 8+headerSize;
  msframes[1] = 0;
}

/// Returns the header metadata for this file as a std::string.
/// Sets the file pointer to the first packet.
std::string & DTSC::File::getHeader(){
  if (fseek(F, 8, SEEK_SET) != 0){
    strbuffer = "";
    return strbuffer;
  }
  strbuffer.resize(headerSize);
  if (fread((void*)strbuffer.c_str(), headerSize, 1, F) != 1){
    strbuffer = "";
    return strbuffer;
  }
  fseek(F, 8+headerSize, SEEK_SET);
  currframe = 1;
  return strbuffer;
}

/// (Re)writes the given string to the header area if the size is the same as the existing header.
/// Forces a write if force is set to true.
bool DTSC::File::writeHeader(std::string & header, bool force){
  if (headerSize != header.size() && !force){
    fprintf(stderr, "Could not overwrite header - not equal size\n");
    return false;
  }
  headerSize = header.size();
  fseek(F, 8, SEEK_SET);
  int ret = fwrite(header.c_str(), headerSize, 1, F);
  fseek(F, 8+headerSize, SEEK_SET);
  return (ret == 1);
}

/// Reads the packet available at the current file position.
/// If the packet could not be read for any reason, the reason is printed to stderr.
/// Reading the packet means the file position is increased to the next packet.
void DTSC::File::seekNext(){
  if (fread(buffer, 4, 1, F) != 1){
    fprintf(stderr, "Could not read header\n");
    strbuffer = "";
    jsonbuffer.null();
    return;
  }
  if (memcmp(buffer, DTSC::Magic_Packet, 4) != 0){
    fprintf(stderr, "Invalid header - %.4s != %.4s\n", buffer, DTSC::Magic_Packet);
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
  jsonbuffer = JSON::fromDTMI(strbuffer);
  if (jsonbuffer.isMember("keyframe")){
    long pos = ftell(F) - (packSize + 8);
    if (frames[currframe] != pos){
      currframe++;
      currtime = jsonbuffer["time"].asInt();
      #if DEBUG >= 4
      std::cerr << "Found a new frame " << currframe << " @ " << pos << "b/" << currtime << "s" << std::endl;
      #endif
      frames[currframe] = pos;
      msframes[currframe] = currtime;
    }
  }
}

/// Returns the internal buffer of the last read packet in raw binary format.
std::string & DTSC::File::getPacket(){return strbuffer;}

/// Returns the internal buffer of the last read packet in JSON format.
JSON::Value & DTSC::File::getJSON(){return jsonbuffer;}

/// Attempts to seek to the given frame number within the file.
/// Returns true if successful, false otherwise.
bool DTSC::File::seek_frame(int frameno){
  if (frames.count(frameno) > 0){
    if (fseek(F, frames[frameno], SEEK_SET) == 0){
      currframe = frameno;
      return true;
    }
  }else{
    for (int i = frameno; i >= 1; --i){
      if (frames.count(i) > 0){currframe = i; break;}
    }
    if (fseek(F, frames[currframe], SEEK_SET) == 0){
      #if DEBUG >= 4
      std::cerr << "Seeking from frame " << currframe << " @ " << frames[currframe] << " to " << frameno << std::endl;
      #endif
      while (currframe < frameno){
        seekNext();
        if (jsonbuffer.isNull()){return false;}
      }
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
    if (it->second > ms){break;}
    if (it->second > currtime){currtime = it->second; currframe = it->first;}
  }
  if (fseek(F, frames[currframe], SEEK_SET) == 0){
    #if DEBUG >= 4
    std::cerr << "Seeking from frame " << currframe << " @ " << msframes[currframe] << "ms to " << ms << "ms" << std::endl;
    #endif
    while (currtime < ms){
      seekNext();
      if (jsonbuffer.isNull()){return false;}
    }
    if (currtime > ms){
      return seek_frame(currframe - 1);
    }
    return true;
  }
  return false;
}

/// Close the file if open
DTSC::File::~File(){
  if (F){
    fclose(F);
    F = 0;
  }
}
