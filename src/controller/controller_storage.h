#include <mist/config.h>
#include <mist/ev.h>
#include <mist/json.h>
#include <mist/util.h>

#include <mutex>
#include <string>

namespace Controller{
  extern Event::Loop E;
  extern std::string instanceId;     ///< global storage of instanceId (previously uniqID) is set in controller.cpp
  extern std::string prometheus;     ///< Prometheus access string
  extern std::string accesslog;      ///< Where to write the access log
  extern std::string udpApiBindAddr; ///< Bound address where the UDP API listens
  extern Util::Config conf;          ///< Global storage of configuration.
  extern JSON::Value Storage;        ///< Global storage of data.
  extern std::mutex logMutex;    ///< Mutex for log thread.
  extern std::mutex configMutex; ///< Mutex for server config access.
  extern bool isTerminal;            ///< True if connected to a terminal and not a log file.
  extern bool isColorized;           ///< True if we colorize the output
  extern uint64_t logCounter;        ///< Count of logged messages since boot
  extern uint64_t systemBoot;        ///< Unix time in milliseconds of system boot
  extern uint64_t lastConfigChange;  ///< Unix time in seconds of last configuration change
  extern uint64_t lastConfigWrite;   ///< Unix time in seconds of last time configuration was written to disk
  extern JSON::Value lastConfigWriteAttempt; ///< Contents of last attempted config write
  extern JSON::Value lastConfigSeen; ///< Contents of config last time we looked at it. Used to check for changes.

  Util::RelAccX *logAccessor();
  Util::RelAccX *accesslogAccessor();
  Util::RelAccX *streamsAccessor();

  void logParser();
  void logAccess(const std::string &sessId, const std::string &strm, const std::string &conn,
                 const std::string &host, uint64_t duration, uint64_t up, uint64_t down,
                 const std::string &tags);

  size_t jwkUriCheck();

  void normalizeTrustedProxies(JSON::Value &tp);

  /// Write contents to Filename.
  bool WriteFile(std::string Filename, std::string contents);

  void getConfigAsWritten(JSON::Value & conf);
  void writeConfigToDisk(bool forceWrite = false);
  void readConfigFromDisk();

  void handleMsg(int errFd);

  void initStorage();
  void deinitStorage(bool leaveBehind);
  void writeConfig();
  void writeStream(const std::string &sName, const JSON::Value &sConf);
  void writeCapabilities();
  void writeProtocols();

  void addShmPage(const std::string & page);
  void wipeShmPages();

}// namespace Controller
