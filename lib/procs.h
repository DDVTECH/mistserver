/// \file procs.h
/// Contains generic function headers for managing processes.

#pragma once
#include <unistd.h>
#include <string>
#include <set>
#include <vector>
#include <deque>

/// Contains utility code, not directly related to streaming media
namespace Util {

  /// Deals with spawning, monitoring and stopping child processes
  class Procs {
    private:
      static std::set<pid_t> plist; ///< Holds active processes
      static bool handler_set; ///< If true, the sigchld handler has been setup.
      static void childsig_handler(int signum);
      static void exit_handler();
      static void runCmd(std::string & cmd);
      static void setHandler();
      static char* const* dequeToArgv(std::deque<std::string> & argDeq);
    public:
      static std::string getOutputOf(char * const * argv);
      static std::string getOutputOf(std::deque<std::string> & argDeq);
      static pid_t StartPiped(char * const * argv, int * fdin, int * fdout, int * fderr);
      static pid_t StartPiped(std::deque<std::string> & argDeq, int * fdin, int * fdout, int * fderr);
      static void Stop(pid_t name);
      static void Murder(pid_t name);
      static void StopAll();
      static int Count();
      static bool isActive(pid_t name);
      static bool isRunning(pid_t pid);
  };

}
