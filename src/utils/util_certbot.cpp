/// \file util_certbot.cpp
/// Certbot integration utility
/// Intended to be ran like so:
// certbot certonly --manual --preferred-challenges=http --manual-auth-hook MistUtilCertbot --deploy-hook MistUtilCertbot -d yourdomain.example.com

// When called from --deploy-hook:
// RENEWED_LINEAGE: directory with the certificate
// RENEWED_DOMAINS: space-delimited list of domains

// When called from --manual-auth-hook:
// CERTBOT_DOMAIN:     The domain being authenticated
// CERTBOT_VALIDATION: The validation string
// CERTBOT_TOKEN:      Resource name part of the HTTP-01 challenge

#include <mist/config.h>
#include <mist/defines.h>
#include <mist/stream.h>
#include <string>

/// Checks if port 80 is HTTP, returns the indice number (>= 0) if it is.
/// Returns -1 if nothing is running on port 80.
/// Returns -2 if the port is taken by another protocol (and prints a FAIL-level message).
/// If found, sets currConf to the current configuration of the HTTP protocol.
int checkPort80(JSON::Value &currConf){
  Util::DTSCShmReader rCapa(SHM_CAPA);
  DTSC::Scan conns = rCapa.getMember("connectors");
  Util::DTSCShmReader rProto(SHM_PROTO);
  DTSC::Scan prtcls = rProto.getScan();
  unsigned int pro_cnt = prtcls.getSize();
  for (unsigned int i = 0; i < pro_cnt; ++i){
    std::string ctor = prtcls.getIndice(i).getMember("connector").asString();
    DTSC::Scan capa = conns.getMember(ctor);
    uint16_t port = prtcls.getIndice(i).getMember("port").asInt();
    // get the default port if none is set
    if (!port){port = capa.getMember("optional").getMember("port").getMember("default").asInt();}
    // Found a port 80 entry?
    if (port == 80){
      if (ctor == "HTTP" || ctor == "HTTP.exe"){
        currConf = prtcls.getIndice(i).asJSON();
        return i;
      }else{
        FAIL_MSG("Found non-HTTP protocol %s on port 80; aborting! Please free up port 80 for HTTP",
                 ctor.c_str());
        return -2;
      }
    }
  }
  return -1;
}

int main(int argc, char **argv){
  Util::redirectLogsIfNeeded();
  Util::Config conf(argv[0]);
  conf.parseArgs(argc, argv);

  // Handle --deploy-hook
  if (getenv("RENEWED_LINEAGE")){
    INFO_MSG("Detected '--deploy-hook' calling. Installing certificate.");
    std::string cbPath = getenv("RENEWED_LINEAGE");
    std::string cbCert = cbPath + "/fullchain.pem";
    std::string cbKey = cbPath + "/privkey.pem";
    Util::DTSCShmReader rProto(SHM_PROTO);
    DTSC::Scan prtcls = rProto.getScan();
    unsigned int pro_cnt = prtcls.getSize();
    bool found = false;
    for (unsigned int i = 0; i < pro_cnt; ++i){
      std::string ctor = prtcls.getIndice(i).getMember("connector").asString();
      if (ctor == "HTTPS"){
        found = true;
        JSON::Value currConf = prtcls.getIndice(i).asJSON();
        JSON::Value cmd;
        cmd["updateprotocol"].append(currConf);
        cmd["updateprotocol"].append(currConf);
        cmd["updateprotocol"][1u]["cert"] = cbCert;
        cmd["updateprotocol"][1u]["key"] = cbKey;
        INFO_MSG("Executing: %s", cmd.toString().c_str());
        Util::sendUDPApi(cmd);
        Util::wait(500);
        Util::sendUDPApi(cmd);
        Util::wait(500);
        Util::sendUDPApi(cmd);
      }
    }
    if (!found){
      INFO_MSG("No HTTPS active; enabling on port 443");
      JSON::Value cmd;
      cmd["addprotocol"]["connector"] = "HTTPS";
      cmd["addprotocol"]["port"] = 443;
      cmd["addprotocol"]["cert"] = cbCert;
      cmd["addprotocol"]["key"] = cbKey;
      INFO_MSG("Executing: %s", cmd.toString().c_str());
      Util::sendUDPApi(cmd);
      Util::wait(500);
      Util::sendUDPApi(cmd);
      Util::wait(500);
      Util::sendUDPApi(cmd);
    }
    Util::wait(5000);
    return 0;
  }

  // Handle --manual-auth-hook
  if (getenv("CERTBOT_VALIDATION") && getenv("CERTBOT_TOKEN")){
    INFO_MSG("Detected '--manual-auth-hook' calling. Performing authentication.");
    // Store certbot variables for later use
    std::string cbValidation = getenv("CERTBOT_VALIDATION");
    std::string cbToken = getenv("CERTBOT_TOKEN");
    std::string cbCombo = cbToken + ":" + cbValidation;
    // Check Mist config, find HTTP output, check config
    JSON::Value currConf;
    int foundHTTP80 = checkPort80(currConf);
    if (foundHTTP80 == -2){return 1;}// abort if port already taken by non-HTTP process
    if (foundHTTP80 == -1){
      INFO_MSG("Nothing on port 80 found - adding HTTP connector on port 80 with correct config "
               "for certbot");
      JSON::Value cmd;
      cmd["addprotocol"]["connector"] = "HTTP";
      cmd["addprotocol"]["port"] = 80;
      cmd["addprotocol"]["certbot"] = cbCombo;
      Util::sendUDPApi(cmd);
      Util::wait(1000);
      int counter = 10;
      while (--counter && ((foundHTTP80 = checkPort80(currConf)) == -1 ||
                           currConf["certbot"].asStringRef() != cbCombo)){
        INFO_MSG("Waiting for Controller to pick up new config...");
        Util::sendUDPApi(cmd);
        Util::wait(1000);
      }
      if (!counter){
        FAIL_MSG("Timed out! Is " APPNAME " running, and is certbot being ran under the same system user " APPNAME " is running under?");
        return 1;
      }
      INFO_MSG("Success!");
      Util::wait(5000);
    }else{
      if (currConf.isMember("certbot") && currConf["certbot"].asStringRef() == cbCombo){
        INFO_MSG("Config already good - no changes needed");
        return 0;
      }
      INFO_MSG("Found HTTP on port 80; updating config...");
      JSON::Value cmd;
      cmd["updateprotocol"].append(currConf);
      cmd["updateprotocol"].append(currConf);
      cmd["updateprotocol"][1u]["certbot"] = cbCombo;
      Util::sendUDPApi(cmd);
      Util::wait(1000);
      int counter = 10;
      while (--counter && ((foundHTTP80 = checkPort80(currConf)) == -1 ||
                           currConf["certbot"].asStringRef() != cbCombo)){
        INFO_MSG("Waiting for Controller to pick up new config...");
        Util::sendUDPApi(cmd);
        Util::wait(1000);
      }
      if (!counter){
        FAIL_MSG("Timed out! Is " APPNAME " running, and is certbot being ran under the same system user " APPNAME " is running under?");
        return 1;
      }
      INFO_MSG("Success!");
      Util::wait(5000);
    }
    return 0;
  }

  // Print usage message to help point users in the right direction
  FAIL_MSG("This utility is meant to be ran by certbot, not by hand.");
  FAIL_MSG("Sample usage: certbot certonly --manual --preferred-challenges=http --manual-auth-hook "
           "MistUtilCertbot --deploy-hook MistUtilCertbot -d yourdomain.example.com");
  WARN_MSG(
      "Note: This utility will alter your MistServer configuration. If ran as deploy hook it will "
      "install the certificate generated by certbot to any already enabled HTTPS output or enable "
      "HTTPS on port 443 (if it was disabled). If ran as auth hook it will change the HTTP port to "
      "80 (and enable HTTP if it wasn't enabled already) in order to perform the validation.");
  return 1;
}
