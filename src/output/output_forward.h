#include "output.h"

namespace Mist {
  class OutForward : public Output {
    public:
      OutForward(Socket::Connection & conn, Util::Config & cfg, JSON::Value & capa);
      static void init(Util::Config *cfg, JSON::Value & capa);
      void sendNext();
      static bool listenMode(Util::Config *config) { return false; }
      bool isReadyForPlay();

    private:
      bool isRecording();
      bool isFileTarget() { return false; }

    protected:
      inline virtual bool keepGoing() { return Util::Config::is_active; }
  };

} // namespace Mist

typedef Mist::OutForward mistOut;
