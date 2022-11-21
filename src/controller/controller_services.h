#include <mist/json.h>

namespace Controller{

  /// Marks the given service as needing a reload (signal USR1) on next check
  void reloadService(size_t indice);

  /// Checks current service configuration, updates state of enabled services if neccesary.
  bool CheckService(JSON::Value &p);

  /// Updates the shared memory page with active services
  void saveActiveServices(bool forceOverride = false);

  /// Reads active services from the shared memory pages
  void loadActiveServices();

  /// Deletes the shared memory page with connector information
  /// in preparation of shutdown.
  void prepareActiveServicesForShutdown();

  /// Forgets all active services, preventing them from being killed,
  /// in preparation of reload.
  void prepareActiveServicesForReload();

}// namespace Controller
