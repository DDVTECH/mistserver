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
    uint64_t time;
    uint64_t lastSecond;
    uint64_t down;
    uint64_t up;
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
      uint64_t firstActive;
      uint64_t firstSec;
      uint64_t lastSec;
      uint64_t wipedUp;
      uint64_t wipedDown;
      std::deque<statStorage> oldConns;
      sessType sessionType;
      bool tracked;
    public:
      statSession();
      std::map<uint64_t, statStorage> curConns;
      sessType getSessType();
      void wipeOld(uint64_t);
      void finish(uint64_t index);
      void switchOverTo(statSession & newSess, uint64_t index);
      void update(uint64_t index, IPC::statExchange & data);
      void ping(const sessIndex & index, uint64_t disconnectPoint);
      uint64_t getStart();
      uint64_t getEnd();
      bool isViewerOn(uint64_t time);
      bool isViewer();
      bool hasDataFor(uint64_t time);
      bool hasData();
      uint64_t getConnTime(uint64_t time);
      uint64_t getLastSecond(uint64_t time);
      uint64_t getDown(uint64_t time);
      uint64_t getUp();
      uint64_t getDown();
      uint64_t getUp(uint64_t time);
      uint64_t getBpsDown(uint64_t time);
      uint64_t getBpsUp(uint64_t time);
      uint64_t getBpsDown(uint64_t start, uint64_t end);
      uint64_t getBpsUp(uint64_t start, uint64_t end);
  };

  
  extern std::map<sessIndex, statSession> sessions;
  extern std::map<unsigned long, sessIndex> connToSession;
  extern tthread::mutex statsMutex;

  std::set<std::string> getActiveStreams(const std::string & prefix = "");
  void parseStatistics(char * data, size_t len, unsigned int id);
  void fillClients(JSON::Value & req, JSON::Value & rep);
  void fillActive(JSON::Value & req, JSON::Value & rep, bool onlyNow = false);
  void fillTotals(JSON::Value & req, JSON::Value & rep);
  void SharedMemStats(void * config);
  bool hasViewers(std::string streamName);
}

