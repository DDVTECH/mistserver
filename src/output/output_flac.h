#include "output_http.h"

namespace Mist{
  class OutFLAC : public HTTPOutput{
  public:
    OutFLAC(Socket::Connection &conn);
    static void init(Util::Config *cfg);
    virtual void respondHTTP(const HTTP::Parser &req, bool headersOnly);
    void sendNext();
    void sendHeader();

  private:
    bool isFileTarget(){return isRecording();}
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutFLAC mistOut;
#endif
