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
