#include "output_http.h"

namespace Mist{
  class OutJPG : public HTTPOutput{
  public:
    OutJPG(Socket::Connection & conn, Util::Config & cfg, JSON::Value & capa);
    static void init(Util::Config *cfg, JSON::Value & capa);
    void respondHTTP(const HTTP::Parser & req, bool headersOnly);
    void sendNext();
    bool isReadyForPlay();
  protected:
    virtual bool isFileTarget(){return isRecording();}
    virtual bool inlineRestartCapable() const{return true;}
    bool motion;
    std::string boundary;
  };
}// namespace Mist

typedef Mist::OutJPG mistOut;

