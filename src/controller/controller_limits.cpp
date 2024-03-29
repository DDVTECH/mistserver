#include "controller_limits.h"
#include "controller_statistics.h"
#include "controller_storage.h"

#include <arpa/inet.h>
#include <iostream>
#include <netdb.h>
#include <sys/socket.h>

namespace Controller{
  void checkStreamLimits(std::string streamName, long long currentKbps, long long connectedUsers){
    if (!Storage["streams"].isMember(streamName)){return;}
    if (!Storage["streams"][streamName].isMember("limits")){return;}
    if (!Storage["streams"][streamName]["limits"]){return;}

    Storage["streams"][streamName].removeMember("hardlimit_active");
    if (Storage["streams"][streamName]["online"].asInt() != 1){
      jsonForEach(Storage["streams"][streamName]["limits"], limitIt){
        if ((*limitIt).isMember("triggered")){
          if ((*limitIt)["type"].asString() == "soft"){
            Log("SLIM", "Softlimit " + (*limitIt)["name"].asString() + " <= " + (*limitIt)["value"].asString() +
                            " for stream " + streamName + " reset - stream unavailable.");
          }else{
            Log("HLIM", "Hardlimit " + (*limitIt)["name"].asString() + " <= " + (*limitIt)["value"].asString() +
                            " for stream " + streamName + " reset - stream unavailable.");
          }
          (*limitIt).removeMember("triggered");
        }
      }
      return;
    }

    // run over all limits.
    jsonForEach(Storage["streams"][streamName]["limits"], limitIt){
      bool triggerLimit = false;
      if ((*limitIt)["name"].asString() == "users" && connectedUsers >= (*limitIt)["value"].asInt()){
        triggerLimit = true;
      }
      if ((*limitIt)["name"].asString() == "kbps_max" && currentKbps >= (*limitIt)["value"].asInt()){
        triggerLimit = true;
      }
      if (triggerLimit){
        if ((*limitIt)["type"].asString() == "hard"){
          Storage["streams"][streamName]["hardlimit_active"] = true;
        }
        if ((*limitIt).isMember("triggered")){continue;}
        if ((*limitIt)["type"].asString() == "soft"){
          Log("SLIM", "Softlimit " + (*limitIt)["name"].asString() + " <= " + (*limitIt)["value"].asString() +
                          " for stream " + streamName + " triggered.");
        }else{
          Log("HLIM", "Hardlimit " + (*limitIt)["name"].asString() + " <= " + (*limitIt)["value"].asString() +
                          " for stream " + streamName + " triggered.");
        }
        (*limitIt)["triggered"] = true;
      }else{
        if (!(*limitIt).isMember("triggered")){continue;}
        if ((*limitIt)["type"].asString() == "soft"){
          Log("SLIM", "Softlimit " + (*limitIt)["name"].asString() +
                          " <= " + (*limitIt)["value"].asString() + " for stream " + streamName + " reset.");
        }else{
          Log("HLIM", "Hardlimit " + (*limitIt)["name"].asString() +
                          " <= " + (*limitIt)["value"].asString() + " for stream " + streamName + " reset.");
        }
        (*limitIt).removeMember("triggered");
      }
    }
  }

  void checkServerLimits(){

    int currentKbps = 0;
    int connectedUsers = 0;
    std::map<std::string, long long> strmUsers;
    std::map<std::string, long long> strmBandw;

    /*
    if (curConns.size()){
      for (std::map<unsigned long, statStorage>::iterator it = curConns.begin(); it != curConns.end(); it++){
        if (it->second.log.size() < 2){continue;}
        std::map<unsigned long long, statLog>::reverse_iterator statRef = it->second.log.rbegin();
        std::map<unsigned long long, statLog>::reverse_iterator prevRef = --(it->second.log.rbegin());
        unsigned int diff = statRef->first - prevRef->first;
        strmUsers[it->second.streamName]++;
        connectedUsers++;
        strmBandw[it->second.streamName] += (((statRef->second.down - prevRef->second.down) + (statRef->second.up - prevRef->second.up)) / diff);
        currentKbps += (((statRef->second.down - prevRef->second.down) + (statRef->second.up - prevRef->second.up)) / diff);
      }
    }
    */

    // check stream limits
    if (Storage["streams"].size()){
      jsonForEach(Storage["streams"], strmIt){
        checkStreamLimits(strmIt.key(), strmBandw[strmIt.key()], strmUsers[strmIt.key()]);
      }
    }

    Storage["config"].removeMember("hardlimit_active");
    if (!Storage["config"]["limits"].size()){return;}
    if (!Storage["streams"].size()){return;}

    jsonForEach(Storage["config"]["limits"], limitIt){
      bool triggerLimit = false;
      if ((*limitIt)["name"].asString() == "users" && connectedUsers >= (*limitIt)["value"].asInt()){
        triggerLimit = true;
      }
      if ((*limitIt)["name"].asString() == "kbps_max" && currentKbps >= (*limitIt)["value"].asInt()){
        triggerLimit = true;
      }
      if (triggerLimit){
        if ((*limitIt)["type"].asString() == "hard"){
          Storage["config"]["hardlimit_active"] = true;
        }
        if ((*limitIt).isMember("triggered")){continue;}
        if ((*limitIt)["type"].asString() == "soft"){
          Log("SLIM", "Serverwide softlimit " + (*limitIt)["name"].asString() +
                          " <= " + (*limitIt)["value"].asString() + " triggered.");
        }else{
          Log("HLIM", "Serverwide hardlimit " + (*limitIt)["name"].asString() +
                          " <= " + (*limitIt)["value"].asString() + " triggered.");
        }
        (*limitIt)["triggered"] = true;
      }else{
        if (!(*limitIt).isMember("triggered")){continue;}
        if ((*limitIt)["type"].asString() == "soft"){
          Log("SLIM", "Serverwide softlimit " + (*limitIt)["name"].asString() +
                          " <= " + (*limitIt)["value"].asString() + " reset.");
        }else{
          Log("HLIM", "Serverwide hardlimit " + (*limitIt)["name"].asString() +
                          " <= " + (*limitIt)["value"].asString() + " reset.");
        }
        (*limitIt).removeMember("triggered");
      }
    }
  }

  bool onList(std::string ip, std::string list){
    if (list == ""){return false;}
    std::string entry;
    std::string lowerIpv6; // lower-case
    std::string upperIpv6; // full-caps
    do{
      entry = list.substr(0, list.find(" ")); // make sure we have a single entry
      lowerIpv6 = "::ffff:" + entry;
      upperIpv6 = "::FFFF:" + entry;
      if (entry == ip || lowerIpv6 == ip || upperIpv6 == ip){return true;}
      long long unsigned int starPos = entry.find("*");
      if (starPos == std::string::npos){
        if (ip == entry){return true;}
      }else{
        if (starPos == 0){// beginning of the filter
          if (ip.substr(ip.length() - entry.size() - 1) == entry.substr(1)){return true;}
        }else{
          if (starPos == entry.size() - 1){// end of the filter
            if (ip.find(entry.substr(0, entry.size() - 1)) == 0){return true;}
            if (ip.find(entry.substr(0, lowerIpv6.size() - 1)) == 0){return true;}
            if (ip.find(entry.substr(0, upperIpv6.size() - 1)) == 0){return true;}
          }else{
            Log("CONF", "Invalid list entry detected: " + entry);
          }
        }
      }
      list.erase(0, entry.size() + 1);
    }while (list != "");
    return false;
  }

  std::string hostLookup(std::string ip){
    struct sockaddr_in6 sa;
    char hostName[1024];
    char service[20];
    if (inet_pton(AF_INET6, ip.c_str(), &(sa.sin6_addr)) != 1){return "\n";}
    sa.sin6_family = AF_INET6;
    sa.sin6_port = 0;
    sa.sin6_flowinfo = 0;
    sa.sin6_scope_id = 0;
    int tmpRet = getnameinfo((struct sockaddr *)&sa, sizeof sa, hostName, sizeof hostName, service,
                             sizeof service, NI_NAMEREQD);
    if (tmpRet == 0){return hostName;}
    return "";
  }

  bool isBlacklisted(std::string host, std::string streamName, int timeConnected){
    std::string myHostName = hostLookup(host);
    if (myHostName == "\n"){return false;}
    bool hasWhitelist = false;
    bool hostOnWhitelist = false;
    if (Storage["streams"].isMember(streamName)){
      if (Storage["streams"][streamName].isMember("limits") &&
          Storage["streams"][streamName]["limits"].size()){
        jsonForEach(Storage["streams"][streamName]["limits"], limitIt){
          if ((*limitIt)["name"].asString() == "host"){
            if ((*limitIt)["value"].asString()[0] == '+'){
              if (!onList(host, (*limitIt)["value"].asString().substr(1))){
                if (myHostName == ""){
                  if (timeConnected > Storage["config"]["limit_timeout"].asInt()){return true;}
                }else{
                  if (!onList(myHostName, (*limitIt)["value"].asString().substr(1))){
                    if ((*limitIt)["type"].asString() == "hard"){
                      Log("HLIM", "Host " + host + " not whitelisted for stream " + streamName);
                      return true;
                    }else{
                      Log("SLIM", "Host " + host + " not whitelisted for stream " + streamName);
                    }
                  }
                }
              }
            }else{
              if ((*limitIt)["value"].asString()[0] == '-'){
                if (onList(host, (*limitIt)["value"].asString().substr(1))){
                  if ((*limitIt)["type"].asString() == "hard"){
                    Log("HLIM", "Host " + host + " blacklisted for stream " + streamName);
                    return true;
                  }else{
                    Log("SLIM", "Host " + host + " blacklisted for stream " + streamName);
                  }
                }
                if (myHostName != "" && onList(myHostName, (*limitIt)["value"].asString().substr(1))){
                  if ((*limitIt)["type"].asString() == "hard"){
                    Log("HLIM", "Host " + myHostName + " blacklisted for stream " + streamName);
                    return true;
                  }else{
                    Log("SLIM", "Host " + myHostName + " blacklisted for stream " + streamName);
                  }
                }
              }
            }
          }
        }
      }
    }
    if (Storage["config"]["limits"].size()){
      jsonForEach(Storage["config"]["limits"], limitIt){
        if ((*limitIt)["name"].asString() == "host"){
          if ((*limitIt)["value"].asString()[0] == '+'){
            if (!onList(host, (*limitIt)["value"].asString().substr(1))){
              if (myHostName == ""){
                if (timeConnected > Storage["config"]["limit_timeout"].asInt()){return true;}
              }else{
                if (!onList(myHostName, (*limitIt)["value"].asString().substr(1))){
                  if ((*limitIt)["type"].asString() == "hard"){
                    Log("HLIM", "Host " + host + " not whitelisted for stream " + streamName);
                    return true;
                  }else{
                    Log("SLIM", "Host " + host + " not whitelisted for stream " + streamName);
                  }
                }
              }
            }
          }else{
            if ((*limitIt)["value"].asString()[0] == '-'){
              if (onList(host, (*limitIt)["value"].asString().substr(1))){
                if ((*limitIt)["type"].asString() == "hard"){
                  Log("HLIM", "Host " + host + " blacklisted for stream " + streamName);
                  return true;
                }else{
                  Log("SLIM", "Host " + host + " blacklisted for stream " + streamName);
                }
              }
              if (myHostName != "" && onList(myHostName, (*limitIt)["value"].asString().substr(1))){
                if ((*limitIt)["type"].asString() == "hard"){
                  Log("HLIM", "Host " + myHostName + " blacklisted for stream " + streamName);
                  return true;
                }else{
                  Log("SLIM", "Host " + myHostName + " blacklisted for stream " + streamName);
                }
              }
            }
          }
        }
      }
    }
    if (hasWhitelist){
      if (hostOnWhitelist || myHostName == ""){
        return false;
      }else{
        return true;
      }
    }
    return false;
  }

}// namespace Controller
