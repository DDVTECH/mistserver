#include "output_http.h"

namespace Mist{
  class OutH264 : public HTTPOutput{
  public:
    OutH264(Socket::Connection &conn);
    static void init(Util::Config *cfg);
    void onHTTP();
    void sendNext();
    void sendHeader();

  private:
    bool isRecording();
  };
}

typedef Mist::OutH264 mistOut;
