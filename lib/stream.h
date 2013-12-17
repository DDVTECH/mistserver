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
      static Socket::Connection getLive(std::string streamname);
      static Socket::Connection getVod(std::string filename, std::string streamname);
      static Socket::Connection getStream(std::string streamname);
      static Socket::Server makeLive(std::string streamname);
  };
}
