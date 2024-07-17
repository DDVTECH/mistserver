#include "input.h"
#include <mist/ts_packet.h>
#include <mist/ts_stream.h>
#include <librist/librist.h>

namespace Mist{

  class InputTSRIST : public Input{
  public:
    InputTSRIST(Util::Config *cfg);
    ~InputTSRIST();
    virtual bool needsLock(){return false;}
    virtual bool publishesTracks(){return false;}
    virtual std::string getConnectedBinHost(){
      return Input::getConnectedBinHost();
    }
    void onFail(const std::string & msg);
    void addData(const char * ptr, size_t len);

  protected:
    // Private Functions
    bool checkArguments();
    bool preRun();
    virtual void getNext(size_t idx = INVALID_TRACK_ID);
    virtual bool needHeader(){return false;}

    bool openStreamSource();
    TS::Stream tsStream; ///< Used for parsing the incoming ts stream
    TS::Packet tsBuf;
    int64_t timeStampOffset;
    uint64_t lastTimeStamp;

    virtual void connStats(Comms::Connections &statComm);

    struct rist_ctx *receiver_ctx;

    bool rawMode;
    Util::ResizeablePointer rawBuffer;
    size_t rawIdx;
    uint64_t lastRawPacket;
    bool hasRaw;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::InputTSRIST mistIn;
#endif
