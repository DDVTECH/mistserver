#include "input.h"
#include <mist/dtsc.h>
#include <mist/nal.h>
#include <mist/socket_srt.h>
#include <mist/ts_packet.h>
#include <mist/ts_stream.h>
#include <string>

namespace Mist{

  class InputTSSRT : public Input{
  public:
    InputTSSRT(Util::Config *cfg, Socket::SRTConnection s = Socket::SRTConnection());
    ~InputTSSRT();
    void setSingular(bool newSingular);
    virtual bool needsLock() override;
    virtual std::string getConnectedBinHost() override {
      if (srtConn && *srtConn){return srtConn->getBinHost();}
      return Input::getConnectedBinHost();
    }
    virtual void onCrash() override;

  protected:
    // Private Functions
    bool checkArguments() override;
    bool preRun() override;
    virtual void getNext(size_t idx = INVALID_TRACK_ID) override;
    virtual bool needHeader() override { return false; }
    virtual bool isSingular() override { return singularFlag; }
    virtual bool isThread() override { return !singularFlag; }
    virtual bool publishesTracks() override { return false; }

    bool openStreamSource() override;
    void streamMainLoop() override;
    TS::Stream tsStream; ///< Used for parsing the incoming ts stream
    TS::Packet tsBuf;
    TS::Assembler assembler;
    int64_t timeStampOffset;
    uint64_t lastTimeStamp;

    Socket::SRTConnection * srtConn;
    Socket::UDPConnection * udpInit;
    bool singularFlag;
    virtual void connStats(Comms::Connections & statComm) override;

    Util::ResizeablePointer rawBuffer;
    size_t rawIdx;
    uint64_t lastRawPacket;
    bool bootMSOffsetCalculated;
  };
}// namespace Mist

typedef Mist::InputTSSRT mistIn;
