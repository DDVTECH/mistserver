#include <mist/converter.h>
#include <mist/json.h>

namespace Controller {
  extern JSON::Value capabilities; ///< Global storage of capabilities
  extern Converter::Converter myConverter;
  void checkCapable(JSON::Value & capa);
  void checkAvailProtocols();
}
