#include "input.h"
#include <mist/dtsc.h>

namespace Mist {
  class inputBalancer : public Input {
    public:
      inputBalancer(Util::Config * cfg);
      int boot(int argc, char * argv[]);
    protected:
      bool checkArguments(){return false;};
      bool readHeader(){return false;};
  };
}

typedef Mist::inputBalancer mistIn;

