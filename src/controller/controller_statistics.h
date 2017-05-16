#pragma once
#include <mist/shared_memory.h>
#include <mist/timing.h>
#include <mist/defines.h>
#include <mist/json.h>
#include <mist/tinythread.h>
#include <mist/http_parser.h>
#include <mist/socket.h>
#include <string>
#include <map>

/// The STAT_CUTOFF define sets how many seconds of statistics history is kept.
#define STAT_CUTOFF 600


namespace Controller {
  
  extern bool killOnExit;
  extern unsigned int maxConnsPerIP;
  extern Controller::semaphore cacheLock;

  //These keep track of which streams are currently active.
  extern std::map<std::string, unsigned int> activeStreams;
  ///This function is ran whenever a stream becomes active.
  void streamStarted(std::string stream);
  ///This function is ran whenever a stream becomes inactive.
  void streamStopped(std::string stream);
  
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
      std::string ID;
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
      unsigned long long firstSec;
      unsigned long long lastSec;
      unsigned long long wipedUp;
      unsigned long long wipedDown;
      std::deque<statStorage> oldConns;
      sessType sessionType;
      bool tracked;
    public:
      statSession();
      uint32_t invalidate();
      uint32_t kill();
      char sync;
      std::map<unsigned long, statStorage> curConns;
      std::set<std::string> tags;
      sessType getSessType();
      void wipeOld(unsigned long long);
      void finish(unsigned long index);
      void switchOverTo(statSession & newSess, unsigned long index);
      void update(unsigned long index, IPC::statExchange & data);
      void ping(const sessIndex & index, unsigned long long disconnectPoint);
      unsigned long long getStart();
      unsigned long long getEnd();
      bool isViewerOn(unsigned long long time);
      bool isViewer();
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
  void parseStatistics(char * data, size_t len, unsigned int id);
  void killStatistics(char * data, size_t len, unsigned int id);
  void fillClients(JSON::Value & req, JSON::Value & rep);
  void fillActive(JSON::Value & req, JSON::Value & rep, bool onlyNow = false);
  void fillTotals(JSON::Value & req, JSON::Value & rep);
  void SharedMemStats(void * config);
  void sessions_invalidate(const std::string & streamname);
  void sessions_shutdown(JSON::Iter & i);
  void sessId_shutdown(const std::string & sessId);
  void tag_shutdown(const std::string & tag);
  void sessId_tag(const std::string & sessId, const std::string & tag);
  void sessions_shutdown(const std::string & streamname, const std::string & protocol = "");
  bool hasViewers(std::string streamName);
  void writeSessionCache(); /*LTS*/

#define PROMETHEUS_TEXT 0
#define PROMETHEUS_JSON 1
  void handlePrometheus(HTTP::Parser & H, Socket::Connection & conn, int mode);
}

