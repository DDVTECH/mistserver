#include<sys/types.h>
#include<stdint.h>

namespace theora{
  class header{
    public:
      header();
      bool read(char* newData, unsigned int length);
      int getHeaderType();
      long unsigned int getFRN();
      long unsigned int getFRD();
    protected:
      uint32_t getInt32(size_t index);
    private:
      char* data;
      unsigned int datasize;
      bool checkDataSize(unsigned int size);
  };
}
