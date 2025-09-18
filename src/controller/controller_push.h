#include <mist/config.h>
#include <mist/json.h>
#include <string>

namespace Controller{
  // Helper functions for converting
  JSON::Value makePushObject(const JSON::Value & input);

  // Functions for current pushes, start/stop/list
  void startPush(const std::string &streamname, std::string &target);
  void stopPush(unsigned int ID);
  void stopPush(const std::string & stream);
  void stopPushGraceful(unsigned int ID);
  void stopPushGraceful(const std::string & stream);
  void listPush(JSON::Value &output);
  void pushLogMessage(uint64_t id, const JSON::Value & msg);
  void setPushStatus(uint64_t id, const JSON::Value & status);
  bool isPushActive(uint64_t id);

  // Functions for automated pushes, add/remove
  void addPush(const JSON::Value &request, JSON::Value &response);
  void removeAllPush(const std::string &streamname);

  // Main loop related functions
  void initPushCheck();
  size_t runPushCheck();
  void deinitPushCheck();

  // internal use only
  void removePush(const JSON::Value &pushInfo);
  void doAutoPush(std::string & streamname);
  bool isPushActive(const std::string &streamname, const std::string &target);
  void stopActivePushes(const std::string &streamname, const std::string &target);
  bool checkCondition(const JSON::Value &currentValue, const uint8_t &comparisonOperator, const JSON::Value &matchedValue);
  bool checkCondition(const std::string &currentValue, const uint8_t &comparisonOperator, const std::string &matchedValue);
  bool checkCondition(const int64_t &currentValue, const uint8_t &comparisonOperator, const int64_t &matchedValue);

  // for storing/retrieving settings
  void pushSettings(const JSON::Value &request, JSON::Value &response);
}// namespace Controller
