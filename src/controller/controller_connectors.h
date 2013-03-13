#include <mist/json.h>

namespace Controller {

  /// Checks if the binary mentioned in the protocol argument is currently active, if so, restarts it.
  void UpdateProtocol(std::string protocol);

  /// Checks current protocol configuration, updates state of enabled connectors if neccesary.
  void CheckProtocols(JSON::Value & p);

}
