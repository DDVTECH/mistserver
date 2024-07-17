#include "input.h"
#include <mist/dtsc.h>
#include <mist/nal.h>
#include <mist/ts_packet.h>
#include <mist/ts_stream.h>
#include <mist/urireader.h>
#include <set>
#include <string>

namespace Mist{
  /// This class contains all functions needed to implement TS Input
  class InputTS : public Input, public Util::DataCallback{
  public:
    InputTS(Util::Config *cfg);
    ~InputTS();

    // This function can simply check standAlone because we ensure it's set in checkArguments,
    // which is always called before the first call to needsLock
    virtual bool needsLock(){return standAlone && Input::needsLock();}

    virtual std::string getConnectedBinHost(){
      if (udpMode){return udpCon.getBinDestination();}
      return reader.getBinHost();
    }
    virtual bool publishesTracks(){return false;}
    virtual void dataCallback(const char *ptr, size_t size);
    virtual size_t getDataCallbackPos() const;
  protected:
    // Private Functions
    bool checkArguments();
    bool preRun();
    bool readHeader();
    virtual bool needHeader();
    virtual void postHeader();
    virtual void getNext(size_t idx = INVALID_TRACK_ID);
    void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID);
    void readPMT();
    bool openStreamSource();
    void streamMainLoop();
    void finish();
    TS::Assembler assembler;
    Util::ResizeablePointer liveReadBuffer;
    TS::Stream tsStream; ///< Used for parsing the incoming ts stream
    Socket::UDPConnection udpCon;
    HTTP::URIReader reader;
    TS::Packet tsBuf;
    pid_t inputProcess;
    bool isFinished;

    bool udpMode;
    bool rawMode;
    size_t rawIdx;
    uint64_t lastRawPacket;
    uint64_t readPos;
    bool unitStartSeen;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::InputTS mistIn;
#endif
