/// \file timing.h
/// Utilities for handling time and timestamps.

#pragma once
#include <stdint.h>
#include <string>

namespace Util{
  void wait(int64_t ms);  ///< Sleeps for the indicated amount of milliseconds or longer.
  void sleep(int64_t ms); ///< Sleeps for roughly the indicated amount of milliseconds.
  void usleep(int64_t us); ///< Sleeps for roughly the indicated amount of microseconds.
  uint64_t getMS();       ///< Gets the current time in milliseconds.
  uint64_t bootSecs();    ///< Gets the current system uptime in seconds.
  uint64_t unixMS();      ///< Gets the current Unix time in milliseconds.
  uint64_t bootMS();      ///< Gets the current system uptime in milliseconds.
  uint64_t getMicros();   ///< Gets the current time in microseconds
  uint64_t getMicros(uint64_t previous); ///< Gets the time difference in microseconds.
  uint64_t getNTP();
  uint64_t epoch(); ///< Gets the amount of seconds since 01/01/1970.
  std::string getUTCString(uint64_t epoch = 0);
  std::string getUTCStringMillis(uint64_t epoch_millis = 0);
  uint64_t getMSFromUTCString(std::string UTCString);
  uint64_t getUTCTimeDiff(std::string UTCString, uint64_t epochMillis);
  std::string getDateString(uint64_t epoch = 0);
  uint64_t getFileUnixTime(const std::string & filename);
}// namespace Util
