#include "input.h"
#include <mist/dtsc.h>

namespace Mist {
  class inputDTSC : public Input {
    public:
      inputDTSC(Util::Config * cfg);
      bool needsLock();
    protected:
      //Private Functions
      bool openStreamSource();
      void closeStreamSource();
      void parseStreamHeader();
      bool setup();
      bool readHeader();
      void getNext(bool smart = true);
      void seek(int seekTime);
      void trackSelect(std::string trackSpec);

      DTSC::File inFile;

      Socket::Connection srcConn;
  };
}

typedef Mist::inputDTSC mistIn;


