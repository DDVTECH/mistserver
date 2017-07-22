#include "input.h"
#include <mist/dtsc.h>
#include <mist/procs.h>

namespace Mist{
  class InputH264 : public Input{
  public:
    InputH264(Util::Config *cfg);
    int run();

  protected:
    bool setup();
    void getNext(bool smart = true);
    Socket::Connection myConn;
    std::string ppsInfo;
    std::string spsInfo;
    uint64_t frameCount;
    // Empty defaults
    bool readHeader(){return true;}
    bool openStreamSource(){return true;}
    void closeStreamSource(){}
    void parseStreamHeader(){}
    void seek(int seekTime){}
    void trackSelect(std::string trackSpec){}
    bool needsLock(){return false;}
    uint64_t startTime;
    pid_t inputProcess;
    uint32_t waitsSinceData;
  };
}

typedef Mist::InputH264 mistIn;

