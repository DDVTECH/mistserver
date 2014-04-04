#include "output.h"


namespace Mist {
  class OutProgressiveMP3 : public Output {
    public:
      OutProgressiveMP3(Socket::Connection & conn);
      ~OutProgressiveMP3();
      static void init(Util::Config * cfg);
      void onRequest();
      void sendNext();
      void onFail();
      void sendHeader();
    protected:
  };
}

typedef Mist::OutProgressiveMP3 mistOut;
