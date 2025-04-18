#include "input.h"
#include <mist/dtsc.h>
#include <mist/flv_tag.h>

namespace Mist{
  class InputFLV : public Input{
  public:
    InputFLV(Util::Config *cfg);
    ~InputFLV();

  protected:
    // Private Functions
    bool checkArguments();
    bool preRun();
    bool readHeader();
    void getNext(size_t idx = INVALID_TRACK_ID);
    void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID);
    bool keepRunning(bool updateActCtr = true);
    FLV::Tag tmpTag;
    uint64_t lastModTime;
    FILE *inFile;
  };
}// namespace Mist

typedef Mist::InputFLV mistIn;
