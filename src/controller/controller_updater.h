/// \file controller_updater.h
/// Contains all code for the controller updater.

#include <mist/json.h>

namespace Controller {
  extern bool updateAfterNextCheck;
  size_t updaterCheck();
  void rollingUpdate();
  void abortUpdate();
  void insertUpdateInfo(JSON::Value & ret);
} // namespace Controller
