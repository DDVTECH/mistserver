#include <mist/config.h>
#include <mist/json.h>
#include <mist/tinythread.h>
#include <string>

namespace Controller{
  // API calls to manage custom variables
  void addVariable(const JSON::Value &request, JSON::Value &output);
  void listCustomVariables(JSON::Value &output);
  void removeVariable(const JSON::Value &request, JSON::Value &output);

  // internal use only
  void variableCheckLoop(void *np);
  void writeToShm();
  void removeVariableByName(const std::string &name);
  void runVariableTarget(const std::string &name, const std::string &target, const uint64_t &maxWait);
  void mutateVariable(const std::string name, std::string &newVal);
}// namespace Controller
