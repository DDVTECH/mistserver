#include <mist/config.h>
#include <mist/json.h>
#include <mist/tinythread.h>
#include <string>

namespace Controller{
  // Functions for current pushes, start/stop/list
  void startPush(const std::string &streamname, std::string &target);
  void stopPush(unsigned int ID);
  void listPush(JSON::Value &output);
  void pushLogMessage(uint64_t id, const JSON::Value & msg);
  void setPushStatus(uint64_t id, const JSON::Value & status);
  bool isPushActive(uint64_t id);

  // Functions for automated pushes, add/remove
  void addPush(JSON::Value &request, JSON::Value &response);
  void removePush(const JSON::Value &request, JSON::Value &response);
  void removeAllPush(const std::string &streamname);

  // internal use only
  void removePush(const JSON::Value &pushInfo);
  void doAutoPush(std::string &streamname);
  void pushCheckLoop(void *np);
  bool isPushActive(const std::string &streamname, const std::string &target);
  void stopActivePushes(const std::string &streamname, const std::string &target);
  bool checkCondition(const JSON::Value &currentValue, const uint8_t &comparisonOperator, const JSON::Value &matchedValue);
  bool checkCondition(const std::string &currentValue, const uint8_t &comparisonOperator, const std::string &matchedValue);
  bool checkCondition(const int64_t &currentValue, const uint8_t &comparisonOperator, const int64_t &matchedValue);

  // for storing/retrieving settings
  void pushSettings(const JSON::Value &request, JSON::Value &response);
}// namespace Controller
