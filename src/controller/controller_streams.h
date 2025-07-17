#include <mist/json.h>

namespace Controller{
  void setProcStatus(uint64_t id, const std::string & proc, const std::string & source, const std::string & sink, const JSON::Value & status);
  void getProcsForStream(const std::string & stream, JSON::Value & returnedProcList);
  void procLogMessage(uint64_t id, const JSON::Value & msg);
  bool isProcActive(uint64_t id);
  bool streamsEqual(JSON::Value &one, JSON::Value &two);
  void checkStream(std::string name, JSON::Value & data);
  void CheckStreams(JSON::Value &in, JSON::Value &out);
  void AddStreams(JSON::Value &in, JSON::Value &out);
  int deleteStream(const std::string &name, JSON::Value &out, bool sourceFileToo = false);
  void checkParameters(JSON::Value &stream);

  struct liveCheck{
    long long int lastms;
    long long int last_active;
  };

}// namespace Controller
