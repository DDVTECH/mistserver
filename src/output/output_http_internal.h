#include "output_http.h"


namespace Mist {
  class OutHTTP : public HTTPOutput {
    public:
      OutHTTP(Socket::Connection & conn);
      ~OutHTTP();
      static void init(Util::Config * cfg);
      static bool listenMode();
      void onHTTP();
  };
}

typedef Mist::OutHTTP mistOut;
