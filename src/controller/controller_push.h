#include <string>
#include <mist/json.h>
#include <mist/config.h>
#include <mist/tinythread.h>

namespace Controller {
  //Functions for current pushes, start/stop/list
  void startPush(std::string & streamname, std::string & target);
  void stopPush(unsigned int ID);
  void listPush(JSON::Value & output);

  //Functions for automated pushes, add/remove
  void addPush(JSON::Value & request);
  void removePush(const JSON::Value & request);
  void removePush(const std::string & streamname);

  void doAutoPush(std::string & streamname);
}

