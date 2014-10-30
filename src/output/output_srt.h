#include "output_http.h"


namespace Mist {
  class OutProgressiveSRT : public HTTPOutput {
    public:
      OutProgressiveSRT(Socket::Connection & conn);
      ~OutProgressiveSRT();
      static void init(Util::Config * cfg);
      void onHTTP();
      void sendNext();
      void sendHeader();
    protected:
      bool webVTT;
      int lastNum;
  };
}

typedef Mist::OutProgressiveSRT mistOut;
