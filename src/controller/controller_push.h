#include <mist/config.h>
#include <mist/json.h>
#include <mist/tinythread.h>
#include <string>

namespace Controller{
  // Functions for current pushes, start/stop/list
  void startPush(const std::string &streamname, std::string &target);
  void stopPush(unsigned int ID);
  void listPush(JSON::Value &output);

  // Functions for automated pushes, add/remove
  void addPush(JSON::Value &request);
  void removePush(const JSON::Value &request);
  void removeAllPush(const std::string &streamname);

  // internal use only
  void doAutoPush(std::string &streamname);
  void pushCheckLoop(void *np);
  bool isPushActive(const std::string &streamname, const std::string &target);

  // for storing/retrieving settings
  void pushSettings(const JSON::Value &request, JSON::Value &response);
}

