/// \file config.h
/// Contains generic function headers for managing configuration.

#pragma once

#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "unknown"
#endif

#include "json.h"

#include <functional>
#include <string>
#include <sys/wait.h>

/// Contains utility code, not directly related to streaming media
namespace Util{
  extern uint32_t printDebugLevel;
  extern __thread char streamName[256]; ///< Used by debug messages to identify the stream name
  void setStreamName(const std::string & sn);
  extern __thread char exitReason[256];
  extern __thread char* mRExitReason;
  void logExitReason(const char* shortString, const char *format, ...);

  enum binType {
    UNSET,
    INPUT,
    OUTPUT,
    PROCESS,
    CONTROLLER
  };

  /// Deals with parsing configuration from commandline options.
  class Config{
  private:
    JSON::Value vals; ///< Holds all current config values
    int long_count;
    static void signal_handler(int signum, siginfo_t *sigInfo, void *ignore);

  public:
    static void setMutexAborter(void * mutex);
    static void wipeShm();
    static void setServerFD(int fd);
    // variables
    static bool is_active;     ///< Set to true by activate(), set to false by the signal handler.
    static bool is_restarting; ///< Set to true when restarting, set to false on boot.
    static binType binaryType;
    // functions
    Config();
    Config(std::string cmd);
    void addOption(const std::string & optname, const JSON::Value & option);
    void addOption(const std::string & optname, const char *jsonStr);
    void printHelp(std::ostream &output);
    bool parseArgs(int &argc, char **&argv);
    bool hasOption(const std::string & optname) const;
    JSON::Value & getOption(std::string optname, bool asArray = false);
    const JSON::Value & getOption(std::string optname, bool asArray = false) const;
    std::string getString(std::string optname) const;
    int64_t getInteger(std::string optname) const;
    bool getBool(std::string optname) const;
    void fillEffectiveArgs(std::deque<std::string> & args, bool longForm = true) const;
    void activate();
    void installDefaultChildSignalHandler();
    bool setupServerSocket(Socket::Server & s);
    bool serveThreadedSocket(int (*callback)(Socket::Connection &));
    bool serveCallbackSocket(std::function<void(Socket::Connection &, Socket::Server &)> callback);
    Socket::Address boundServer;
    void addOptionsFromCapabilities(const JSON::Value &capabilities);
    void addBasicConnectorOptions(JSON::Value &capabilities);
    void addStandardPushCapabilities(JSON::Value &capabilities);
    void addConnectorOptions(int port, JSON::Value &capabilities);
  };

  /// Gets the directory and name of the binary the executable is stored in.
  std::string getMyPathWithBin();

  /// Gets directory the current executable is stored in.
  std::string getMyPath();

  /// Gets all executables in getMyPath that start with "Mist".
  void getMyExec(std::deque<std::string> &execs);

  /// Will set the active user to the named username.
  void setUser(std::string user);

}// namespace Util
