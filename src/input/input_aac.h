#include "input.h"
#include <mist/dtsc.h>
#include <mist/adts.h>
#include <mist/urireader.h>

namespace Mist{
  class inputAAC : public Input{
  public:
    inputAAC(Util::Config *cfg);
    ~inputAAC();

  protected:
    // Private Functions
    bool checkArguments();
    bool preRun();
    bool readHeader();
    void seek(uint64_t seekTime, size_t idx);
    void getNext(size_t idx = INVALID_TRACK_ID);
    bool keepRunning();
    uint64_t lastModTime;
    HTTP::URIReader inFile;
    size_t audioTrack;
    size_t filePos;
  };
}// namespace Mist

typedef Mist::inputAAC mistIn;
