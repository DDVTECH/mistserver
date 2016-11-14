#include "input.h"
#include <mist/dtsc.h>
#include <mist/flv_tag.h>

namespace Mist {
  class inputFLV : public Input {
    public:
      inputFLV(Util::Config * cfg);
    protected:
      //Private Functions
      bool setup();
      bool readHeader();
      void getNext(bool smart = true);
      void seek(int seekTime);
      void trackSelect(std::string trackSpec);
      bool keepRunning();
      FLV::Tag tmpTag;
      uint64_t lastModTime;
      FILE * inFile;
  };
}

typedef Mist::inputFLV mistIn;

