/// \file timing.h
/// Utilities for handling time and timestamps.

#pragma once
#include <string>

namespace Util {
  void wait(int ms); ///< Sleeps for the indicated amount of milliseconds or longer.
  void sleep(int ms); ///< Sleeps for roughly the indicated amount of milliseconds.
  long long int getMS(); ///< Gets the current time in milliseconds.
  long long int bootSecs(); ///< Gets the current system uptime in seconds.
  long long unsigned int getMicros();///<Gets the current time in microseconds.
  long long unsigned int getMicros(long long unsigned int previous);///<Gets the time difference in microseconds.
  long long int getNTP();
  long long int epoch(); ///< Gets the amount of seconds since 01/01/1970.
  std::string getUTCString(long long int epoch = 0);
}
