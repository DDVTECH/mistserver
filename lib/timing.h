/// \file timing.h
/// Utilities for handling time and timestamps.

#pragma once

namespace Util {
  void sleep(int ms); ///< Sleeps for the indicated amount of milliseconds or longer.
  long long int getMS(); ///< Gets the current time in milliseconds.
  long long int epoch(); ///< Gets the amount of seconds since 01/01/1970.
}
