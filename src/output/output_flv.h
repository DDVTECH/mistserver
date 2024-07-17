#include "output_http.h"

namespace Mist{
  class OutFLV : public HTTPOutput{
  public:
    OutFLV(Socket::Connection &conn);
    static void init(Util::Config *cfg);
    void onHTTP();
    void sendNext();
    void sendHeader();

  private:
    virtual bool inlineRestartCapable() const{return true;}
    FLV::Tag tag;
    bool isRecording();
    bool isFileTarget(){return isRecording();}
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutFLV mistOut;
#endif
