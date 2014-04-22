#ifndef INT64_MIN
#define INT64_MIN (-(9223372036854775807 ## LL)-1)
#endif

#ifndef INT64_MAX
#define INT64_MAX ((9223372036854775807 ## LL))
#endif

#include "input.h"
extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
}

namespace Mist {
  class inputAV : public Input {
    public:
      inputAV(Util::Config * cfg);
      ~inputAV();
    protected:
      //Private Functions
      bool setup();
      bool readHeader();
      void getNext(bool smart = true);
      void seek(int seekTime);
      void trackSelect(std::string trackSpec);
    private:
      AVFormatContext *pFormatCtx;
  };
}

typedef Mist::inputAV mistIn;
