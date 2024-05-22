/// \file procs.h
/// Contains generic function headers for managing processes.

#pragma once
#include "tinythread.h"
#include <deque>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>
#include <stdint.h> 

/// Contains utility code, not directly related to streaming media
namespace Util{

  /// Gets directory the current executable is stored in.
  std::string getMyPath();

  /// Gets all executables in getMyPath that start with "Mist".
  void getMyExec(std::deque<std::string> &execs);

  /// Deals with spawning, monitoring and stopping child processes
  class Procs{
  private:
    static tthread::mutex plistMutex;
    static std::set<pid_t> plist; ///< Holds active process list.
    static bool thread_handler;   ///< True while thread handler should be running.
    static void childsig_handler(int signum);
    static void exit_handler();
    static char *const *dequeToArgv(std::deque<std::string> &argDeq);
    static void grim_reaper(void *n);
  public:
    static bool childRunning(pid_t p);
    static tthread::thread *reaper_thread;
    static bool handler_set; ///< If true, the sigchld handler has been setup.
    static void fork_prepare();
    static void fork_complete();
    static void setHandler();
    static std::string getOutputOf(char *const *argv, uint64_t maxWait = 0);
    static std::string getOutputOf(std::deque<std::string> &argDeq, uint64_t maxWait = 0);
    static std::string getLimitedOutputOf(char *const *argv, uint64_t maxWait, uint32_t maxValBytes);
    static pid_t StartPiped(const char *const *argv, int *fdin, int *fdout, int *fderr);
    static pid_t StartPiped(std::deque<std::string> &argDeq, int *fdin, int *fdout, int *fderr);
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
