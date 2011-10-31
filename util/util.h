/// \file util.h
/// Contains generic function headers for managing processes and configuration.

#include <unistd.h>
#include <string>
#include <map>

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

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

  /// Will set the active user to the named username.
  void setUser(std::string user);

  /// Deals with parsing configuration from files or commandline options.
  class Config{
  private:
    bool ignore_daemon;
    bool ignore_interface;
    bool ignore_port;
    bool ignore_user;
  public:
    std::string confsection;
    std::string configfile;
    bool daemon_mode;
    std::string interface;
    int listen_port;
    std::string username;
    Config();
    void parseArgs(int argc, char ** argv);
    void parseFile();
  };

  /// Will turn the current process into a daemon.
  void Daemonize();

};
