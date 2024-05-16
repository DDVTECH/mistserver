#include "input.h"
#include <mist/dtsc.h>

namespace Mist{
  class InputDTSC : public Input{
  public:
    InputDTSC(Util::Config *cfg);

  protected:
    // Private Functions
    bool checkArguments();
    bool readHeader();
    void getNext(bool smart = true);
    void seek(int seekTime);
    void trackSelect(std::string trackSpec);

    DTSC::File inFile;

    std::map<int, unsigned long long int> iVecs;
    std::string key;
  };
}// namespace Mist

typedef Mist::InputDTSC mistIn;
