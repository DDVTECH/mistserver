/// \file stream.h
/// Utilities for handling streams.

#pragma once
#include <string>
#include "socket.h"
#include "json.h"
#include "dtsc.h"
#include "shared_memory.h"
#include "util.h"

namespace Util {
  std::string getTmpFolder();
  void sanitizeName(std::string & streamname);
  bool streamAlive(std::string & streamname);
  bool startInput(std::string streamname, std::string filename = "", bool forkFirst = true, bool isProvider = false, const std::map<std::string, std::string> & overrides = std::map<std::string, std::string>(), pid_t * spawn_pid = NULL);
  JSON::Value getStreamConfig(const std::string & streamname);
  JSON::Value getInputBySource(const std::string & filename, bool isProvider = false);
  DTSC::Meta getStreamMeta(const std::string & streamname);
  uint8_t getStreamStatus(const std::string & streamname);

  class DTSCShmReader{
    public:
      DTSCShmReader(const std::string &pageName);
      DTSC::Scan getMember(const std::string &indice);
      DTSC::Scan getScan();
    private:
      IPC::sharedPage rPage;
      Util::RelAccX rAcc;
  };

}

