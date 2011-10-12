/// \file proc.h
/// Contains generic function headers for managing processes.

#include <unistd.h>
#include <string>
#include <map>

/// Contains utility code, not directly related to streaming media
namespace Util{

  /// Deals with spawning, monitoring and stopping child processes
  class Procs{
    private:
      static std::map<pid_t, std::string> plist; ///< Holds active processes
      static bool handler_set; ///< If true, the sigchld handler has been setup.
      static void childsig_handler(int signum);
      static void runCmd(std::string & cmd);
    public:
      static pid_t Start(std::string name, std::string cmd);
      static pid_t Start(std::string name, std::string cmd, std::string cmd2);
      static void Stop(std::string name);
      static void Stop(pid_t name);
      static void StopAll();
      static int Count();
      static bool isActive(std::string name);
      static bool isActive(pid_t name);
      static pid_t getPid(std::string name);
      static std::string getName(pid_t name);
  };
};
