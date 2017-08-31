#include "output_http.h"


namespace Mist {
  class OutProgressiveFLV : public HTTPOutput {
    public:
      OutProgressiveFLV(Socket::Connection & conn);
      static void init(Util::Config * cfg);
      void onHTTP();
      void sendNext();
      void sendHeader();
    private:
      FLV::Tag tag;
      bool isRecording();
      bool isFileTarget(){return isRecording();}
  };
}

typedef Mist::OutProgressiveFLV mistOut;
