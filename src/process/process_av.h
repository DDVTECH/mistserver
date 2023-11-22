#include <mist/defines.h>
#include <mist/json.h>

namespace Mist{
  bool getFirst = false;
  bool sendFirst = false;

  uint64_t packetTimeDiff;
  uint64_t sendPacketTime;
  JSON::Value opt; /// Options

  class ProcAV{
  public:
    ProcAV(){};
    bool CheckConfig();
    void Run();
  };

}// namespace Mist
