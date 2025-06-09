#include "output_http.h"

namespace Mist {
  class OutAAC : public HTTPOutput {
    public:
      OutAAC(Socket::Connection & conn, Util::Config & cfg, JSON::Value & capa);
      static void init(Util::Config *cfg, JSON::Value & capa);
      void respondHTTP(const HTTP::Parser & req, bool headersOnly);
      void sendNext();
      void initialSeek(bool dryRun = false);

    private:
      virtual bool inlineRestartCapable() const { return true; }
      bool isFileTarget() { return isRecording(); }
  };
} // namespace Mist

typedef Mist::OutAAC mistOut;
