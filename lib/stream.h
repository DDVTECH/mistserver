/// \file stream.h
/// Utilities for handling streams.

#pragma once
#include <string>
#include "socket.h"

namespace Util {
  class Stream{
    public:
      static void sanitizeName(std::string & streamname);
      static Socket::Connection getLive(std::string streamname);
      static Socket::Connection getVod(std::string streamname);
      static Socket::Connection getStream(std::string streamname);
      static Socket::Server makeLive(std::string streamname);
  };
}
