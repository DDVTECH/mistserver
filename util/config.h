/// \file config.h
/// Contains generic function headers for managing configuration.

#include <string>

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

/// Contains utility code, not directly related to streaming media
namespace Util{

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

  /// Will set the active user to the named username.
  void setUser(std::string user);

  /// Will turn the current process into a daemon.
  void Daemonize();

};
