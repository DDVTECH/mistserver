#include<sys/types.h>
#include<stdint.h>

namespace theora{
  class header{
    public:
      header();
      bool read(char* newData, unsigned int length);
      int getHeaderType();
      char getKFGShift();
      long unsigned int getPICH();//movie height
      long unsigned int getPICW();//movie width
      long unsigned int getFRN();//frame rate numerator
      long unsigned int getFRD();//frame rate denominator
    protected:
      uint32_t getInt32(size_t index);
      uint32_t getInt24(size_t index);
      uint16_t getInt16(size_t index);
    private:
      char* data;
      unsigned int datasize;
      bool checkDataSize(unsigned int size);
  };
}
