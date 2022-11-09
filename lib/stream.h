/// \file stream.h
/// Utilities for handling streams.

#pragma once
#include "dtsc.h"
#include "json.h"
#include "shared_memory.h"
#include "socket.h"
#include "util.h"
#include <string>
#include <list>

const JSON::Value empty;

namespace Util{
  size_t streamCustomVariables(std::string &str);
  size_t streamVariables(std::string &str, const std::string &streamname, const std::string &source = "", uint8_t depth = 0);
  std::string getTmpFolder();
  void sanitizeName(std::string &streamname);
  bool streamAlive(std::string &streamname);
  bool startInput(std::string streamname, std::string filename = "", bool forkFirst = true,
                  bool isProvider = false,
                  const std::map<std::string, std::string> &overrides = std::map<std::string, std::string>(),
                  pid_t *spawn_pid = NULL);
  int startPush(const std::string &streamname, std::string &target, int debugLvl = -1);
  JSON::Value getStreamConfig(const std::string &streamname);
  JSON::Value getGlobalConfig(const std::string &optionName);
  JSON::Value getInputBySource(const std::string &filename, bool isProvider = false);
  void sendUDPApi(JSON::Value & cmd);
  uint8_t getStreamStatus(const std::string &streamname);
  uint8_t getStreamStatusPercentage(const std::string &streamname);
  bool checkException(const JSON::Value &ex, const std::string &useragent);
  std::string codecString(const std::string &codec, const std::string &initData = "");

  std::set<size_t> getSupportedTracks(const DTSC::Meta &M, const JSON::Value &capa,
                                      const std::string &type = "", const std::string &UA = "");
  std::set<size_t> pickTracks(const DTSC::Meta &M, const std::set<size_t> trackList, const std::string &trackType, const std::string &trackVal);
  std::set<size_t> findTracks(const DTSC::Meta &M, const JSON::Value &capa, const std::string &trackType, const std::string &trackVal, const std::string &UA = "");
  std::set<size_t> wouldSelect(const DTSC::Meta &M, const std::string &trackSelector = "",
                               const JSON::Value &capa = empty, const std::string &UA = "");
  std::set<size_t> wouldSelect(const DTSC::Meta &M, const std::map<std::string, std::string> &targetParams,
                               const JSON::Value &capa = empty, const std::string &UA = "", uint64_t seekTarget = 0);

  enum trackSortOrder{
    TRKSORT_DEFAULT = 0,
    TRKSORT_BPS_LTH,
    TRKSORT_BPS_HTL,
    TRKSORT_ID_LTH,
    TRKSORT_ID_HTL,
    TRKSORT_RES_LTH,
    TRKSORT_RES_HTL
  };
  extern trackSortOrder defaultTrackSortOrder;
  void sortTracks(std::set<size_t> & validTracks, const DTSC::Meta & M, trackSortOrder sorting, std::list<size_t> & srtTrks);

  /// This struct keeps packet information sorted in playback order
  struct sortedPageInfo{
    bool operator<(const sortedPageInfo &rhs) const{
      if (time < rhs.time){return true;}
      return (time == rhs.time && tid < rhs.tid);
    }
    size_t tid;
    uint64_t time;
    uint64_t offset;
    size_t partIndex;
    bool ghostPacket;
  };

  /// Packet sorter used to determine which packet should be output next
  class packetSorter{
    public:
      packetSorter();
      size_t size() const;
      void clear();
      const sortedPageInfo * begin() const;
      void insert(const sortedPageInfo &pInfo);
      void dropTrack(size_t tid);
      void replaceFirst(const sortedPageInfo &pInfo);
      void moveFirstToEnd();
      bool hasEntry(size_t tid) const;
      void getTrackList(std::set<size_t> &toFill) const;
      void getTrackList(std::map<size_t, uint64_t> &toFill) const;
      void setSyncMode(bool synced);
      bool getSyncMode() const;
    private:
      bool dequeMode;
      std::deque<sortedPageInfo> dequeBuffer;
      std::set<sortedPageInfo> setBuffer;
  };


  class DTSCShmReader{
  public:
    DTSCShmReader(const std::string &pageName);
    DTSC::Scan getMember(const std::string &indice);
    DTSC::Scan getScan();

  private:
    IPC::sharedPage rPage;
    Util::RelAccX rAcc;
  };

}// namespace Util
