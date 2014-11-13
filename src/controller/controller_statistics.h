#include <mist/shared_memory.h>
#include <mist/timing.h>
#include <mist/defines.h>
#include <mist/json.h>
#include <string>
#include <map>


namespace Controller {
  struct statLog {
    long time;
    long lastSecond;
    long long down;
    long long up;
  };

  class statStorage {
    public:
      void update(IPC::statExchange & data);
      std::string host;
      unsigned int crc;
      std::string streamName;
      std::string connector;
      std::map<unsigned long long, statLog> log;
  };

  
  extern std::multimap<unsigned long long int, statStorage> oldConns;
  extern std::map<unsigned long, statStorage> curConns;
  void parseStatistics(char * data, size_t len, unsigned int id);
  void fillClients(JSON::Value & req, JSON::Value & rep);
  void fillActive(JSON::Value & req, JSON::Value & rep);
  void fillTotals(JSON::Value & req, JSON::Value & rep);
  void SharedMemStats(void * config);
  bool hasViewers(std::string streamName);
}

