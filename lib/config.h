/// \file config.h
/// Contains generic function headers for managing configuration.

#pragma once
#include <string>

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

/// Contains utility code, not directly related to streaming media
namespace Util{

  /// Deals with parsing configuration from commandline options.
  class Config{
    public:
      bool daemon_mode;
      std::string interface;
      int listen_port;
      std::string username;
      Config();
      void parseArgs(int argc, char ** argv);
  };

  /// Will set the active user to the named username.
  void setUser(std::string user);

  /// Will turn the current process into a daemon.
  void Daemonize();

};
