/// \file time.cpp
/// Utilities for handling time and timestamps.

#include "timing.h"
#include <sys/time.h>//for gettimeofday
#include <time.h>//for time and nanosleep
/// Sleeps for the indicated amount of milliseconds or longer.
void Util::sleep(int ms){
  if (ms < 0){
    return;
  }
  if (ms > 10000){
    return;
  }
  struct timespec T;
  T.tv_sec = ms / 1000;
  T.tv_nsec = 1000000 * (ms % 1000);
  nanosleep( &T, 0);
}

/// Gets the current time in milliseconds.
long long int Util::getMS(){
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return ((long long int)t.tv_sec) * 1000 + t.tv_nsec / 1000000;
}

/// Gets the amount of seconds since 01/01/1970.
long long int Util::epoch(){
  return time(0);
}
