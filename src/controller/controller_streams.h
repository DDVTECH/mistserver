#include <mist/json.h>

namespace Controller {
  extern std::map<std::string, int> lastBuffer; ///< Last moment of contact with all buffers.

  bool streamsEqual(JSON::Value & one, JSON::Value & two);
  void startStream(std::string name, JSON::Value & data);
  void CheckAllStreams(JSON::Value & data);
  void CheckStreams(JSON::Value & in, JSON::Value & out);
} //Controller namespace
