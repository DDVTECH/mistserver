/// \file player.h
/// Provides functionality for playing files for Video on Demand

#pragma once

#include "buffer_stream.h"

namespace Player{
  class File{
  private:
    std::ifstream fileSrc; ///<File handle of the input file.
    std::string inBuffer; ///<Buffer of unprocessed bytes read from input.
    DTSC::Stream * stream;
    DTSC::Ring * ring;
    bool nextPacket(); ///<Pulls the next packet into the queue.
    bool getPacketFromInput(); ///<Attempts to retrieve a packet from input.
    bool readCommand();
    int fillBuffer(std::string & buffer);
  public:
    File(std::string filename); ///<Attempts to open a DTSC file
    void Play();
    ~File();
    void seek(unsigned int miliseconds);
    std::string & getPacket();
  };
};