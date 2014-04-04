#include "output.h"


namespace Mist {
  class OutProgressiveFLV : public Output {
    public:
      OutProgressiveFLV(Socket::Connection & conn);
      ~OutProgressiveFLV();
      static void init(Util::Config * cfg);
      void onRequest();
      void sendNext();
      void onFail();
      void sendHeader();
    protected:
  };
}

typedef Mist::OutProgressiveFLV mistOut;
