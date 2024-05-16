#ifndef INT64_MIN
#define INT64_MIN (-(9223372036854775807##LL) - 1)
#endif

#ifndef INT64_MAX
#define INT64_MAX ((9223372036854775807##LL))
#endif

#include "input.h"
extern "C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace Mist{
  class InputAV : public Input{
  public:
    InputAV(Util::Config *cfg);
    ~InputAV();

  protected:
    // Private Functions
    bool checkArguments();
    bool preRun();
    bool readHeader();
    void getNext(size_t idx = INVALID_TRACK_ID);
    void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID);

  private:
    AVFormatContext *pFormatCtx;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::InputAV mistIn;
#endif
