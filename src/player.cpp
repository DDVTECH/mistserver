
#include <iostream>
#include <fstream>
#include <algorithm>
#include <limits.h>
#include <errno.h>
#include <cstdio>
#include <sys/time.h>
#include <mist/dtsc.h>
#include "player.h"

namespace Player{
  ///\todo Make getNowMS available in a library
  /// Gets the current system time in milliseconds.
  long long int getNowMS(){
    timeval t;
    gettimeofday(&t, 0);
    return t.tv_sec * 1000 + t.tv_usec/1000;
  }//getNowMS

  void setBlocking(int fd, bool blocking){
    int flags = fcntl(fd, F_GETFL);
    if (blocking){
      flags &= ~O_NONBLOCK;
    }else{
      flags |= O_NONBLOCK;
    }
    fcntl(fd, F_SETFL, flags);
  }

  File::File(std::string filename){
    stream = new DTSC::Stream(5);
    ring = NULL;// ring will be initialized when necessary
    fileSrc.open(filename.c_str(), std::ifstream::in | std::ifstream::binary);
    setBlocking(STDIN_FILENO, false);//prevent reading from stdin from blocking
    std::cout.setf(std::ios::unitbuf);//do not choke

    fileSrc.seekg(0, std::ios::end);
    fileSize = fileSrc.tellg();
    fileSrc.seekg(0);

    nextPacket();// initial read always returns nothing
    if (!nextPacket()){//parse metadata
      std::cout << stream->outHeader();
    } else {
      std::cerr << "Error: Expected metadata!" << std::endl;
    }
  };
  File::~File() {
    if (ring) {
      stream->dropRing(ring);
      ring = NULL;
    }
    delete stream;
  }
  /// \returns Number of read bytes or -1 on EOF
  int File::fillBuffer(std::string & buffer){
    char buff[1024 * 10];
    if (fileSrc.good()){
      fileSrc.read(buff, sizeof(buff));
      buffer.append(buff, fileSrc.gcount());
      return fileSrc.gcount();
    }
    return -1;
  }
  // \returns True if there is a packet available for pull.
  bool File::nextPacket(){
    if (stream->parsePacket(inBuffer)){
      return true;
    } else {
      fillBuffer(inBuffer);
    }
    return false;
  }
  void File::seek(unsigned int miliseconds){
    DTSC::Stream * tmpStream = new DTSC::Stream(1);
    unsigned long leftByte = 1, rightByte = fileSize;
    unsigned int leftMS = 0, rightMS = INT_MAX;
    /// \todo set last packet as last byte, consider metadata
    while (rightMS - leftMS >= 100 && leftMS + 100 <= miliseconds){
      std::string buffer;
      // binary search: pick the first packet on the right
      unsigned long medByte = leftByte + (rightByte - leftByte) / 2;
      fileSrc.clear();// clear previous IO errors
      fileSrc.seekg(medByte);

      do{ // find first occurrence of packet
        int read_bytes = fillBuffer(buffer);
        if (read_bytes < 0){// EOF? O noes! EOF!
          goto seekDone;
        }
        unsigned long header_pos = buffer.find(DTSC::Magic_Packet);
        if (header_pos == std::string::npos){
          // it is possible that the magic packet is partially shown, e.g. "DTP"
          if ((unsigned)read_bytes > strlen(DTSC::Magic_Packet) - 1){
            read_bytes -= strlen(DTSC::Magic_Packet) - 1;
            buffer.erase(0, read_bytes);
            medByte += read_bytes;
          }
          continue;// continue filling the buffer without parsing packet
        }
      }while (!tmpStream->parsePacket(buffer));
      JSON::Value & medPacket = tmpStream->getPacket(0);
      /// \todo What if time does not exist? Currently it just crashes.
      // assumes that the time does not get over 49 days (on a 32-bit system)
      unsigned int medMS = (unsigned int)medPacket["time"].asInt();

      if (medMS > miliseconds){
        rightByte = medByte;
        rightMS = medMS;
      }else if (medMS < miliseconds){
        leftByte = medByte;
        leftMS = medMS;
      }
    }
seekDone:
    // clear the buffer and adjust file pointer
    inBuffer.clear();
    fileSrc.seekg(leftByte);
    delete tmpStream;
  };
  std::string & File::getPacket(){
    static std::string emptystring;
    if (ring->waiting){
      return emptystring;
    }//still waiting for next buffer?
    if (ring->starved){
      //if corrupt data, warn and get new DTSC::Ring
      std::cerr << "Warning: User was send corrupt video data and send to the next keyframe!" << std::endl;
      stream->dropRing(ring);
      ring = stream->getRing();
      return emptystring;
    }
    //switch to next buffer
    if (ring->b < 0){
      ring->waiting = true;
      return emptystring;
    }//no next buffer? go in waiting mode.
    // get current packet
    std::string & packet = stream->outPacket(ring->b);
    // make next request take a different packet
    ring->b--;
    return packet;
  }

  /// Reads a command from stdin. Returns true if a command was read.
  bool File::readCommand() {
    char line[512];
    size_t line_len;
    if (fgets(line, sizeof(line), stdin) == NULL){
      return false;
    }
    line[sizeof(line) - 1] = 0;// in case stream is not null-terminated...
    line_len = strlen(line);
    if (line[line_len - 1] == '\n'){
      line[--line_len] = 0;
    }
    {
      unsigned int position = INT_MAX;// special value that says "invalid"
      if (!strncmp("seek ", line, sizeof("seek ") - 1)){
        position = atoi(line + sizeof("seek ") - 1);
      }
      if (!strncmp("relseek ", line, sizeof("relseek " - 1))){
        /// \todo implement relseek in a smart way
        //position = atoi(line + sizeof("relseek "));
      }
      if (position != INT_MAX){
        File::seek(position);
        inBuffer.clear();//clear buffered data from file
        return true;
      }
    }
    if (!strncmp("byteseek ", line, sizeof("byteseek " - 1))){
      std::streampos byte = atoi(line + sizeof("byteseek "));
      fileSrc.seekg(byte);//if EOF, then it's the client's fault, ignore it.
      inBuffer.clear();//clear buffered data from file
      return true;
    }
    if (!strcmp("play", line)){
      playing = true;
    }
    if (!strcmp("pause", line)){
      playing = false;
    }
    return false;
  }

  void File::Play() {
    long long now, timeDiff = 0, lastTime = 0;
    while (fileSrc.good() || !inBuffer.empty()) {
      if (readCommand()) {
        continue;
      }
      if (!playing){
        setBlocking(STDIN_FILENO, true);
        continue;
      }else{
        setBlocking(STDIN_FILENO, false);
      }
      now = getNowMS();
      if (now - timeDiff >= lastTime || lastTime - (now - timeDiff) > 5000) {
        if (nextPacket()) {
          if (!ring){ring = stream->getRing();}//get ring after reading first non-metadata
          std::string & packet = getPacket();
          if (packet.empty()){
            continue;
          }
          lastTime = stream->getTime();
          if (std::abs(now - timeDiff - lastTime) > 5000) {
            timeDiff = now - lastTime;
          }
          std::cout.write(packet.c_str(), packet.length());
        }
      } else {
        usleep(std::min(999LL, lastTime - (now - timeDiff)) * 1000);
      }
    }
  }
};

int main(int argc, char** argv){
  if (argc < 2){
    std::cerr << "Usage: " << argv[0] << " filename.dtsc" << std::endl;
    return 1;
  }
  std::string filename = argv[1];
  #if DEBUG >= 3
  std::cerr << "VoD " << filename << std::endl;
  #endif
  Player::File file(filename);
  file.Play();
  return 0;
}

