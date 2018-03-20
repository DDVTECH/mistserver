#pragma once
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
  struct statLog {
    long time;
    long lastSecond;
    long long down;
    long long up;
  };

  enum sessType {
    SESS_UNSET = 0,
    SESS_INPUT,
    SESS_OUTPUT,
    SESS_VIEWER
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
      std::string toStr();
  };
  
  
  class statStorage {
    private:
      long long removeUp;
      long long removeDown;
    public:
      statStorage();
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
      sessType sessionType;
    public:
      statSession();
      std::map<unsigned long, statStorage> curConns;
      void wipeOld(unsigned long long);
      void finish(unsigned long index);
      void switchOverTo(statSession & newSess, unsigned long index);
      void update(unsigned long index, IPC::statExchange & data);
      void ping(const sessIndex & index, unsigned long long disconnectPoint);
      unsigned long long getStart();
      unsigned long long getEnd();
      bool hasDataFor(unsigned long long time);
      bool hasData();
      long long getConnTime(unsigned long long time);
      long long getLastSecond(unsigned long long time);
      long long getDown(unsigned long long time);
      long long getUp();
      long long getDown();
      long long getUp(unsigned long long time);
      long long getBpsDown(unsigned long long time);
      long long getBpsUp(unsigned long long time);
      long long getBpsDown(unsigned long long start, unsigned long long end);
      long long getBpsUp(unsigned long long start, unsigned long long end);
  };

  
  extern std::map<sessIndex, statSession> sessions;
  extern std::map<unsigned long, sessIndex> connToSession;
  extern tthread::mutex statsMutex;

  std::set<std::string> getActiveStreams(const std::string & prefix = "");
  void parseStatistics(char * data, size_t len, unsigned int id);
  void fillClients(JSON::Value & req, JSON::Value & rep);
  void fillTotals(JSON::Value & req, JSON::Value & rep);
  void SharedMemStats(void * config);
  bool hasViewers(std::string streamName);
}

