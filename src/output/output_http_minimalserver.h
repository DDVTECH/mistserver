#include "output_http.h"

namespace Mist{
  class OutHTTPMinimalServer : public HTTPOutput{
  public:
    OutHTTPMinimalServer(Socket::Connection & conn, Util::Config & cfg, JSON::Value & capa);
    ~OutHTTPMinimalServer();
    static void init(Util::Config *cfg, JSON::Value & capa);
    void onHTTP();

  private:
    std::string resolved_path;
  };
}// namespace Mist

typedef Mist::OutHTTPMinimalServer mistOut;
