#include "output.h"


namespace Mist {
  class OutRaw : public Output {
    public:
      OutRaw(Socket::Connection & conn);
      ~OutRaw();
      static void init(Util::Config * cfg);
      void sendNext();
      void sendHeader();
  };
}

typedef Mist::OutRaw mistOut;
