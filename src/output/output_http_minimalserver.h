#include "output_http.h"

namespace Mist{
  class OutHTTPMinimalServer : public HTTPOutput{
  public:
    OutHTTPMinimalServer(Socket::Connection &conn);
    ~OutHTTPMinimalServer();
    static void init(Util::Config *cfg);
    void onHTTP();

  private:
    std::string resolved_path;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutHTTPMinimalServer mistOut;
#endif
