
#include <iostream>
#include <fstream>
#include <algorithm>

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

  File::File(std::string filename){
    stream = new DTSC::Stream(5);
    ring = NULL;// ring will be initialized when necessary
    fileSrc.open(filename.c_str(), std::ifstream::in | std::ifstream::binary);
    std::cout.setf(std::ios::unitbuf);//do not choke
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
  // \returns True if there is a packet available for pull.
  bool File::nextPacket(){
    if (fileSrc.good()){
      if (stream->parsePacket(inBuffer)){
        return true;
      } else {
        char buffer[1024 * 10];
        fileSrc.read(buffer, sizeof(buffer));
        inBuffer.append(buffer, fileSrc.gcount());
      }
    }
    return false;
  }
  void File::seek(int position){
    // XXX: implement seek.
  };
  std::string * File::getPacket(){
    if (ring->waiting){
      return NULL;
    }//still waiting for next buffer?
    if (ring->starved){
      //if corrupt data, warn and get new DTSC::Ring
      std::cerr << "Warning: User was send corrupt video data and send to the next keyframe!" << std::endl;
      stream->dropRing(ring);
      ring = stream->getRing();
      return NULL;
    }
    //switch to next buffer
    if (ring->b < 0){
      ring->waiting = true;
      return NULL;
    }//no next buffer? go in waiting mode.
    // get current packet
    std::string * packet = &stream->outPacket(ring->b);
    // make next request take a different packet
    ring->b--;
    return packet;
  }

  /// Reads a command from stdin. Returns true if a command was read.
  bool File::readCommand() {
    // XXX: implement seek.
    return false;
  }

  void File::Play() {
    long long now, timeDiff = 0, lastTime = 0;
    while (fileSrc.good()) {
      if (readCommand()) {
        continue;
      }
      now = getNowMS();
      if (now - timeDiff >= lastTime || lastTime - (now - timeDiff) > 5000) {
        if (nextPacket()) {
          std::string * packet;
          if (!ring){ring = stream->getRing();}//get ring after reading first non-metadata
          packet = getPacket();
          if (!packet){
            continue;
          }
          lastTime = stream->getTime();
          if (std::abs(now - timeDiff - lastTime) > 5000) {
            timeDiff = now - lastTime;
          }
          std::cout.write(packet->c_str(), packet->length());
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

