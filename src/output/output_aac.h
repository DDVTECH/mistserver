#include "output_http.h"

namespace Mist{
  class OutAAC : public HTTPOutput{
  public:
    OutAAC(Socket::Connection &conn);
    static void init(Util::Config *cfg);
    void respondHTTP(const HTTP::Parser & req, bool headersOnly);
    void sendNext();
    void initialSeek();

  private:
    bool isFileTarget(){return isRecording();}
  };
}// namespace Mist

typedef Mist::OutAAC mistOut;
