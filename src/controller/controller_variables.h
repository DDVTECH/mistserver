#include <mist/config.h>
#include <mist/json.h>
#include <string>

namespace Controller{

  extern size_t variableTimer;

  // API calls to manage custom variables
  void addVariable(const JSON::Value & request, JSON::Value & output);
  void removeVariable(const JSON::Value &request, JSON::Value &output);

  size_t variableRun();
  void variableDeinit();

  // internal use only
  void writeToShm();
  void removeVariableByName(const std::string &name);
  bool runVariableTarget(const std::string & name, const std::string & target, const uint64_t & maxWait);
  void mutateVariable(const std::string name, std::string &newVal);
}// namespace Controller
