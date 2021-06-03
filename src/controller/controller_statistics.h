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
  
  /// This function is ran whenever a stream becomes active.
  void streamStarted(std::string stream);
  /// This function is ran whenever a stream becomes inactive.
  void streamStopped(std::string stream);

  void updateBandwidthConfig();

  struct statLog{
    uint64_t time;
    uint64_t firstActive;
    uint64_t lastSecond;
    uint64_t down;
    uint64_t up;
    uint64_t pktCount;
    uint64_t pktLost;
    uint64_t pktRetransmit;
    std::string streamName;
    std::string host;
    std::string connectors;
  };

  enum sessType{SESS_UNSET = 0, SESS_INPUT, SESS_OUTPUT, SESS_VIEWER, SESS_UNSPECIFIED};

  class uxInfo{
    public:
      uxInfo(){
        bad = 0;
        good = 0;
        great = 0;
      };
      size_t bad;
      size_t good;
      size_t great;
      void add(uint8_t q){
        if (q >= 100){
          ++great;
        }else if (q >= 75){
          ++good;
        }else{
          ++bad;
        }

      }
  };

  class uxIndex{
  public:
    uxIndex();
    uxIndex(const std::string & stream, const std::string & proto, const std::string & geo, uint8_t qual);
    std::string stream;
    std::string proto;
    std::string geo;
    uint8_t qual;
    bool operator==(const uxIndex &o) const;
    bool operator!=(const uxIndex &o) const;
    bool operator>(const uxIndex &o) const;
    bool operator<=(const uxIndex &o) const;
    bool operator<(const uxIndex &o) const;
    bool operator>=(const uxIndex &o) const;
  };


  class statStorage{
  public:
    void update(Comms::Sessions &statComm, size_t index);
    bool hasDataFor(unsigned long long);
    statLog &getDataFor(unsigned long long);
    std::map<unsigned long long, statLog> log;
  };

  /// A session class that keeps track of both current and archived connections.
  /// Allows for moving of connections to another session.
  class statSession{
  private:
    sessType sessionType;
    uint8_t noBWCount; ///< Set to 2 when not to count for external bandwidth
    std::string sessId;

  public:
    statSession();
    void finish();
    statStorage curData;
    std::set<std::string> tags;
    sessType getSessType();
    void update(uint64_t index, Comms::Sessions &data);
    uint64_t getStart();
    uint64_t getEnd();
    bool hasDataFor(uint64_t time);
    const std::string& getSessId();
    const std::string& getStreamName(uint64_t t);
    const std::string& getStreamName();
    std::string getStrHost(uint64_t t);
    std::string getStrHost();
    const std::string& getHost(uint64_t t);
    const std::string& getHost();
    const std::string& getConnectors(uint64_t t);
    const std::string& getConnectors();
    uint64_t getFirstActive();
    uint64_t getConnTime(uint64_t time);
    uint64_t getConnTime();
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
  void uxOnActive(size_t id);
  void uxOnDisconnect(size_t id);
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
  void killConnections(std::string sessId);

#define PROMETHEUS_TEXT 0
#define PROMETHEUS_JSON 1
  void handlePrometheus(HTTP::Parser &H, Socket::Connection &conn, int mode);
}// namespace Controller
