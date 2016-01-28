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

DTSC::File::File() {
  F = 0;
  buffer = malloc(4);
  endPos = 0;
}

DTSC::File::File(const File & rhs) {
  buffer = malloc(4);
  *this = rhs;
}

DTSC::File & DTSC::File::operator =(const File & rhs) {
  created = rhs.created;
  if (rhs.F) {
    F = fdopen(dup(fileno(rhs.F)), (created ? "w+b" : "r+b"));
  } else {
    F = 0;
  }
  endPos = rhs.endPos;
  if (rhs.myPack) {
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

DTSC::File::operator bool() const {
  return F;
}

/// Open a filename for DTSC reading/writing.
/// If create is true and file does not exist, attempt to create.
DTSC::File::File(std::string filename, bool create) {
  buffer = malloc(8);
  if (create) {
    F = fopen(filename.c_str(), "w+b");
    if (!F) {
      DEBUG_MSG(DLVL_ERROR, "Could not create file %s: %s", filename.c_str(), strerror(errno));
      return;
    }
    //write an empty header
    fseek(F, 0, SEEK_SET);
    fwrite(DTSC::Magic_Header, 4, 1, F);
    memset(buffer, 0, 4);
    fwrite(buffer, 4, 1, F); //write 4 zero-bytes
    headerSize = 0;
  } else {
    F = fopen(filename.c_str(), "r+b");
  }
  created = create;
  if (!F) {
    DEBUG_MSG(DLVL_ERROR, "Could not open file %s", filename.c_str());
    return;
  }
  fseek(F, 0, SEEK_END);
  endPos = ftell(F);

  bool sepHeader = false;
  if (!create) {
    fseek(F, 0, SEEK_SET);
    if (fread(buffer, 4, 1, F) != 1) {
      DEBUG_MSG(DLVL_ERROR, "Can't read file contents of %s", filename.c_str());
      fclose(F);
      F = 0;
      return;
    }
    if (memcmp(buffer, DTSC::Magic_Header, 4) != 0) {
      if (memcmp(buffer, DTSC::Magic_Packet2, 4) != 0 && memcmp(buffer, DTSC::Magic_Packet, 4) != 0) {
        DEBUG_MSG(DLVL_ERROR, "%s is not a valid DTSC file", filename.c_str());
        fclose(F);
        F = 0;
        return;
      } else {
        metadata.moreheader = -1;
      }
    }
  }
  //we now know the first 4 bytes are DTSC::Magic_Header and we have a valid file
  fseek(F, 4, SEEK_SET);
  if (fread(buffer, 4, 1, F) != 1) {
    fseek(F, 4, SEEK_SET);
    memset(buffer, 0, 4);
    fwrite(buffer, 4, 1, F); //write 4 zero-bytes
  } else {
    headerSize = ntohl(((uint32_t *)buffer)[0]);
  }
  if (metadata.moreheader != -1) {
    if (!sepHeader) {
      readHeader(0);
      fseek(F, 8 + headerSize, SEEK_SET);
    } else {
      fseek(F, 0, SEEK_SET);
    }
  } else {
    fseek(F, 0, SEEK_SET);
    File Fhead(filename + ".dtsh");
    if (Fhead) {
      metaStorage = Fhead.metaStorage;
      metadata = metaStorage;
    }
  }
  currframe = 0;
}


/// Returns the header metadata for this file as JSON::Value.
DTSC::Meta & DTSC::File::getMeta() {
  return metadata;
}


/// (Re)writes the given string to the header area if the size is the same as the existing header.
/// Forces a write if force is set to true.
bool DTSC::File::writeHeader(std::string & header, bool force) {
  if (headerSize != header.size() && !force) {
    DEBUG_MSG(DLVL_ERROR, "Could not overwrite header - not equal size");
    return false;
  }
  headerSize = header.size();
  int pSize = htonl(header.size());
  fseek(F, 4, SEEK_SET);
  int tmpret = fwrite((void *)(&pSize), 4, 1, F);
  if (tmpret != 1) {
    return false;
  }
  fseek(F, 8, SEEK_SET);
  int ret = fwrite(header.c_str(), headerSize, 1, F);
  fseek(F, 8 + headerSize, SEEK_SET);
  return (ret == 1);
}

/// Adds the given string as a new header to the end of the file.
/// \returns The positon the header was written at, or 0 on failure.
long long int DTSC::File::addHeader(std::string & header) {
  fseek(F, 0, SEEK_END);
  long long int writePos = ftell(F);
  int hSize = htonl(header.size());
  int ret = fwrite(DTSC::Magic_Header, 4, 1, F); //write header
  if (ret != 1) {
    return 0;
  }
  ret = fwrite((void *)(&hSize), 4, 1, F); //write size
  if (ret != 1) {
    return 0;
  }
  ret = fwrite(header.c_str(), header.size(), 1, F); //write contents
  if (ret != 1) {
    return 0;
  }
  fseek(F, 0, SEEK_END);
  endPos = ftell(F);
  return writePos; //return position written at
}

/// Reads the header at the given file position.
/// If the packet could not be read for any reason, the reason is printed.
/// Reading the header means the file position is moved to after the header.
void DTSC::File::readHeader(int pos) {
  fseek(F, pos, SEEK_SET);
  if (fread(buffer, 4, 1, F) != 1) {
    if (feof(F)) {
      DEBUG_MSG(DLVL_DEVEL, "End of file reached while reading header @ %d", pos);
    } else {
      DEBUG_MSG(DLVL_ERROR, "Could not read header @ %d", pos);
    }
    metadata = Meta();
    return;
  }
  if (memcmp(buffer, DTSC::Magic_Header, 4) != 0) {
    DEBUG_MSG(DLVL_ERROR, "Invalid header - %.4s != %.4s  @ %i", (char *)buffer, DTSC::Magic_Header, pos);
    metadata = Meta();
    return;
  }
  if (fread(buffer, 4, 1, F) != 1) {
    DEBUG_MSG(DLVL_ERROR, "Could not read header size @ %i", pos);
    metadata = Meta();
    return;
  }
  long packSize = ntohl(((unsigned long *)buffer)[0]) + 8;
  std::string strBuffer;
  strBuffer.resize(packSize);
  if (packSize) {
    fseek(F, pos, SEEK_SET);
    if (fread((void *)strBuffer.c_str(), packSize, 1, F) != 1) {
      DEBUG_MSG(DLVL_ERROR, "Could not read header packet @ %i", pos);
      metadata = Meta();
      return;
    }
    metadata = Meta(DTSC::Packet(strBuffer.data(), strBuffer.size(),true));
  }
  //if there is another header, read it and replace metadata with that one.
  if (metadata.moreheader) {
    if (metadata.moreheader < getBytePosEOF()) {
      readHeader(metadata.moreheader);
      return;
    }
  }
  if (!metadata.live){
    metadata.vod = true;
  }
}

long int DTSC::File::getBytePosEOF() {
  return endPos;
}

long int DTSC::File::getBytePos() {
  return ftell(F);
}

bool DTSC::File::reachedEOF() {
  return feof(F);
}

/// Reads the packet available at the current file position.
/// If the packet could not be read for any reason, the reason is printed.
/// Reading the packet means the file position is increased to the next packet.
void DTSC::File::seekNext() {
  if (!currentPositions.size()) {
    DEBUG_MSG(DLVL_WARN, "No seek positions set - returning empty packet.");
    myPack.null();
    return;
  }
  seekPos thisPos = *currentPositions.begin();
  fseek(F, thisPos.bytePos, SEEK_SET);
  if (reachedEOF()) {
    myPack.null();
    return;
  }
  clearerr(F);
  currentPositions.erase(currentPositions.begin());
  lastreadpos = ftell(F);
  if (fread(buffer, 4, 1, F) != 1) {
    if (feof(F)) {
      DEBUG_MSG(DLVL_DEVEL, "End of file reached while seeking @ %i", (int)lastreadpos);
    } else {
      DEBUG_MSG(DLVL_ERROR, "Could not seek to next @ %i", (int)lastreadpos);
    }
    myPack.null();
    return;
  }
  if (memcmp(buffer, DTSC::Magic_Header, 4) == 0) {
    seek_time(myPack.getTime(), myPack.getTrackId(), true);
    return seekNext();
  }
  long long unsigned int version = 0;
  if (memcmp(buffer, DTSC::Magic_Packet, 4) == 0) {
    version = 1;
  }
  if (memcmp(buffer, DTSC::Magic_Packet2, 4) == 0) {
    version = 2;
  }
  if (version == 0) {
    DEBUG_MSG(DLVL_ERROR, "Invalid packet header @ %#x - %.4s != %.4s @ %d", (unsigned int)lastreadpos, (char *)buffer, DTSC::Magic_Packet2, (int)lastreadpos);
    myPack.null();
    return;
  }
  if (fread(buffer, 4, 1, F) != 1) {
    DEBUG_MSG(DLVL_ERROR, "Could not read packet size @ %d", (int)lastreadpos);
    myPack.null();
    return;
  }
  long packSize = ntohl(((unsigned long *)buffer)[0]);
  char * packBuffer = (char *)malloc(packSize + 8);
  if (version == 1) {
    memcpy(packBuffer, "DTPD", 4);
  } else {
    memcpy(packBuffer, "DTP2", 4);
  }
  memcpy(packBuffer + 4, buffer, 4);
  if (fread((void *)(packBuffer + 8), packSize, 1, F) != 1) {
    DEBUG_MSG(DLVL_ERROR, "Could not read packet @ %d", (int)lastreadpos);
    myPack.null();
    free(packBuffer);
    return;
  }
  myPack.reInit(packBuffer, packSize + 8);
  free(packBuffer);
  if (metadata.merged) {
    int tempLoc = getBytePos();
    char newHeader[20];
    bool insert = false;
    seekPos tmpPos;
    if (fread((void *)newHeader, 20, 1, F) == 1) {
      if (memcmp(newHeader, DTSC::Magic_Packet2, 4) == 0) {
        tmpPos.bytePos = tempLoc;
        tmpPos.trackID = ntohl(((int *)newHeader)[2]);
        tmpPos.seekTime = 0;
        if (selectedTracks.find(tmpPos.trackID) != selectedTracks.end()) {
          tmpPos.seekTime = ((long long unsigned int)ntohl(((int *)newHeader)[3])) << 32;
          tmpPos.seekTime += ntohl(((int *)newHeader)[4]);
          insert = true;
        } else {
          long tid = myPack.getTrackId();
          for (unsigned int i = 0; i != metadata.tracks[tid].keys.size(); i++) {
            if ((unsigned long long)metadata.tracks[tid].keys[i].getTime() > myPack.getTime()) {
              tmpPos.seekTime = metadata.tracks[tid].keys[i].getTime();
              tmpPos.bytePos = metadata.tracks[tid].keys[i].getBpos();
              tmpPos.trackID = tid;
              insert = true;
              break;
            }
          }
        }
        if (currentPositions.size()) {
          for (std::set<seekPos>::iterator curPosIter = currentPositions.begin(); curPosIter != currentPositions.end(); curPosIter++) {
            if ((*curPosIter).trackID == tmpPos.trackID && (*curPosIter).seekTime >= tmpPos.seekTime) {
              insert = false;
              break;
            }
          }
        }
      }
    }
    if (insert){
      if (tmpPos.seekTime > 0xffffffffffffff00ll){
        tmpPos.seekTime = 0;
      }
      currentPositions.insert(tmpPos);
    } else {
      seek_time(myPack.getTime(), myPack.getTrackId(), true);
    }
    seek_bpos(tempLoc);
  }else{
    seek_time(thisPos.seekTime, thisPos.trackID);
    fseek(F, thisPos.bytePos, SEEK_SET);
  }
}

void DTSC::File::parseNext(){
  lastreadpos = ftell(F);
  if (fread(buffer, 4, 1, F) != 1) {
    if (feof(F)) {
      DEBUG_MSG(DLVL_DEVEL, "End of file reached @ %d", (int)lastreadpos);
    } else {
      DEBUG_MSG(DLVL_ERROR, "Could not read header @ %d", (int)lastreadpos);
    }
    myPack.null();
    return;
  }
  if (memcmp(buffer, DTSC::Magic_Header, 4) == 0) {
    if (lastreadpos != 0) {
      readHeader(lastreadpos);
      std::string tmp = metaStorage.toNetPacked();
      myPack.reInit(tmp.data(), tmp.size());
      DEBUG_MSG(DLVL_DEVEL, "Read another header");
    } else {
      if (fread(buffer, 4, 1, F) != 1) {
        DEBUG_MSG(DLVL_ERROR, "Could not read header size @ %d", (int)lastreadpos);
        myPack.null();
        return;
      }
      long packSize = ntohl(((unsigned long *)buffer)[0]);
      std::string strBuffer = "DTSC";
      strBuffer.append((char *)buffer, 4);
      strBuffer.resize(packSize + 8);
      if (fread((void *)(strBuffer.c_str() + 8), packSize, 1, F) != 1) {
        DEBUG_MSG(DLVL_ERROR, "Could not read header @ %d", (int)lastreadpos);
        myPack.null();
        return;
      }
      myPack.reInit(strBuffer.data(), strBuffer.size());
    }
    return;
  }
  long long unsigned int version = 0;
  if (memcmp(buffer, DTSC::Magic_Packet, 4) == 0) {
    version = 1;
  }
  if (memcmp(buffer, DTSC::Magic_Packet2, 4) == 0) {
    version = 2;
  }
  if (version == 0) {
    DEBUG_MSG(DLVL_ERROR, "Invalid packet header @ %#x - %.4s != %.4s @ %d", (unsigned int)lastreadpos, (char *)buffer, DTSC::Magic_Packet2, (int)lastreadpos);
    myPack.null();
    return;
  }
  if (fread(buffer, 4, 1, F) != 1) {
    DEBUG_MSG(DLVL_ERROR, "Could not read packet size @ %d", (int)lastreadpos);
    myPack.null();
    return;
  }
  long packSize = ntohl(((unsigned long *)buffer)[0]);
  char * packBuffer = (char *)malloc(packSize + 8);
  if (version == 1) {
    memcpy(packBuffer, "DTPD", 4);
  } else {
    memcpy(packBuffer, "DTP2", 4);
  }
  memcpy(packBuffer + 4, buffer, 4);
  if (fread((void *)(packBuffer + 8), packSize, 1, F) != 1) {
    DEBUG_MSG(DLVL_ERROR, "Could not read packet @ %d", (int)lastreadpos);
    myPack.null();
    free(packBuffer);
    return;
  }
  myPack.reInit(packBuffer, packSize + 8);
  free(packBuffer);
}

/// Returns the byte positon of the start of the last packet that was read.
long long int DTSC::File::getLastReadPos() {
  return lastreadpos;
}

/// Returns the internal buffer of the last read packet in raw binary format.
DTSC::Packet & DTSC::File::getPacket() {
  return myPack;
}

bool DTSC::File::seek_time(unsigned int ms, unsigned int trackNo, bool forceSeek) {
  seekPos tmpPos;
  tmpPos.trackID = trackNo;
  if (!forceSeek && myPack && ms >= myPack.getTime() && trackNo >= myPack.getTrackId()) {
    tmpPos.seekTime = myPack.getTime();
    tmpPos.bytePos = getBytePos();
  } else {
    tmpPos.seekTime = 0;
    tmpPos.bytePos = 0;
  }
  if (reachedEOF()) {
    clearerr(F);
    seek_bpos(0);
    tmpPos.bytePos = 0;
    tmpPos.seekTime = 0;
  }
  DTSC::Track & trackRef = metadata.tracks[trackNo];
  for (unsigned int i = 0; i < trackRef.keys.size(); i++) {
    long keyTime = trackRef.keys[i].getTime();
    if (keyTime > ms) {
      break;
    }
    if ((long long unsigned int)keyTime > tmpPos.seekTime) {
      tmpPos.seekTime = keyTime;
      tmpPos.bytePos = trackRef.keys[i].getBpos();
    }
  }
  bool foundPacket = false;
  while (!foundPacket) {
    lastreadpos = ftell(F);
    if (reachedEOF()) {
      DEBUG_MSG(DLVL_WARN, "Reached EOF during seek to %u in track %d - aborting @ %lld", ms, trackNo, lastreadpos);
      return false;
    }
    //Seek to first packet after ms.
    seek_bpos(tmpPos.bytePos);
    //read the header
    char header[20];
    if (fread((void *)header, 20, 1, F) != 1){
      DEBUG_MSG(DLVL_WARN, "Could not read header from file. Much sadface.");
      return false;
    }
    //check if packetID matches, if not, skip size + 8 bytes.
    int packSize = ntohl(((int *)header)[1]);
    unsigned int packID = ntohl(((int *)header)[2]);
    if (memcmp(header, Magic_Packet2, 4) != 0 || packID != trackNo) {
      if (memcmp(header, "DT", 2) != 0) {
        DEBUG_MSG(DLVL_WARN, "Invalid header during seek to %u in track %d @ %lld - resetting bytePos from %lld to zero", ms, trackNo, lastreadpos, tmpPos.bytePos);
        tmpPos.bytePos = 0;
        continue;
      }
      tmpPos.bytePos += 8 + packSize;
      continue;
    }
    //get timestamp of packet, if too large, break, if not, skip size bytes.
    long long unsigned int myTime = ((long long unsigned int)ntohl(((int *)header)[3]) << 32);
    myTime += ntohl(((int *)header)[4]);
    tmpPos.seekTime = myTime;
    if (myTime >= ms) {
      foundPacket = true;
    } else {
      tmpPos.bytePos += 8 + packSize;
      continue;
    }
  }
  //DEBUG_MSG(DLVL_HIGH, "Seek to %u:%d resulted in %lli", trackNo, ms, tmpPos.seekTime);
  if (tmpPos.seekTime > 0xffffffffffffff00ll){
    tmpPos.seekTime = 0;
  }
  currentPositions.insert(tmpPos);
  return true;
}

/// Attempts to seek to the given time in ms within the file.
/// Returns true if successful, false otherwise.
bool DTSC::File::seek_time(unsigned int ms) {
  currentPositions.clear();
  if (selectedTracks.size()) {
    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++) {
      seek_time(ms, (*it), true);
    }
  }
  return true;
}

bool DTSC::File::seek_bpos(int bpos) {
  if (fseek(F, bpos, SEEK_SET) == 0) {
    return true;
  }
  return false;
}

void DTSC::File::rewritePacket(std::string & newPacket, int bytePos) {
  fseek(F, bytePos, SEEK_SET);
  fwrite(newPacket.c_str(), newPacket.size(), 1, F);
  fseek(F, 0, SEEK_END);
  if (ftell(F) > endPos) {
    endPos = ftell(F);
  }
}

void DTSC::File::writePacket(std::string & newPacket) {
  fseek(F, 0, SEEK_END);
  fwrite(newPacket.c_str(), newPacket.size(), 1, F); //write contents
  fseek(F, 0, SEEK_END);
  endPos = ftell(F);
}

void DTSC::File::writePacket(JSON::Value & newPacket) {
  writePacket(newPacket.toNetPacked());
}

bool DTSC::File::atKeyframe() {
  if (myPack.getFlag("keyframe")) {
    return true;
  }
  long long int bTime = myPack.getTime();
  DTSC::Track & trackRef = metadata.tracks[myPack.getTrackId()];
  for (unsigned int i = 0; i < trackRef.keys.size(); i++) {
    if (trackRef.keys[i].getTime() >= bTime) {
      return (trackRef.keys[i].getTime() == bTime);
    }
  }
  return false;
}

void DTSC::File::selectTracks(std::set<unsigned long> & tracks) {
  selectedTracks = tracks;
  currentPositions.clear();
  seek_time(0);
}

/// Close the file if open
DTSC::File::~File() {
  if (F) {
    fclose(F);
    F = 0;
  }
  free(buffer);
}
