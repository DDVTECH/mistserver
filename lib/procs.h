/// \file procs.h
/// Contains generic function headers for managing processes.

#pragma once
#include <deque>
#include <set>
#include <string>
#include <unistd.h>
#include <stdint.h> 

/// Contains utility code, not directly related to streaming media
namespace Util{

  /// Deals with spawning, monitoring and stopping child processes
  class Procs{
  public:
    static bool childRunning(pid_t p);
    static void fork_prepare();
    static void reap();
    static void fork_complete();
    static void setHandler();
    static std::string getOutputOf(const char *const *argv, uint64_t maxWait = 10000);
    static std::string getOutputOf(const std::deque<std::string> & argDeq, uint64_t maxWait = 10000);
    static std::string getLimitedOutputOf(char *const *argv, uint64_t maxWait, uint32_t maxValBytes);
    static std::string getLimitedOutputOf(const std::deque<std::string> & argDeq, uint64_t maxWait,
                                          uint32_t maxValBytes);
    static pid_t StartPiped(const char *const *argv, int *fdIn, int *fdOut, int *fdErr);
    static pid_t StartPiped(const char *const *argv);
    static pid_t StartPiped(const std::deque<std::string> & argDeq, int *fdIn, int *fdOut, int *fdErr);
    static void Stop(pid_t name);
    static void Murder(pid_t name);
    static void StopAll();
    static int Count();
    static bool isActive(pid_t name);
    static bool isRunning(pid_t pid);
    static void forget(pid_t pid);
    static void remember(pid_t pid);
    static std::set<int> socketList; ///< Holds sockets that should be closed before forking
    static int kill_timeout;
  };
}// namespace Util
