#pragma once
#include <map>
#include <mist/json.h>
#include <string>

namespace Controller{
  void checkStreamLimits(std::string streamName, long long currentKbps, long long connectedUsers);
  void checkServerLimits();
  bool isBlacklisted(std::string host, std::string streamName, int timeConnected);
  std::string hostLookup(std::string ip);
  bool onList(std::string ip, std::string list);
  std::string getCountry(std::string ip);
}// namespace Controller
