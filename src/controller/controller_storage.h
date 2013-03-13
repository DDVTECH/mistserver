#include <string>
#include <mist/json.h>

namespace Controller {

  extern JSON::Value Storage; ///< Global storage of data.

  /// Store and print a log message.
  void Log(std::string kind, std::string message);

  /// Write contents to Filename.
  void WriteFile(std::string Filename, std::string contents);

}
