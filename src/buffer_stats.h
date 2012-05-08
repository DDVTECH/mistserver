#pragma once
#include <string>

namespace Buffer{
  /// Converts a stats line to up, down, host, connector and conntime values.
  class Stats{
    public:
      unsigned int up;
      unsigned int down;
      std::string host;
      std::string connector;
      unsigned int conntime;
      Stats();
      Stats(std::string s);
  };
}
