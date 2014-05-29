#include <string>
#include <mist/json.h>

#include <mist/tinythread.h>
namespace Controller {

  extern JSON::Value Storage; ///< Global storage of data.

  extern tthread::mutex logMutex;///< Mutex for log thread.

  /// Store and print a log message.
  void Log(std::string kind, std::string message);

  /// Write contents to Filename.
  bool WriteFile(std::string Filename, std::string contents);

}
