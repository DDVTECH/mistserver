/// \file stream.h
/// Utilities for handling streams.

#pragma once
#include "dtsc.h"
#include "json.h"
#include "shared_memory.h"
#include "socket.h"
#include "util.h"
#include <string>

const JSON::Value empty;

namespace Util{
  void streamVariables(std::string &str, const std::string &streamname, const std::string &source = "");
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
  uint8_t getStreamStatus(const std::string &streamname);
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
