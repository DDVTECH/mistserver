#include "output_ts_base.h"
#include <mist/ts_stream.h>
#include <mist/socket_srt.h>

namespace Mist{
  class OutTSSRT : public TSOutput{
  public:
    OutTSSRT(Socket::Connection &conn, Socket::SRTConnection * _srtSock = 0);
    ~OutTSSRT();

    static bool listenMode();
    static void listener(Util::Config & conf,
                         std::function<void(Socket::Connection &, Socket::Server &)> callback);

    static void init(Util::Config *cfg);
    void sendTS(const char *tsData, size_t len = 188);
    virtual void requestHandler(bool readable);
    virtual bool onFinish();
    virtual uint64_t getInitialSeekPosition();
    inline virtual bool keepGoing(){return config->is_active;}
  protected:
    virtual void connStats(uint64_t now, Comms::Connections &statComm);
    virtual std::string getConnectedHost(){return srtConn?srtConn->remoteaddr.host():"";}
    virtual std::string getConnectedBinHost(){return srtConn?srtConn->getBinHost():"";}
    virtual bool dropPushTrack(uint32_t trackId, const std::string & dropReason);
  private:
    virtual bool isRecording();
    HTTP::URL target;
    int64_t timeStampOffset;
    uint64_t lastTimeStamp;
    uint64_t lastWorked;
    bool pushOut;
    Util::ResizeablePointer packetBuffer;
    TS::Stream tsIn;
    TS::Assembler assembler;
    bool bootMSOffsetCalculated;

    Socket::SRTConnection * srtConn;
    Socket::UDPConnection * udpInit;
  };
}// namespace Mist

typedef Mist::OutTSSRT mistOut;
