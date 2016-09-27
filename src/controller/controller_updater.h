/// \file controller_updater.cpp
/// Contains all code for the controller updater.

#include <string>

#ifndef SHARED_SECRET
#define SHARED_SECRET "empty"
#endif

namespace Controller {
  extern JSON::Value updates;

  std::string readFile(std::string filename);
  bool writeFile(std::string filename, std::string & contents);
  JSON::Value CheckUpdateInfo();
  void CheckUpdates();
  void updateComponent(const std::string & component, const std::string & md5sum, Socket::Connection & updrConn);

} //Controller namespace
