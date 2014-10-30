#include "output_http.h"


namespace Mist {
  class OutJSON : public HTTPOutput {
    public:
      OutJSON(Socket::Connection & conn);
      ~OutJSON();
      static void init(Util::Config * cfg);
      void onHTTP();
      bool onFinish();
      void sendNext();
      void sendHeader();
    protected:
      std::string jsonp;
      bool first;
  };
}

typedef Mist::OutJSON mistOut;
