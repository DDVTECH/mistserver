#include <mist/config.h>
#include <mist/json.h>

namespace Controller{
  // API calls to manage external writers
  void addExternalWriter(JSON::Value &request);
  void listExternalWriters(JSON::Value &output);
  void removeExternalWriter(const JSON::Value &request);

  // internal use only
  void externalWritersToShm();
}// namespace Controller
