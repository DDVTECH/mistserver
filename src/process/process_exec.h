#include "../input/input_ebml.h"
#include "../output/output_ebml.h"
#include <mist/defines.h>
#include <mist/json.h>
#include <mist/stream.h>

namespace Mist{
  bool getFirst = false;
  bool sendFirst = false;

  int64_t packetTimeDiff = 0;
  uint64_t sendPacketTime = 0;
  JSON::Value opt; /// Options

  class ProcMKVExec{
  public:
    ProcMKVExec(){};
    bool CheckConfig();
    void Run();
  };

}// namespace Mist
