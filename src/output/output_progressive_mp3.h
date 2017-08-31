#include "output_http.h"


namespace Mist {
  class OutProgressiveMP3 : public HTTPOutput {
    public:
      OutProgressiveMP3(Socket::Connection & conn);
      static void init(Util::Config * cfg);
      void onHTTP();
      void sendNext();
      void sendHeader();
    private:
      bool isRecording();
      bool isFileTarget(){return isRecording();}
  };
}

typedef Mist::OutProgressiveMP3 mistOut;
