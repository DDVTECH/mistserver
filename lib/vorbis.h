#pragma once
#include <cmath>
#include <sys/types.h>
#include <stdint.h>
#include <string>
#include <deque>

namespace vorbis {
  struct mode {
    bool blockFlag;
    unsigned short windowType;
    unsigned short transformType;
    char mapping;
  };

  inline unsigned int ilog(unsigned int input) {
    return (std::log(input)) / (std::log(2)) + 1;
  }

  class header {
    public:
      header();
      header(char * newData, unsigned int length);
      bool read(char * newData, unsigned int length);
      int getHeaderType();
      long unsigned int getVorbisVersion();
      char getAudioChannels();
      long unsigned int getAudioSampleRate();
      long unsigned int getBitrateMaximum();
      long unsigned int getBitrateNominal();
      long unsigned int getBitrateMinimum();
      char getBlockSize0();
      char getBlockSize1();
      char getFramingFlag();
      std::string toPrettyString(size_t indent = 0);
      std::deque<mode> readModeDeque(char audioChannels);
    protected:
      uint32_t getInt32(size_t index);
      uint32_t getInt24(size_t index);
      uint16_t getInt16(size_t index);
    private:
      std::deque<mode> modes;
      char * data;
      unsigned int datasize;
      bool checkDataSize(unsigned int size);
      bool validate();
  };
}
