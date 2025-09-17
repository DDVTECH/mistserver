#include "output_http.h"

namespace Mist {
  class OutWSRaw : public HTTPOutput {
    public:
      OutWSRaw(Socket::Connection & conn, Util::Config & cfg, JSON::Value & capa);
      static void init(Util::Config *cfg, JSON::Value & capa);
      void respondHTTP(const HTTP::Parser & req, bool headersOnly);
      void sendNext();
      void sendHeader();
      bool doesWebsockets() { return true; }
      void onWebsocketConnect();
      void onWebsocketFrame();

    protected:
      void sendWebsocketCodecData(const std::string & type);
      Util::ResizeablePointer webBuf;

    private:
      bool keysOnly;
  };
} // namespace Mist

typedef Mist::OutWSRaw mistOut;
