#pragma once
#include <string>

namespace Triggers{

  static const std::string empty;

  bool doTrigger(const std::string & triggerType, const std::string &payload, const std::string &streamName, bool dryRun, std::string &response, bool paramsCB(const char *, const void *) = 0, const void * extraParam = 0);
  std::string handleTrigger(const std::string &triggerType, const std::string &value, const std::string &payload, int sync);

  // All of the below are just shorthands for specific usage of the doTrigger function above:
  bool shouldTrigger(const std::string & triggerType, const std::string &streamName = empty, bool paramsCB(const char *, const void *) = 0, const void * extraParam = 0);
  bool doTrigger(const std::string & triggerType, const std::string & payload = empty, const std::string & streamName = empty);
}

