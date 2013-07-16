#pragma once
#include<sys/types.h>
#include<stdint.h>
#include<string>

namespace vorbis{
  class header{
    public:
      header();
      bool read(char* newData, unsigned int length);
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
    protected:
      uint32_t getInt32(size_t index);
      uint32_t getInt24(size_t index);
      uint16_t getInt16(size_t index);
    private:
      char* data;
      unsigned int datasize;
      bool checkDataSize(unsigned int size);
      bool validate();
  };
}
