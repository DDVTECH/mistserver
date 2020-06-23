#include "defines.h"

namespace ID3 {

  bool isID3(const char * p, const size_t l);
  size_t getID3Size(const char * p, const size_t l);

  uint32_t ss32dec(const char * p);

  class Tag{
    public:
      Tag(const char * p = 0, const size_t l = 0);
    private:
      const char * ptr;
      const size_t len;
  };





}//ID3 namespace



