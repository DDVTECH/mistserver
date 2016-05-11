#include <string>
#include <mist/json.h>
#include <mist/config.h>
#include <mist/tinythread.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include "controller_storage.h"
#include "controller_statistics.h"

namespace Controller {

  /// Internal list of currently active pushes
  std::map<pid_t, JSON::Value> activePushes;

  /// Internal list of waiting pushes
  std::deque<JSON::Value> waitingPushes;

  /// Immediately starts a push for the given stream to the given target.
  /// Simply calls Util::startPush and stores the resulting PID in the local activePushes map.
  void startPush(std::string & stream, std::string & target){
    pid_t ret = Util::startPush(stream, target);
    if (ret){
      JSON::Value push;
      push.append((long long)ret);
      push.append(stream);
      push.append(target);
      activePushes[ret] = push;
    }
  }

  /// Immediately stops a push with the given ID
  void stopPush(unsigned int ID){
    Util::Procs::Stop(ID);
  }

  /// Loops, checking every second if any pushes need restarting.
  void pushCheckLoop(){
  }

  /// Gives a list of all currently active pushes
  void listPush(JSON::Value & output){
    output.null();
    std::set<pid_t> toWipe;
    for (std::map<pid_t, JSON::Value>::iterator it = activePushes.begin(); it != activePushes.end(); ++it){
      if (Util::Procs::isActive(it->first)){
        output.append(it->second);
      }else{
        toWipe.insert(it->first);
      }
    }
    while (toWipe.size()){
      activePushes.erase(*toWipe.begin());
      toWipe.erase(toWipe.begin());
    }
  }

  /// Adds a push to the list of auto-pushes.
  /// Auto-starts currently active matches immediately.
  void addPush(JSON::Value & request){
    JSON::Value newPush;
    if (request.isArray()){
      newPush = request;
    }else{
      newPush.append(request["stream"]);
      newPush.append(request["target"]);
    }
    Controller::Storage["autopushes"].append(newPush);
    if (activeStreams.size()){
      const std::string & pStr = newPush[0u].asStringRef();
      std::string target = newPush[1u].asStringRef();
      for (std::map<std::string, unsigned int>::iterator it = activeStreams.begin(); it != activeStreams.end(); ++it){
        std::string streamname = it->first;
        if (pStr == streamname || (*pStr.rbegin() == '+' && streamname.substr(0, pStr.size()) == pStr)){
          startPush(streamname, target);
        }
      }
    }
  }

  /// Removes a push from the list of auto-pushes.
  /// Does not stop currently active matching pushes.
  void removePush(const JSON::Value & request){
    JSON::Value delPush;
    if (request.isString()){
      return removePush(request.asStringRef());
    }
    if (request.isArray()){
      delPush = request;
    }else{
      delPush.append(request["stream"]);
      delPush.append(request["target"]);
    }
    JSON::Value newautopushes;
    jsonForEach(Controller::Storage["autopushes"], it){
      if ((*it) != delPush){
        newautopushes.append(*it);
      }
    }
    Controller::Storage["autopushes"] = newautopushes;
  }

  /// Removes a push from the list of auto-pushes.
  /// Does not stop currently active matching pushes.
  void removePush(const std::string & streamname){
    JSON::Value newautopushes;
    jsonForEach(Controller::Storage["autopushes"], it){
      if ((*it)[0u] != streamname){
        newautopushes.append(*it);
      }
    }
    Controller::Storage["autopushes"] = newautopushes;
  }

  /// Starts all configured auto pushes for the given stream.
  void doAutoPush(std::string & streamname){
    jsonForEach(Controller::Storage["autopushes"], it){
      const std::string & pStr = (*it)[0u].asStringRef();
      if (pStr == streamname || (*pStr.rbegin() == '+' && streamname.substr(0, pStr.size()) == pStr)){
        std::string stream = streamname;
        std::string target = (*it)[1u];
        startPush(stream, target);
      }
    }
  }

  void pushSettings(const JSON::Value & request, JSON::Value & response){
    if (request.isObject()){
      if (request.isMember("wait")){
        Controller::Storage["push_settings"]["wait"] = request["wait"].asInt();
      }
      if (request.isMember("maxspeed")){
        Controller::Storage["push_settings"]["maxspeed"] = request["maxspeed"].asInt();
      }
      
    }
  response = Controller::Storage["push_settings"];
  }

}

