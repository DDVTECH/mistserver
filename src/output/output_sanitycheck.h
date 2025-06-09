#include "output.h"
#include <list>

namespace Mist{
  class OutSanityCheck : public Output{
  public:
    OutSanityCheck(Socket::Connection & conn, Util::Config & cfg, JSON::Value & capa);
    static void init(Util::Config *cfg, JSON::Value & capa);
    void sendNext();
    void sendHeader();
    static bool listenMode(Util::Config *) { return false; }

  protected:
    void writeContext();
    bool syncMode;
    std::deque<std::string> packets;
    Util::packetSorter sortSet;
    std::map<size_t, uint64_t> trkTime;
  };
}// namespace Mist

typedef Mist::OutSanityCheck mistOut;
