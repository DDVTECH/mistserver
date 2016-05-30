#include <mist/json.h>

namespace Controller {
  bool streamsEqual(JSON::Value & one, JSON::Value & two);
  void checkStream(std::string name, JSON::Value & data);
  bool CheckAllStreams(JSON::Value & data);
  void CheckStreams(JSON::Value & in, JSON::Value & out);
  void AddStreams(JSON::Value & in, JSON::Value & out);
  void deleteStream(const std::string & name, JSON::Value & out);

  struct liveCheck {
    long long int lastms;
    long long int last_active;
  };

} //Controller namespace
