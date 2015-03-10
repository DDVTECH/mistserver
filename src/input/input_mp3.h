#include "input.h"
#include <mist/dtsc.h>
#include <deque>

namespace Mist {
  const static double sampleRates[2][3] = {{44.1, 48.0, 32.0}, {22.05, 24.0, 16.0}};
  const static int sampleCounts[2][3] = {{374, 1152, 1152}, {384, 1152, 576}};
  const static int bitRates[2][3][16] = {{{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, -1},
                                         {0, 32, 48, 56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, -1},
                                         {0, 32, 40, 48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, -1}},
                                        {{0, 32, 48, 56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256, -1},
                                         {0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, -1},
                                         {0,  8, 16, 24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, -1}}};
  class inputMP3 : public Input {
    public:
      inputMP3(Util::Config * cfg);
    protected:
      //Private Functions
      bool setup();
      bool readHeader();
      void getNext(bool smart = true);
      void seek(int seekTime);
      void trackSelect(std::string trackSpec);
      double timestamp;

      FILE * inFile;
  };
}

typedef Mist::inputMP3 mistIn;

