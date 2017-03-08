#include "controller_license.h"
#include "controller_storage.h"
#include <iostream>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/socket.h>
#include <mist/auth.h>
#include <mist/timing.h>
#include <mist/config.h>
#include <mist/encryption.h>
#include <mist/encode.h>


namespace Controller{
  
  static JSON::Value currentLicense;
  
  const JSON::Value & getLicense(){
    return currentLicense;
  }
  
  //PACKAGE_VERSION = MistServer version 
  //RELEASE = OS + user_ID
  
  void initLicense(){
    if (Storage.isMember("license") && Storage.isMember("license_id")){
      INFO_MSG("Reading license from storage")
      readLicense(Storage["license_id"].asInt(), Storage["license"].asStringRef());
      if (!isLicensed()){
        updateLicense();
      }
    }else{
      updateLicense();
    }
  }

  bool isLicensed(){
    uint64_t now = Util::epoch();
#if DEBUG >= DLVL_DEVEL
    INFO_MSG("Verifying license against %llu: %s", now, currentLicense.toString().c_str());
#endif
    //The loop below is timechecker loop
    if (!currentLicense.isMember("valid_from") || !currentLicense.isMember("valid_till") || now < currentLicense["valid_from"].asInt() || now > currentLicense["valid_till"].asInt()){
      if (currentLicense.isMember("user_msg") && currentLicense["user_msg"].asStringRef().size()){
        FAIL_MSG("%s", currentLicense["user_msg"].asStringRef().c_str());
      }
      return false;//license is expired
    }
    if (RELEASE != currentLicense["release"].asStringRef() || PACKAGE_VERSION != currentLicense["version"].asStringRef()){
      FAIL_MSG("Could not verify license");
      return false;
    }
    //everything seems okay
    return true;
  }
  
  bool checkLicense(){
    if (!currentLicense.isMember("interval")){
      currentLicense["interval"] = 3600ll;
    }
    INFO_MSG("Checking license time");
    if(!isLicensed()){
      FAIL_MSG("Not licensed, shutting down");
      kill(getpid(), SIGINT);
      return false;
    }
    return true;
  }
  
  void parseKey(std::string key, char * newKey, unsigned int len){
    memset(newKey, 0, len);
    for (size_t i = 0; i < key.size() && i < (len << 1); ++i){
      char c = key[i];
      newKey[i>>1] |= ((c&15) + (((c&64)>>6) | ((c&64)>>3))) << ((~i&1) << 2);
    }
  }
  
  void updateLicense(){
    INFO_MSG("Running license updater");
    JSON::Value response;
    
    HTTP::Parser http;
    Socket::Connection updrConn("releases.mistserver.org", 80, true);
    if ( !updrConn){
      WARN_MSG("Failed to reach licensing server");
      return;
    }
    
    //Sending request to server.
    //http.url = "/licensing.php"
    //also see statics at start function.
    http.url = "/license.php?release="+Encodings::URL::encode(RELEASE)+"&version="+Encodings::URL::encode(PACKAGE_VERSION)+"&iid="+Encodings::URL::encode(instanceId)+"&lid="+currentLicense["lic_id"].asString();
    long long currID = currentLicense["lic_id"].asInt();
    http.method = "GET";
    http.SetHeader("Host", "releases.mistserver.org");
    http.SetHeader("X-Version", PACKAGE_VERSION);
    if (currID){
      char aesKey[16];
      if (strlen(SUPER_SECRET) >= 32){
        parseKey(SUPER_SECRET SUPER_SECRET + 7,aesKey,16);
      }else{
        parseKey("4E56721C67306E1F473156F755FF5570",aesKey,16);
      }
      for (unsigned int i = 0; i < 8; ++i){
        aesKey[15-i] = ((currID >> (i*8)) + aesKey[15-i]) & 0xFF;
      }
      char ivec[16];
      memset(ivec, 0, 16);
      http.SetHeader("X-IRDGAF", Encodings::Base64::encode(Encryption::AES_Crypt(RELEASE "|" PACKAGE_VERSION, sizeof(RELEASE "|" PACKAGE_VERSION), aesKey, ivec)));
    }
    updrConn.SendNow(http.BuildRequest());
    http.Clean();
    unsigned int startTime = Util::epoch();
    while ((Util::epoch() - startTime < 10) && (updrConn || updrConn.Received().size())){
      if (updrConn.spool() || updrConn.Received().size()){
        if ( *(updrConn.Received().get().rbegin()) != '\n'){
          std::string tmp = updrConn.Received().get();
          updrConn.Received().get().clear();
          if (updrConn.Received().size()){
            updrConn.Received().get().insert(0, tmp);
          }else{
            updrConn.Received().append(tmp);
          }
          continue;
        }
        if (http.Read(updrConn.Received().get())){
          response = JSON::fromString(http.body);
          break; //break out of while loop
        }
      }
    }
    updrConn.close();
    
    //read license
    readLicense(response["lic_id"].asInt(), response["license"].asStringRef());
    
  }
  
  void readLicense(uint64_t licID, const std::string & input){
    char aesKey[16];
    if (strlen(SUPER_SECRET) >= 32){
      parseKey(SUPER_SECRET SUPER_SECRET + 7,aesKey,16);
    }else{
      parseKey("4E56721C67306E1F473156F755FF5570",aesKey,16);
    }
    for (unsigned int i = 0; i < 8; ++i){
      aesKey[15-i] = ((licID >> (i*8)) + aesKey[15-i]) & 0xFF;
    }
    std::string cipher = Encodings::Base64::decode(input);
    std::string deCrypted;
    //magic ivecs, they are empty. It's secretly 16 times \0. 
    char ivec[16];
    memset(ivec, 0, 16);
    deCrypted = Encryption::AES_Crypt(cipher.c_str(), cipher.size(), aesKey, ivec);
    //get time stamps and license.
    
    //verify checksum
    if (deCrypted.size() < 33 || Secure::md5(deCrypted.substr(32)) != deCrypted.substr(0,32)){
      FAIL_MSG("Could not decode license");
      Storage.removeMember("license");
      return;
    }
    currentLicense = JSON::fromString(deCrypted.substr(32));
    if (RELEASE != currentLicense["release"].asStringRef() || PACKAGE_VERSION != currentLicense["version"].asStringRef()){
      FAIL_MSG("Could not verify license");
      return;
    }

    //Store license here.
    if (currentLicense["store"].asBool()){
      if (Storage["license"].asStringRef() != input){
        Storage["license"] = input;
        Storage["license_id"] = (long long)licID;
        INFO_MSG("Stored license for offline use");
      }
    }
  }
  
  void licenseLoop(void * np){
    unsigned long now = Util::epoch();
    while (conf.is_active){
      if (Util::epoch() - now > currentLicense["interval"].asInt()){
        updateLicense();
        if (checkLicense()){
          now = Util::epoch();
        }
      }
      Util::sleep(1000);//sleep a bit
    }
  }
}
