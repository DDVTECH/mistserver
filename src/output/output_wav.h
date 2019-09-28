#include "output_http.h"


namespace Mist {
  class OutWAV : public HTTPOutput {
    public:
      OutWAV(Socket::Connection & conn);
      static void init(Util::Config * cfg);
      void onHTTP();
      void sendNext();
      void sendHeader();
    protected:
      virtual bool inlineRestartCapable() const{return true;}
    private:
      bool isRecording();
      bool isFileTarget(){return isRecording();}
  };
}

typedef Mist::OutWAV mistOut;

