#include "output_ts_base.h"
#include <mist/ts_stream.h>
#include <mist/url.h>
#include <librist/librist.h>

namespace Mist{
  class OutTSRIST : public TSOutput{
  public:
    OutTSRIST(Socket::Connection &conn);
    ~OutTSRIST();

    static bool listenMode(){return !(config->getString("target").size());}

    static void init(Util::Config *cfg);
    void sendTS(const char *tsData, size_t len = 188);
    bool isReadyForPlay(){return true;}
    virtual void requestHandler();
    static void listener(Util::Config &conf, int (*callback)(Socket::Connection &S));
    std::string getConnectedHost();
    std::string getConnectedBinHost();

  protected:
    virtual void connStats(uint64_t now, Comms::Connections &statComm);
    //virtual std::string getConnectedHost(){
    //  return srtConn.remotehost;
    //}

  private:
    HTTP::URL target;
    int64_t timeStampOffset;
    uint64_t lastTimeStamp;
    uint64_t connTime;
    bool pushOut;
    Util::ResizeablePointer packetBuffer;
    Socket::UDPConnection pushSock;
    TS::Stream tsIn;
    TS::Assembler assembler;
    
    //RIST specific vars
    struct rist_ctx *sender_ctx;
    struct rist_peer *peer;

  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutTSRIST mistOut;
#endif
