/// \file stream.h
/// Utilities for handling streams.

#pragma once
#include <string>
#include "socket.h"

namespace Util{
  class Stream{
    /// Sanitize a streamname.
    static void sanitizeName(std::string & streamname);
  public:
    /// Get a connection to a Live stream.
    static Socket::Connection getLive(std::string streamname);
    /// Get a connection to a VoD stream.
    static Socket::Connection getVod(std::string streamname);
    /// Probe for available streams. Currently first VoD, then Live.
    static Socket::Connection getStream(std::string streamname);

    /// Create a Live stream on the system.
    static Socket::Server makeLive(std::string streamname);
  };
}
