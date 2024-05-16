#include "output_ts_base.h"
#include <mist/ts_stream.h>
#include <mist/rtp.h>
namespace Mist{
  class OutTS : public TSOutput{
  public:
    OutTS(Socket::Connection &conn);
    ~OutTS();
    static void init(Util::Config *cfg);
    void sendTS(const char *tsData, size_t len = 188);
    static bool listenMode();
    virtual void initialSeek(bool dryRun = false);
    bool isReadyForPlay();
    void onRequest();
    std::string getConnectedHost();
    std::string getConnectedBinHost();

  private:
    size_t udpSize;
    bool pushOut;
    bool wrapRTP;
    bool sendFEC;
    void onRTP(void *socket, const char *data, size_t nbytes);
    std::string packetBuffer;
    Socket::UDPConnection pushSock;
    Socket::UDPConnection fecColumnSock;
    Socket::UDPConnection fecRowSock;
    uint8_t dropPercentage;
    TS::Stream tsIn;
    std::string getStatsName();
    RTP::Packet tsOut;

  protected:
    inline virtual bool keepGoing(){
      return config->is_active && (!listenMode() || myConn);
    }
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutTS mistOut;
#endif
