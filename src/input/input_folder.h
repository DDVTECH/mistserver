#include "input.h"
#include <mist/dtsc.h>

namespace Mist {
  class inputFolder : public Input {
    public:
      inputFolder(Util::Config * cfg);
      int boot(int argc, char * argv[]);
    protected:
      bool checkArguments(){return false;};
      bool readHeader(){return false;};
      bool needHeader(){return false;};
  };
}

typedef Mist::inputFolder mistIn;
