#include "output_http.h"

namespace Mist{
  class OutAAC : public HTTPOutput{
  public:
    OutAAC(Socket::Connection &conn);
    static void init(Util::Config *cfg);
    void onHTTP();
    void sendNext();
    void sendHeader();
    void initialSeek();

  private:
    bool isRecording();
    bool isFileTarget(){return isRecording();}
  };
}// namespace Mist

typedef Mist::OutAAC mistOut;
