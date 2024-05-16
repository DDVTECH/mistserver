#include "output_http.h"

namespace Mist{
  class OutMP3 : public HTTPOutput{
  public:
    OutMP3(Socket::Connection &conn);
    static void init(Util::Config *cfg);
    void onHTTP();
    void sendNext();
    void sendHeader();

  private:
    bool isRecording();
    bool isFileTarget(){return isRecording();}
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutMP3 mistOut;
#endif
