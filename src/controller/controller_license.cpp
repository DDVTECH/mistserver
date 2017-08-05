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
#include <mist/downloader.h>


namespace Controller{
  
  uint64_t exitDelay = 0;
  static JSON::Value currentLicense;
  static uint64_t lastCheck = 0;
  static int32_t timeOffset = 0;
  static bool everContactedServer = false;
  
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
        updateLicense("&boot=1");
        checkLicense();
      }else{
        lastCheck = std::min(Util::epoch(), currentLicense["valid_from"].asInt());
      }
    }else{
      updateLicense("&boot=1");
      checkLicense();
    }
  }

  bool isLicensed(){
    uint64_t now = Util::epoch() + timeOffset;
#if DEBUG >= DLVL_DEVEL
    INFO_MSG("Verifying license against %llu: %s", now, currentLicense.toString().c_str());
#endif
    //Print messages for user, if any
    if (currentLicense.isMember("user_msg") && currentLicense["user_msg"].asStringRef().size()){
      WARN_MSG("%s", currentLicense["user_msg"].asStringRef().c_str());
    }
    //Check time
    if (!currentLicense.isMember("valid_from") || !currentLicense.isMember("valid_till") || now < currentLicense["valid_from"].asInt() || now > currentLicense["valid_till"].asInt()){
      return false;//license is expired
    }
    //Check release/version
    if (RELEASE != currentLicense["release"].asStringRef() || PACKAGE_VERSION != currentLicense["version"].asStringRef()){
      FAIL_MSG("Could not verify license");
      return false;
    }
    //everything seems okay
    return true;
  }
  
  bool checkLicense(){
    if (!conf.is_active){return true;}
    INFO_MSG("Checking license validity");
    if(!everContactedServer && !isLicensed()){
      updateLicense("&expired=1");
    }
    if(!isLicensed()){
      FAIL_MSG("Not licensed, shutting down");
      if (currentLicense.isMember("delay") && currentLicense["delay"].asInt()){
        exitDelay = currentLicense["delay"].asInt();
      }
      kill(getpid(), SIGINT);
      conf.is_active = false;
      return false;
    }
    lastCheck = Util::epoch();
    return true;
  }
  
  void parseKey(std::string key, char * newKey, unsigned int len){
    memset(newKey, 0, len);
    for (size_t i = 0; i < key.size() && i < (len << 1); ++i){
      char c = key[i];
      newKey[i>>1] |= ((c&15) + (((c&64)>>6) | ((c&64)>>3))) << ((~i&1) << 2);
    }
  }
  
  void updateLicense(const std::string & extra){
    INFO_MSG("Running license updater %s", extra.c_str());
    JSON::Value response;
    
    HTTP::Downloader dl;
    HTTP::URL url("http://releases.mistserver.org/license.php");
    url.args = "release="+Encodings::URL::encode(RELEASE)+"&version="+Encodings::URL::encode(PACKAGE_VERSION)+"&iid="+Encodings::URL::encode(instanceId)+"&lid="+currentLicense["lic_id"].asString() + extra;

    long long currID = currentLicense["lic_id"].asInt();
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
      dl.setHeader("X-IRDGAF", Encodings::Base64::encode(Encryption::AES_Crypt(RELEASE "|" PACKAGE_VERSION, sizeof(RELEASE "|" PACKAGE_VERSION), aesKey, ivec)));
    }
    if (!dl.get(url) || !dl.isOk()){
      return;
    }
    response = JSON::fromString(dl.data());
    everContactedServer = true;
    
    //read license
    readLicense(response["lic_id"].asInt(), response["license"].asStringRef(), true);
  }
  
  void readLicense(uint64_t licID, const std::string & input, bool fromServer){
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
      WARN_MSG("Could not decode license");
      return;
    }
    JSON::Value newLicense = JSON::fromString(deCrypted.substr(32));
    if (RELEASE != newLicense["release"].asStringRef() || PACKAGE_VERSION != newLicense["version"].asStringRef()){
      FAIL_MSG("Could not verify license");
      return;
    }

    if (fromServer){
      uint64_t localTime = Util::epoch();
      uint64_t remoteTime = newLicense["time"].asInt();
      if (localTime > remoteTime + 60){
        WARN_MSG("Your computer clock is %u seconds ahead! Please ensure your computer clock is set correctly.", localTime - remoteTime);
      }
      if (localTime < remoteTime - 60){
        WARN_MSG("Your computer clock is %u seconds late! Please ensure your computer clock is set correctly.", remoteTime - localTime);
      }
      timeOffset = remoteTime - localTime;

      if (newLicense.isMember("plid") && newLicense["plid"] != currentLicense["lic_id"]){
        FAIL_MSG("Could not verify license ID");
        return;
      }
    }

    currentLicense = newLicense;

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
    while (conf.is_active){
      uint64_t interval = currentLicense["interval"].asInt();
      if (Util::epoch() - lastCheck > (interval?interval:3600)){
        if (interval){
          updateLicense();
        }
        checkLicense();
      }
      Util::sleep(1000);//sleep a bit
    }
    if (everContactedServer){
      updateLicense("&shutdown=1");
    }
  }
}

