
#include <mist/json.h>

namespace Controller{
  
  const JSON::Value & getLicense();
  void initLicense();
  bool isLicensed(); //checks/verifies license time
  bool checkLicense(); //Call from Mainloop.
  void updateLicense(const std::string & extra = ""); //retrieves update from license server
  void licenseLoop(void * np);
  void readLicense(uint64_t licId, const std::string & input); //checks/interprets license
  
  
  
  
}
