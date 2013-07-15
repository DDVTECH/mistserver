#pragma once
#include<sys/types.h>
#include<stdint.h>
#include<string>

namespace vorbis{
  class header{
    public:
      header();
      bool read(char* newData, unsigned int length);
    private:
      char* data;
      unsigned int datasize;
      bool checkDataSize(unsigned int size);
  };
}
