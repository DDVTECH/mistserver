#include <mist/ts_stream.h>
#include <mist/defines.h>
#include <mist/json.h>
#include <mist/stream.h>

namespace Mist{
  JSON::Value opt; /// Options

  class ProcLivepeerText{
  public:
    ProcLivepeerText(){};
    bool CheckConfig();
    void Run();
  };

}// namespace Mist

