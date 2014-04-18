/// \file stream.h
/// Utilities for handling streams.

#pragma once
#include <string>
#include "socket.h"

namespace Util {
  std::string getTmpFolder();
  class Stream{
    public:
      static void sanitizeName(std::string & streamname);
      static bool getLive(std::string streamname);
      static bool getVod(std::string filename, std::string streamname);
      static bool getStream(std::string streamname);
      static Socket::Server makeLive(std::string streamname);
  };
}
