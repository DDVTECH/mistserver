/// \file time.cpp
/// Utilities for handling time and timestamps.

#include "timing.h"
#include <sys/time.h>//for gettimeofday
#include <time.h>//for time and nanosleep

/// Sleeps for the indicated amount of milliseconds or longer.
void Util::sleep(int ms){
  struct timespec T;
  T.tv_sec = ms/1000;
  T.tv_nsec = 1000*(ms%1000);
  nanosleep(&T, 0);
}

/// Gets the current time in milliseconds.
long long int Util::getMS(){
  /// \todo Possibly change to use clock_gettime - needs -lrt though...
  timeval t;
  gettimeofday(&t, 0);
  return t.tv_sec * 1000 + t.tv_usec/1000;
}

/// Gets the amount of seconds since 01/01/1970.
long long int Util::epoch(){
  return time(0);
}
