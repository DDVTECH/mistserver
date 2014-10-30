#include "output_http.h"


namespace Mist {
  class OutProgressiveMP3 : public HTTPOutput {
    public:
      OutProgressiveMP3(Socket::Connection & conn);
      ~OutProgressiveMP3();
      static void init(Util::Config * cfg);
      void onHTTP();
      void sendNext();
      void sendHeader();
  };
}

typedef Mist::OutProgressiveMP3 mistOut;
