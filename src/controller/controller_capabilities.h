#include <mist/json.h>

namespace Controller{
  extern JSON::Value capabilities; ///< Global storage of capabilities
  size_t updateLoad();
  void checkSpecs();
  void checkCapable(JSON::Value &capa, bool minimal = false);
  void checkAvailProtocols();
  void checkAvailTriggers(); /*LTS*/
  size_t getMemTotal();
  size_t getMemUsed();
  size_t getShmTotal();
  size_t getShmUsed();
  size_t getCpuUse();
}// namespace Controller
