/// \file timing.cpp
/// Utilities for handling time and timestamps.

#include "timing.h"
#include <cstdio>
#include <cstring>
#include <sys/time.h> //for gettimeofday
#include <time.h>     //for time and nanosleep

// emulate clock_gettime() for OSX compatibility
#if defined(__APPLE__) || defined(__MACH__)
#include <mach/clock.h>
#include <mach/mach.h>
#define CLOCK_REALTIME CALENDAR_CLOCK
#define CLOCK_MONOTONIC SYSTEM_CLOCK
void clock_gettime(int ign, struct timespec *ts){
  clock_serv_t cclock;
  mach_timespec_t mts;
  host_get_clock_service(mach_host_self(), ign, &cclock);
  clock_get_time(cclock, &mts);
  mach_port_deallocate(mach_task_self(), cclock);
  ts->tv_sec = mts.tv_sec;
  ts->tv_nsec = mts.tv_nsec;
}
#endif

/// Sleeps for the indicated amount of milliseconds or longer.
/// Will not sleep if ms is negative.
/// Will not sleep for longer than 10 minutes (600000ms).
/// If interrupted by signal, resumes sleep until at least ms milliseconds have passed.
/// Can be slightly off (in positive direction only) depending on OS accuracy.
void Util::wait(int64_t ms){
  if (ms < 0){return;}
  if (ms > 600000){ms = 600000;}
  uint64_t start = getMS();
  uint64_t now = start;
  while (now < start + ms){
    sleep(start + ms - now);
    now = getMS();
  }
}

/// Sleeps for roughly the indicated amount of milliseconds.
/// Will not sleep if ms is negative.
/// Will not sleep for longer than 100 seconds (100000ms).
/// Can be interrupted early by a signal, no guarantee of minimum sleep time.
/// Can be slightly off depending on OS accuracy.
void Util::sleep(int64_t ms){
  if (ms < 0){return;}
  if (ms > 100000){ms = 100000;}
  struct timespec T;
  T.tv_sec = ms / 1000;
  T.tv_nsec = 1000000 * (ms % 1000);
  nanosleep(&T, 0);
}

uint64_t Util::getNTP(){
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return ((uint64_t)(t.tv_sec + 2208988800ull) << 32) + (t.tv_nsec * 4.2949);
}

/// Gets the current time in milliseconds.
uint64_t Util::getMS(){
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

uint64_t Util::bootSecs(){
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec;
}

uint64_t Util::bootMS(){
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return ((uint64_t)t.tv_sec) * 1000 + t.tv_nsec / 1000000;
}

uint64_t Util::unixMS(){
  struct timeval t;
  gettimeofday(&t, 0);
  return ((uint64_t)t.tv_sec) * 1000 + t.tv_usec / 1000;
}

/// Gets the current time in microseconds.
uint64_t Util::getMicros(){
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

/// Gets the time difference in microseconds.
uint64_t Util::getMicros(uint64_t previous){
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000 - previous;
}

/// Gets the amount of seconds since 01/01/1970.
uint64_t Util::epoch(){
  return time(0);
}

std::string Util::getUTCString(uint64_t epoch){
  if (!epoch){epoch = time(0);}
  time_t rawtime = epoch;
  struct tm *ptm;
  ptm = gmtime(&rawtime);
  char result[20];
  snprintf(result, 20, "%.4d-%.2d-%.2dT%.2d:%.2d:%.2d", ptm->tm_year + 1900, ptm->tm_mon + 1,
           ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
  return std::string(result);
}

std::string Util::getDateString(uint64_t epoch){
  char buffer[80];
  time_t rawtime = epoch;
  if (!epoch) {
    time(&rawtime);
  }
  struct tm * timeinfo;
  timeinfo = localtime(&rawtime);
  strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S %z", timeinfo);
  return std::string(buffer);
}
