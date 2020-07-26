#include "output_ts_base.h"
#include <mist/ts_stream.h>

#include <mist/socket_srt.h>

namespace Mist{
  class OutTSSRT : public TSOutput{
  public:
    OutTSSRT(Socket::Connection &conn, SRTSOCKET _srtSock);
    ~OutTSSRT();

    static bool listenMode(){return !(config->getString("target").size());}

    static void init(Util::Config *cfg);
    void sendTS(const char *tsData, size_t len = 188);
    bool isReadyForPlay(){return true;}

  protected:
    // Stats handling
    virtual bool setAlternateConnectionStats(Comms::Statistics &statComm);
    virtual void handleLossyStats(Comms::Statistics &statComm);

  private:
    bool pushOut;
    std::string packetBuffer;
    Socket::UDPConnection pushSock;
    TS::Stream tsIn;

    Socket::SRTConnection srtConn;
  };
}// namespace Mist

typedef Mist::OutTSSRT mistOut;
