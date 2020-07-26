#include "input.h"
#include <mist/dtsc.h>
#include <mist/nal.h>
#include <mist/socket_srt.h>
#include <mist/ts_packet.h>
#include <mist/ts_stream.h>
#include <set>
#include <string>

namespace Mist{
  /// This class contains all functions needed to implement TS Input
  class inputTSSRT : public Input{
  public:
    inputTSSRT(Util::Config *cfg, SRTSOCKET s = -1);
    ~inputTSSRT();
    void setSingular(bool newSingular);
    virtual bool needsLock();

  protected:
    // Private Functions
    bool checkArguments();
    bool preRun();
    virtual void getNext(size_t idx = INVALID_TRACK_ID);
    virtual bool needHeader(){return false;}
    virtual bool preventBufferStart(){return srtConn.getSocket() == -1;}
    virtual bool isSingular(){return singularFlag;}
    virtual bool isThread(){return !singularFlag;}

    bool openStreamSource();
    void parseStreamHeader();
    void streamMainLoop();
    TS::Stream tsStream; ///< Used for parsing the incoming ts stream
    TS::Packet tsBuf;
    std::string leftBuffer;
    uint64_t lastTimeStamp;

    Socket::SRTServer sSock;
    Socket::SRTConnection srtConn;
    bool singularFlag;
    size_t tmpIdx;
    virtual size_t streamByteCount(){
      return srtConn.dataDown();
    }; // For live streams: to update the stats with correct values.
    virtual void handleLossyStats(Comms::Statistics &statComm);
  };
}// namespace Mist

typedef Mist::inputTSSRT mistIn;
