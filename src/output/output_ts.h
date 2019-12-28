#include "output_ts_base.h"
#include <mist/ts_stream.h>

namespace Mist{
  class OutTS : public TSOutput{
  public:
    OutTS(Socket::Connection &conn);
    ~OutTS();
    static void init(Util::Config *cfg);
    void sendTS(const char *tsData, size_t len = 188);
    static bool listenMode();
    virtual void initialSeek();
    bool isReadyForPlay();
    void onRequest();

  private:
    size_t udpSize;
    bool pushOut;
    std::string packetBuffer;
    Socket::UDPConnection pushSock;
    TS::Stream tsIn;
    std::string getStatsName();
  };
}// namespace Mist

typedef Mist::OutTS mistOut;
