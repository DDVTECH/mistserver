#pragma once
#include <GeoIP.h>
#include <mist/json.h>
#include <map>
#include <string>

#define GEOIPV4 "/usr/share/GeoIP/GeoIP.dat"
#define GEOIPV6 "/usr/share/GeoIP/GeoIPv6.dat"

namespace Controller{
  void checkStreamLimits(std::string streamName, long long currentKbps, long long connectedUsers);
  void checkServerLimits();
  bool isBlacklisted(std::string host, std::string streamName, int timeConnected);
  std::string hostLookup(std::string ip);
  bool onList(std::string ip, std::string list);
  std::string getCountry(std::string ip);
}
