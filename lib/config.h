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
    void addOption(std::string optname, JSON::Value option);
    void printHelp(std::ostream &output);
    bool parseArgs(int &argc, char **&argv);
    bool hasOption(const std::string &optname);
    JSON::Value &getOption(std::string optname, bool asArray = false);
    std::string getString(std::string optname);
    int64_t getInteger(std::string optname);
    bool getBool(std::string optname);
    void fillEffectiveArgs(std::deque<std::string> & args, bool longForm = true);
    void activate();
    void installDefaultChildSignalHandler();
    int threadServer(Socket::Server &server_socket, int (*callback)(Socket::Connection &S));
    int serveThreadedSocket(int (*callback)(Socket::Connection &));
    int serveForkedSocket(int (*callback)(Socket::Connection &S));
    int serveCallbackSocket(std::function<void(Socket::Connection &, Socket::Server &)> callback);
    void addOptionsFromCapabilities(const JSON::Value &capabilities);
    void addBasicConnectorOptions(JSON::Value &capabilities);
    void addStandardPushCapabilities(JSON::Value &capabilities);
    void addConnectorOptions(int port, JSON::Value &capabilities);
  };

  /// The interface address the current serveSocket function is listening on
  extern std::string listenInterface;
  /// The port the current serveSocket function is listening on
  extern uint32_t listenPort;

  /// Gets the directory and name of the binary the executable is stored in.
  std::string getMyPathWithBin();

  /// Gets directory the current executable is stored in.
  std::string getMyPath();

  /// Gets all executables in getMyPath that start with "Mist".
  void getMyExec(std::deque<std::string> &execs);

  /// Will set the active user to the named username.
  void setUser(std::string user);

}// namespace Util
