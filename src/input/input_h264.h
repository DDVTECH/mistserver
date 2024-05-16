#include "input.h"
#include <mist/dtsc.h>
#include <mist/procs.h>

namespace Mist{
  class InputH264 : public Input{
  public:
    InputH264(Util::Config *cfg);

  protected:
    virtual bool needHeader(){return false;}
    bool checkArguments();
    void getNext(size_t idx = INVALID_TRACK_ID);
    Socket::Connection myConn;
    std::string ppsInfo;
    std::string spsInfo;
    uint64_t frameCount;
    // Empty defaults
    bool openStreamSource();
    void closeStreamSource(){}
    void parseStreamHeader();
    void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID){}
    bool needsLock(){return false;}
    uint64_t startTime;
    pid_t inputProcess;
    uint32_t waitsSinceData;
    size_t tNumber;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::InputH264 mistIn;
#endif
