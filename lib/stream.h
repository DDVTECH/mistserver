/// \file stream.h
/// Utilities for handling streams.

#pragma once
#include <string>
#include "socket.h"
#include "json.h"
#include "dtsc.h"

namespace Util {
  void streamVariables(std::string &str, const std::string & streamname, const std::string & source = "");
  std::string getTmpFolder();
  void sanitizeName(std::string & streamname);
  bool streamAlive(std::string & streamname);
  bool startInput(std::string streamname, std::string filename = "", bool forkFirst = true, bool isProvider = false, const std::map<std::string, std::string> & overrides = std::map<std::string, std::string>(), pid_t * spawn_pid = NULL);
  int startPush(const std::string & streamname, std::string & target);
  JSON::Value getStreamConfig(const std::string & streamname);
  JSON::Value getInputBySource(const std::string & filename, bool isProvider = false);
  DTSC::Meta getStreamMeta(const std::string & streamname);
  uint8_t getStreamStatus(const std::string & streamname);
}

