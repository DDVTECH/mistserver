#include <mist/shared_memory.h>
#include <mist/timing.h>
#include <mist/defines.h>
#include <mist/json.h>
#include <mist/tinythread.h>
#include <string>
#include <map>

/// The STAT_CUTOFF define sets how many seconds of statistics history is kept.
#define STAT_CUTOFF 600


namespace Controller {
  
  extern bool killOnExit;
  
  struct statLog {
    long time;
    long lastSecond;
    long long down;
    long long up;
  };

  /// This is a comparison and storage class that keeps sessions apart from each other.
  /// Whenever two of these objects are not equal, it will create a new session.
  class sessIndex {
    public:
      sessIndex(std::string host, unsigned int crc, std::string streamName, std::string connector);
      sessIndex(IPC::statExchange & data);
      sessIndex();
      std::string host;
      unsigned int crc;
      std::string streamName;
      std::string connector;
      
      bool operator== (const sessIndex &o) const;
      bool operator!= (const sessIndex &o) const;
      bool operator> (const sessIndex &o) const;
      bool operator<= (const sessIndex &o) const;
      bool operator< (const sessIndex &o) const;
      bool operator>= (const sessIndex &o) const;
  };
  
  
  class statStorage {
    public:
      void update(IPC::statExchange & data);
      bool hasDataFor(unsigned long long);
      statLog & getDataFor(unsigned long long);
      std::map<unsigned long long, statLog> log;
  };
  
  /// A session class that keeps track of both current and archived connections.
  /// Allows for moving of connections to another session.
  class statSession {
    private:
      unsigned long long firstSec;
      unsigned long long lastSec;
      std::deque<statStorage> oldConns;
      std::map<unsigned long, statStorage> curConns;
      char sync;
    public:
      statSession();
      void wipeOld(unsigned long long);
      void finish(unsigned long index);
      void switchOverTo(statSession & newSess, unsigned long index);
      void update(unsigned long index, IPC::statExchange & data);
      unsigned long long getStart();
      unsigned long long getEnd();
      bool isViewerOn(unsigned long long time);
      bool isViewer();
      bool hasDataFor(unsigned long long time);
      bool hasData();
      long long getConnTime(unsigned long long time);
      long long getLastSecond(unsigned long long time);
      long long getDown(unsigned long long time);
      long long getUp(unsigned long long time);
      long long getBpsDown(unsigned long long time);
      long long getBpsUp(unsigned long long time);
      long long getBpsDown(unsigned long long start, unsigned long long end);
      long long getBpsUp(unsigned long long start, unsigned long long end);
  };

  
  extern std::map<sessIndex, statSession> sessions;
  extern std::map<unsigned long, sessIndex> connToSession;
  extern tthread::mutex statsMutex;
  void parseStatistics(char * data, size_t len, unsigned int id);
  void killStatistics(char * data, size_t len, unsigned int id);
  void fillClients(JSON::Value & req, JSON::Value & rep);
  void fillActive(JSON::Value & req, JSON::Value & rep, bool onlyNow = false);
  void fillTotals(JSON::Value & req, JSON::Value & rep);
  void SharedMemStats(void * config);
  bool hasViewers(std::string streamName);
}

