#include <mist/json.h>

namespace Controller{

  /// Marks the given protocol as needing a reload (signal USR1) on next check
  void reloadProtocol(size_t indice);

  /// Checks current protocol configuration, updates state of enabled connectors if neccesary.
  bool CheckProtocols(JSON::Value &p, const JSON::Value &capabilities);

  /// Updates the shared memory page with active connectors
  void saveActiveConnectors(bool forceOverride = false);

  /// Reads active connectors from the shared memory pages
  void loadActiveConnectors();

  /// Deletes the shared memory page with connector information
  /// in preparation of shutdown.
  void prepareActiveConnectorsForShutdown();

  /// Forgets all active connectors, preventing them from being killed,
  /// in preparation of reload.
  void prepareActiveConnectorsForReload();

}// namespace Controller
