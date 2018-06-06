#include "output_http.h"
#include <mist/websocket.h>

namespace Mist {
  class OutJSON : public HTTPOutput {
    public:
      OutJSON(Socket::Connection & conn);
      ~OutJSON();
      static void init(Util::Config * cfg);
      void onHTTP();
      bool onFinish();
      void onFail();
      void sendNext();
      void sendHeader();
    protected:
      JSON::Value lastVal;
      bool keepReselecting;
      std::string jsonp;
      bool dupcheck;
      std::set<std::string> nodup;
      bool first;
      HTTP::Websocket * ws;
  };
}

typedef Mist::OutJSON mistOut;
