#include "output_http.h"

namespace Mist{
  class OutWAV : public HTTPOutput{
  public:
    OutWAV(Socket::Connection & conn, Util::Config & cfg, JSON::Value & capa);
    static void init(Util::Config *cfg, JSON::Value & capa);
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

typedef Mist::OutWAV mistOut;
