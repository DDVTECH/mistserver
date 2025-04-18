#include "controller_api.h"
#include "controller_capabilities.h"
#include "controller_connectors.h"
#include "controller_statistics.h"
#include "controller_storage.h"
#include "controller_streams.h"
#include "controller_updater.h"
#include "controller_uplink.h"
#include <mist/auth.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/dtsc.h>
#include <mist/timing.h>
#include <stdlib.h>

void Controller::uplinkConnection(){
  std::string uplink_name = Controller::conf.getString("uplink-name");
  std::string uplink_pass = Controller::conf.getString("uplink-pass");
  std::string uplink_addr = Controller::conf.getString("uplink");
  std::string uplink_host = "";
  std::string uplink_chal = "";
  int uplink_port = 0;
  if (uplink_addr.size() > 0){
    size_t colon = uplink_addr.find(':');
    if (colon != std::string::npos && colon != 0 && colon != uplink_addr.size()){
      uplink_host = uplink_addr.substr(0, colon);
      uplink_port = atoi(uplink_addr.substr(colon + 1, std::string::npos).c_str());
      Controller::Log("CONF", "Connection to uplink enabled on host " + uplink_host + " and port " +
                                  uplink_addr.substr(colon + 1, std::string::npos));
    }
  }
  // cancel the whole thread if no uplink is set
  if (!uplink_port){return;}

  uint64_t lastSend = Util::epoch() - 5;
  Socket::Connection uplink;
  while (Controller::conf.is_active){
    if (!uplink){
      INFO_MSG("Connecting to uplink at %s:%u", uplink_host.c_str(), uplink_port);
      uplink.open(uplink_host, uplink_port, true);
    }
    if (uplink){
      if (uplink.spool()){
        if (uplink.Received().available(9)){
          std::string data = uplink.Received().copy(8);
          if (data.substr(0, 4) != "DTSC"){
            uplink.Received().clear();
            continue;
          }
          unsigned int size = ntohl(*(const unsigned int *)(data.data() + 4));
          if (uplink.Received().available(8 + size)){
            std::string packet = uplink.Received().remove(8 + size);
            DTSC::Scan inScan = DTSC::Packet(packet.data(), packet.size()).getScan();
            if (!inScan){continue;}
            JSON::Value curVal;
            // Parse config and streams from the request.
            if (inScan.hasMember("authorize") && inScan.getMember("authorize").hasMember("challenge")){
              uplink_chal = inScan.getMember("authorize").getMember("challenge").asString();
            }
            if (inScan.hasMember("streams")){
              curVal = inScan.getMember("streams").asJSON();
              Controller::CheckStreams(curVal, Controller::Storage["streams"]);
            }
            if (inScan.hasMember("addstream")){
              curVal = inScan.getMember("addstream").asJSON();
              Controller::AddStreams(curVal, Controller::Storage["streams"]);
            }
            if (inScan.hasMember("deletestream")){
              curVal = inScan.getMember("deletestream").asJSON();
              // if array, delete all elements
              // if object, delete all entries
              // if string, delete just the one
              if (curVal.isString()){
                Controller::Storage["streams"].removeMember(curVal.asStringRef());
              }
              if (curVal.isArray()){
                jsonForEach(curVal, it){
                  Controller::Storage["streams"].removeMember(it->asString());
                }
              }
              if (curVal.isObject()){
                jsonForEach(curVal, it){Controller::Storage["streams"].removeMember(it.key());}
              }
            }
          }
        }
      }
      if (Util::epoch() - lastSend >= 2){
        JSON::Value data;
        data["tracks"].null(); // make sure the data is encoded as DTSC
        if (uplink_chal.size()){
          data["authorize"]["username"] = uplink_name;
          data["authorize"]["password"] = Secure::md5(Secure::md5(uplink_pass) + uplink_chal);
        }
        JSON::Value totalsRequest;
        Controller::fillClients(totalsRequest, data["clients"]);
        totalsRequest["start"] = lastSend;
        Controller::fillTotals(totalsRequest, data["totals"]);
        data["streams"] = Controller::Storage["streams"];
        jsonForEach(data["streams"], it){
          it->removeMember("meta");
          it->removeMember("l_meta");
          it->removeMember("name");
        }
        data["config"] = Controller::Storage["config"];
        data["config"]["uniq"] = instanceId;
        data["config"]["version"] = PACKAGE_VERSION;
        Controller::checkCapable(capabilities);
        data["capabilities"] = capabilities;
        data["capabilities"].removeMember("connectors");
        data.sendTo(uplink);
        lastSend = Util::epoch();
      }
    }else{
      Controller::Log("UPLK", "Could not connect to uplink.");
    }
    Util::wait(2000); // wait for 2.5 seconds
  }
}
