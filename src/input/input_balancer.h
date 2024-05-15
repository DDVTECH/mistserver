#include "input.h"
#include <mist/dtsc.h>

namespace Mist{
  class InputBalancer : public Input{
  public:
    InputBalancer(Util::Config *cfg);
    int boot(int argc, char *argv[]);

  protected:
    bool checkArguments(){return false;};
    bool needHeader(){return false;};
  };
}// namespace Mist

typedef Mist::InputBalancer mistIn;
