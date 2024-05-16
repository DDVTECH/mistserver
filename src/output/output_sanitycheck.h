#include "output.h"
#include <list>

namespace Mist{
  class OutSanityCheck : public Output{
  public:
    OutSanityCheck(Socket::Connection &conn);
    static void init(Util::Config *cfg);
    void sendNext();
    void sendHeader();
    static bool listenMode(){return false;}

  protected:
    void writeContext();
    bool syncMode;
    std::deque<std::string> packets;
    Util::packetSorter sortSet;
    std::map<size_t, uint64_t> trkTime;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutSanityCheck mistOut;
#endif
