#include "output_http.h"

namespace Mist {
  class OutHTTPMinimalServer : public HTTPOutput {
    public:
      OutHTTPMinimalServer(Socket::Connection & conn);
      ~OutHTTPMinimalServer();
      static void init(Util::Config * cfg);
      void onHTTP();
    private:
      std::string resolved_path;
  };
}

typedef Mist::OutHTTPMinimalServer mistOut;

