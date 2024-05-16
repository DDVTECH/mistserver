#include "output.h"

namespace Mist{
  class OutJSONLine : public Output{
  public:
    OutJSONLine(Socket::Connection &conn);
    ~OutJSONLine();
    static void init(Util::Config *cfg);
    void sendNext();
    static bool listenMode();
    bool isReadyForPlay();
    void requestHandler();
  private:
    std::string getStatsName();
    Util::ResizeablePointer dPtr;
    size_t trkIdx;
    uint64_t offset;

  protected:
    inline virtual bool keepGoing(){
      return config->is_active && (!listenMode() || myConn);
    }
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutJSONLine mistOut;
#endif
