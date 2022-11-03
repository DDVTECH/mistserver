#pragma once
#include <map>
#include <mist/comms.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/json.h>
#include <mist/shared_memory.h>
#include <mist/socket.h>
#include <mist/timing.h>
#include <mist/tinythread.h>
#include <string>

/// The STAT_CUTOFF define sets how many seconds of statistics history is kept.
#ifndef STAT_CUTOFF
#define STAT_CUTOFF 600
#endif

namespace Controller{

  extern bool killOnExit;
  extern unsigned int maxConnsPerIP;

  /// This function is ran whenever a stream becomes active.
  void streamStarted(std::string stream);
  /// This function is ran whenever a stream becomes inactive.
  void streamStopped(std::string stream);

  void updateBandwidthConfig();

  struct statLog{
    uint64_t time;
    uint64_t lastSecond;
    uint64_t down;
    uint64_t up;
    uint64_t pktCount;
    uint64_t pktLost;
    uint64_t pktRetransmit;
  };

  enum sessType{SESS_UNSET = 0, SESS_INPUT, SESS_OUTPUT, SESS_VIEWER};

  /// This is a comparison and storage class that keeps sessions apart from each other.
  /// Whenever two of these objects are not equal, it will create a new session.
  class sessIndex{
  public:
    sessIndex();
    sessIndex(const Comms::Statistics &statComm, size_t id);
    std::string ID;
    std::string host;
    unsigned int crc;
    std::string streamName;
    std::string connector;

    bool operator==(const sessIndex &o) const;
    bool operator!=(const sessIndex &o) const;
    bool operator>(const sessIndex &o) const;
    bool operator<=(const sessIndex &o) const;
    bool operator<(const sessIndex &o) const;
    bool operator>=(const sessIndex &o) const;
    std::string toStr();
  };

  class statStorage{
  public:
    void update(Comms::Statistics &statComm, size_t index);
    bool hasDataFor(unsigned long long);
    statLog &getDataFor(unsigned long long);
    std::map<unsigned long long, statLog> log;
  };

  /// A session class that keeps track of both current and archived connections.
  /// Allows for moving of connections to another session.
  class statSession{
  private:
    uint64_t firstActive;
    uint64_t firstSec;
    uint64_t lastSec;
    uint64_t wipedUp;
    uint64_t wipedDown;
    uint64_t wipedPktCount;
    uint64_t wipedPktLost;
    uint64_t wipedPktRetransmit;
    std::deque<statStorage> oldConns;
    sessType sessionType;
    bool tracked;
    uint8_t noBWCount; ///< Set to 2 when not to count for external bandwidth
  public:
    statSession();
    uint32_t invalidate();
    uint32_t kill();
    char sync;
    std::map<uint64_t, statStorage> curConns;
    std::set<std::string> tags;
    sessType getSessType();
    void wipeOld(uint64_t);
    void finish(uint64_t index);
    void switchOverTo(statSession &newSess, uint64_t index);
    void update(uint64_t index, Comms::Statistics &data);
    void dropSession(const sessIndex &index);
    uint64_t getStart();
    uint64_t getEnd();
    bool isViewerOn(uint64_t time);
    bool isConnected();
    bool isTracked();
    bool hasDataFor(uint64_t time);
    bool hasData();
    uint64_t getConnTime(uint64_t time);
    uint64_t getLastSecond(uint64_t time);
    uint64_t getDown(uint64_t time);
    uint64_t getUp();
    uint64_t getDown();
    uint64_t getUp(uint64_t time);
    uint64_t getPktCount();
    uint64_t getPktCount(uint64_t time);
    uint64_t getPktLost();
    uint64_t getPktLost(uint64_t time);
    uint64_t getPktRetransmit();
    uint64_t getPktRetransmit(uint64_t time);
    uint64_t getBpsDown(uint64_t time);
    uint64_t getBpsUp(uint64_t time);
    uint64_t getBpsDown(uint64_t start, uint64_t end);
    uint64_t getBpsUp(uint64_t start, uint64_t end);
  };

  extern std::map<sessIndex, statSession> sessions;
  extern std::map<unsigned long, sessIndex> connToSession;
  extern tthread::mutex statsMutex;
  extern uint64_t statDropoff;

  struct triggerLog{
    uint64_t totalCount;
    uint64_t failCount;
    uint64_t ms;
  };

  extern std::map<std::string, triggerLog> triggerStats;

  void statLeadIn();
  void statOnActive(size_t id);
  void statOnDisconnect(size_t id);
  void statLeadOut();

  std::set<std::string> getActiveStreams(const std::string &prefix = "");
  void killStatistics(char *data, size_t len, unsigned int id);
  void fillClients(JSON::Value &req, JSON::Value &rep);
  void fillActive(JSON::Value &req, JSON::Value &rep);
  void fillHasStats(JSON::Value &req, JSON::Value &rep);
  void fillTotals(JSON::Value &req, JSON::Value &rep);
  void SharedMemStats(void *config);
  void sessions_invalidate(const std::string &streamname);
  void sessions_shutdown(JSON::Iter &i);
  void sessId_shutdown(const std::string &sessId);
  void tag_shutdown(const std::string &tag);
  void sessId_tag(const std::string &sessId, const std::string &tag);
  void sessions_shutdown(const std::string &streamname, const std::string &protocol = "");
  bool hasViewers(std::string streamName);
  void writeSessionCache(); /*LTS*/

#define PROMETHEUS_TEXT 0
#define PROMETHEUS_JSON 1
  void handlePrometheus(HTTP::Parser &H, Socket::Connection &conn, int mode);
}// namespace Controller


