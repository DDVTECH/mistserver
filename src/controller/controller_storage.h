#include <string>
#include <mist/json.h>
#include <mist/config.h>
#include <mist/tinythread.h>

namespace Controller {
  extern Util::Config conf;///< Global storage of configuration.
  extern JSON::Value Storage; ///< Global storage of data.
  extern tthread::mutex logMutex;///< Mutex for log thread.
  extern tthread::mutex configMutex;///< Mutex for server config access.
  extern bool configChanged; ///< Bool that indicates config must be written to SHM.
  
  /// Store and print a log message.
  void Log(std::string kind, std::string message);

  /// Write contents to Filename.
  bool WriteFile(std::string Filename, std::string contents);
  
  void handleMsg(void * err);

  void writeConfig();

}
