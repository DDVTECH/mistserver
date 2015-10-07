#pragma once
#include <string>

namespace Triggers{  
  bool doTrigger(const std::string triggerType, const std::string &payload, const std::string &streamName, bool dryRun, std::string & response);
  std::string handleTrigger(const std::string &triggerType, const std::string &value, const std::string &payload, int sync);

  //All of the below are just shorthands for specific usage of the doTrigger function above:
  bool shouldTrigger(const std::string triggerType);
  bool shouldTrigger(const std::string triggerType, const std::string &streamName);
  bool doTrigger(const std::string triggerType);
  bool doTrigger(const std::string triggerType, const std::string &payload);
  bool doTrigger(const std::string triggerType, const std::string &payload, const std::string &streamName);
}
