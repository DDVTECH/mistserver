/// \file config.h
/// Contains generic function headers for managing configuration.

#pragma once

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "unknown"
#endif

#include <string>
#include "json.h"

/// Contains utility code, not directly related to streaming media
namespace Util {

  /// Deals with parsing configuration from commandline options.
  class Config{
    private:
      JSON::Value vals; ///< Holds all current config values
      int long_count;
      static void signal_handler(int signum);
    public:
      //variables
      static std::string libver; ///< Version number of the library as a string.
      static bool is_active; ///< Set to true by activate(), set to false by the signal handler.
      //functions
      Config(std::string cmd, std::string version);
      void addOption(std::string optname, JSON::Value option);
      void printHelp(std::ostream & output);
      void parseArgs(int argc, char ** argv);
      JSON::Value & getOption(std::string optname, bool asArray = false);
      std::string getString(std::string optname);
      long long int getInteger(std::string optname);
      bool getBool(std::string optname);
      void activate();
      void addConnectorOptions(int port);
  };

  /// Gets directory the current executable is stored in.
  std::string getMyPath();

  /// Will set the active user to the named username.
  void setUser(std::string user);

  /// Will turn the current process into a daemon.
  void Daemonize();

}
;
