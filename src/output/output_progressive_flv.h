#include "output_http.h"


namespace Mist {
  class OutProgressiveFLV : public HTTPOutput {
    public:
      OutProgressiveFLV(Socket::Connection & conn);
      ~OutProgressiveFLV();
      static void init(Util::Config * cfg);
      void onHTTP();
      void sendNext();
      void sendHeader();
    private:
      FLV::Tag tag;
  };
}

typedef Mist::OutProgressiveFLV mistOut;
