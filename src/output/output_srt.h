#include "output.h"


namespace Mist {
  class OutProgressiveSRT : public Output {
    public:
      OutProgressiveSRT(Socket::Connection & conn);
      ~OutProgressiveSRT();
      static void init(Util::Config * cfg);
      void onRequest();
      void sendNext();
      void onFail();
      void sendHeader();
    protected:
      bool webVTT;
      int lastNum;
  };
}

typedef Mist::OutProgressiveSRT mistOut;
