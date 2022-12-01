#include "input.h"
#include <mist/dtsc.h>
#include <mist/nal.h>
#include <mist/socket_srt.h>
#include <mist/ts_packet.h>
#include <mist/ts_stream.h>
#include <set>
#include <string>

namespace Mist{

  class inputTSSRT : public Input{
  public:
    inputTSSRT(Util::Config *cfg, Socket::SRTConnection s = Socket::SRTConnection());
    ~inputTSSRT();
    void setSingular(bool newSingular);
    virtual bool needsLock();
    virtual std::string getConnectedBinHost(){
      if (srtConn){return srtConn.getBinHost();}
      return Input::getConnectedBinHost();
    }

  protected:
    // Private Functions
    bool checkArguments();
    bool preRun();
    virtual void getNext(size_t idx = INVALID_TRACK_ID);
    virtual bool needHeader(){return false;}
    virtual bool isSingular(){return singularFlag;}
    virtual bool isThread(){return !singularFlag;}
    virtual bool publishesTracks(){return false;}

    bool openStreamSource();
    void streamMainLoop();
    TS::Stream tsStream; ///< Used for parsing the incoming ts stream
    TS::Packet tsBuf;
    TS::Assembler assembler;
    int64_t timeStampOffset;
    uint64_t lastTimeStamp;

    Socket::SRTConnection srtConn;
    bool singularFlag;
    virtual void connStats(Comms::Connections &statComm);

    Util::ResizeablePointer rawBuffer;
    size_t rawIdx;
    uint64_t lastRawPacket;
  };
}// namespace Mist

typedef Mist::inputTSSRT mistIn;
