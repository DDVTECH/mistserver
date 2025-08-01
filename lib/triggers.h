#pragma once
#include <functional>
#include <string>

namespace Triggers {
  static const std::string empty;

  bool doTrigger(const std::string & triggerType, const std::string & payload, const std::string & streamName,
                 bool dryRun, std::string & response, std::function<bool(const char *)> paramsCB = 0);

  std::string handleTrigger(const std::string & triggerType, const std::string & value, const std::string & payload,
                            int sync, const std::string & defaultResponse);

  // All of the below are just shorthands for specific usage of the doTrigger function above:

  bool shouldTrigger(const std::string & triggerType, const std::string & streamName = empty,
                     std::function<bool(const char *)> paramsCB = 0);

  bool doTrigger(const std::string & triggerType, const std::string & payload = empty, const std::string & streamName = empty);
} // namespace Triggers
