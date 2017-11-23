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
      uint32_t filter_from;
      uint32_t filter_to;
      uint32_t index;
  };
}

typedef Mist::OutProgressiveSRT mistOut;
