#include "output_http.h"

namespace Mist{
  class OutWAV : public HTTPOutput{
  public:
    OutWAV(Socket::Connection &conn);
    static void init(Util::Config *cfg);
    void onHTTP();
    void sendNext();
    void sendHeader();

  protected:
    virtual bool inlineRestartCapable() const{return true;}

  private:
    bool isRecording();
    bool isFileTarget(){return isRecording();}
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutWAV mistOut;
#endif
