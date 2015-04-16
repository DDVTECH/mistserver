#include <mist/json.h>

namespace Controller {
  extern JSON::Value capabilities; ///< Global storage of capabilities
  void checkCapable(JSON::Value & capa);
  void checkAvailProtocols();
}
