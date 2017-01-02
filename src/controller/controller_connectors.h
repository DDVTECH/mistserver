#include <mist/json.h>

namespace Controller {

  /// Checks if the binary mentioned in the protocol argument is currently active, if so, restarts it.
  void UpdateProtocol(std::string protocol);

  /// Checks current protocol configuration, updates state of enabled connectors if neccesary.
  bool CheckProtocols(JSON::Value & p, const JSON::Value & capabilities);

  /// Updates the shared memory page with active connectors
  void saveActiveConnectors();

  /// Reads active connectors from the shared memory pages
  void loadActiveConnectors();


  /// Deletes the shared memory page with connector information
  /// in preparation of shutdown.
  void prepareActiveConnectorsForShutdown();

  /// Forgets all active connectors, preventing them from being killed,
  /// in preparation of reload.
  void prepareActiveConnectorsForReload();

}

