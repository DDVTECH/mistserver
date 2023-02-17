#include "input.h"
#include <mist/dtsc.h>

namespace Mist{
  class inputFolder : public Input{
  public:
    inputFolder(Util::Config *cfg);
    int boot(int argc, char *argv[]);

  protected:
    bool checkArguments(){return false;};
    bool needHeader(){return false;};
    void getNext(size_t idx = INVALID_TRACK_ID){}
    void seek(uint64_t time, size_t idx = INVALID_TRACK_ID){}
  };
}// namespace Mist

typedef Mist::inputFolder mistIn;
