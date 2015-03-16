/// \file procs.h
/// Contains generic function headers for managing processes.

#pragma once
#include <unistd.h>
#include <string>
#include <map>
#include <vector>

/// Contains utility code, not directly related to streaming media
namespace Util {

  typedef void (*TerminationNotifier)(pid_t pid, int exitCode);

  /// Deals with spawning, monitoring and stopping child processes
  class Procs {
    private:
      static std::map<pid_t, std::string> plist; ///< Holds active processes
      static std::map<pid_t, TerminationNotifier> exitHandlers; ///< termination function, if any
      static bool handler_set; ///< If true, the sigchld handler has been setup.
      static void childsig_handler(int signum);
      static void exit_handler();
      static void runCmd(std::string & cmd);
      static void setHandler();
    public:
      static std::string getOutputOf(char * const * argv);
      static std::string getOutputOf(std::string cmd);
      static pid_t Start(std::string name, std::string cmd);
      static pid_t Start(std::string name, std::string cmd, std::string cmd2);
      static pid_t Start(std::string name, std::string cmd, std::string cmd2, std::string cmd3);
      static pid_t StartPiped(char * const * argv, int * fdin, int * fdout, int * fderr);
      static pid_t StartPiped(std::string name, char * const * argv, int * fdin, int * fdout, int * fderr);
      static pid_t StartPiped(std::string name, std::string cmd, int * fdin, int * fdout, int * fderr);
      static pid_t StartPiped2(std::string name, std::string cmd1, std::string cmd2, int * fdin, int * fdout, int * fderr1, int * fderr2);
      static void Stop(std::string name);
      static void Stop(pid_t name);
      static void Murder(pid_t name);
      static void StopAll();
      static int Count();
      static bool isActive(std::string name);
      static bool isActive(pid_t name);
      static bool isRunnning(pid_t pid);
      static pid_t getPid(std::string name);
      static std::string getName(pid_t name);
      static bool SetTerminationNotifier(pid_t pid, TerminationNotifier notifier);
  };

}
