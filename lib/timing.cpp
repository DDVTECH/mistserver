/// \file timing.cpp
/// Utilities for handling time and timestamps.

#include "timing.h"
#include <sys/time.h>//for gettimeofday
#include <time.h>//for time and nanosleep
#include <sys/sysinfo.h> //forsysinfo

//emulate clock_gettime() for OSX compatibility
#if defined(__APPLE__) || defined(__MACH__)
#include <mach/clock.h>
#include <mach/mach.h>
#define CLOCK_REALTIME 0
void clock_gettime(int ign, struct timespec * ts){
  clock_serv_t cclock;
  mach_timespec_t mts;
  host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
  clock_get_time(cclock, &mts);
  mach_port_deallocate(mach_task_self(), cclock);
  ts->tv_sec = mts.tv_sec;
  ts->tv_nsec = mts.tv_nsec;
}
#endif

/// Sleeps for the indicated amount of milliseconds or longer.
void Util::sleep(int ms){
  if (ms < 0){
    return;
  }
  if (ms > 100000){
    ms = 100000;
  }
  struct timespec T;
  T.tv_sec = ms / 1000;
  T.tv_nsec = 1000000 * (ms % 1000);
  nanosleep( &T, 0);
}

long long Util::getNTP(){
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return ((((long long int)t.tv_sec) + 2208988800) << 32) + (t.tv_nsec*4.2949);
}

/// Gets the current time in milliseconds.
long long int Util::getMS(){
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return ((long long int)t.tv_sec) * 1000 + t.tv_nsec / 1000000;
}

long long int Util::bootSecs(){
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec;
}

/// Gets the current time in microseconds.
long long unsigned int Util::getMicros(){
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return ((long long unsigned int)t.tv_sec) * 1000000 + t.tv_nsec / 1000;
}

/// Gets the time difference in microseconds.
long long unsigned int Util::getMicros(long long unsigned int previous){
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return ((long long unsigned int)t.tv_sec) * 1000000 + t.tv_nsec / 1000 - previous;
}

/// Gets the amount of seconds since 01/01/1970.
long long int Util::epoch(){
  return time(0);
}
