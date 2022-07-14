#include "controller_push.h"
#include "controller_statistics.h"
#include "controller_storage.h"
#include <mist/bitfields.h>
#include <mist/config.h>
#include <mist/json.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include <mist/tinythread.h>
#include <mist/triggers.h>
#include <string>

namespace Controller{

  /// Internal list of currently active pushes
  std::map<pid_t, JSON::Value> activePushes;

  /// Internal list of waiting pushes
  std::map<std::string, std::map<std::string, unsigned int> > waitingPushes;

  static bool mustWritePushList = false;
  static bool pushListRead = false;

  /// Immediately starts a push for the given stream to the given target.
  /// Simply calls Util::startPush and stores the resulting PID in the local activePushes map.
  void startPush(const std::string &stream, std::string &target){
    // Cancel if already active
    if (isPushActive(stream, target)){return;}
    std::string originalTarget = target;
    pid_t ret = Util::startPush(stream, target);
    if (ret){
      JSON::Value push;
      push.append(ret);
      push.append(stream);
      push.append(originalTarget);
      push.append(target);
      activePushes[ret] = push;
      mustWritePushList = true;
    }
  }

  void setPushStatus(uint64_t id, const JSON::Value & status){
    if (!activePushes.count(id)){return;}
    activePushes[id][5].extend(status);
  }

  void pushLogMessage(uint64_t id, const JSON::Value & msg){
    JSON::Value &log = activePushes[id][4];
    log.append(msg);
    log.shrink(10);
  }

  bool isPushActive(uint64_t id){
    while (Controller::conf.is_active && !pushListRead){Util::sleep(100);}
    return activePushes.count(id);
  }

  /// Only used internally, to remove pushes
  static void removeActivePush(pid_t id){
    //ignore if the push does not exist
    if (!activePushes.count(id)){return;}

    JSON::Value p = activePushes[id];
    if (Triggers::shouldTrigger("PUSH_END", p[1].asStringRef())){
      std::string payload = p[0u].asString() + "\n" + p[1u].asString() + "\n" + p[2u].asString() + "\n" + p[3u].asString() + "\n" + p[4u].toString() + "\n" + p[5u].toString();
      Triggers::doTrigger("PUSH_END", payload, p[1].asStringRef());
    }

    //actually remove, make sure next pass the new list is written out too
    activePushes.erase(id);
    mustWritePushList = true;
  }

  /// Returns true if the push is currently active, false otherwise.
  bool isPushActive(const std::string &streamname, const std::string &target){
    while (Controller::conf.is_active && !pushListRead){Util::sleep(100);}
    std::set<pid_t> toWipe;
    for (std::map<pid_t, JSON::Value>::iterator it = activePushes.begin(); it != activePushes.end(); ++it){
      if (Util::Procs::isActive(it->first)){
        // Apply variable substitution to make sure another push target does not resolve to the same target
        if (it->second[1u].asStringRef() == streamname){
          std::string activeTarget = it->second[2u].asStringRef();
          std::string cmpTarget = target;
          Util::streamVariables(activeTarget, streamname);
          Util::streamVariables(cmpTarget, streamname);
          if (activeTarget == cmpTarget){
            return true;
          }
        }
      }else{
        toWipe.insert(it->first);
      }
    }
    while (toWipe.size()){
      removeActivePush(*toWipe.begin());
      toWipe.erase(toWipe.begin());
    }
    return false;
  }

  /// Stops any pushes matching the stream name (pattern) and target
  void stopActivePushes(const std::string &streamname, const std::string &target){
    while (Controller::conf.is_active && !pushListRead){Util::sleep(100);}
    std::set<pid_t> toWipe;
    for (std::map<pid_t, JSON::Value>::iterator it = activePushes.begin(); it != activePushes.end(); ++it){
      if (Util::Procs::isActive(it->first)){
        if (it->second[2u].asStringRef() == target &&
            (it->second[1u].asStringRef() == streamname ||
             (*streamname.rbegin() == '+' && it->second[1u].asStringRef().substr(0, streamname.size()) == streamname))){
          Util::Procs::Stop(it->first);
        }
      }else{
        toWipe.insert(it->first);
      }
    }
    while (toWipe.size()){
      removeActivePush(*toWipe.begin());
      toWipe.erase(toWipe.begin());
    }
  }

  /// Immediately stops a push with the given ID
  void stopPush(unsigned int ID){
    if (ID > 1 && activePushes.count(ID)){Util::Procs::Stop(ID);}
  }

  /// Compactly writes the list of pushes to a pointer, assumed to be 8MiB in size
  static void writePushList(char *pwo){
    char *max = pwo + 8 * 1024 * 1024 - 4;
    for (std::map<pid_t, JSON::Value>::iterator it = activePushes.begin(); it != activePushes.end(); ++it){
      // check if the whole entry will fit
      unsigned int entrylen = 4 + 2 + it->second[1u].asStringRef().size() + 2 +
                              it->second[2u].asStringRef().size() + 2 + it->second[3u].asStringRef().size();
      if (pwo + entrylen >= max){return;}
      // write the pid as a 32 bits unsigned integer
      Bit::htobl(pwo, it->first);
      pwo += 4;
      // write the streamname, original target and target, 2-byte-size-prepended
      for (unsigned int i = 1; i < 4; ++i){
        const std::string &itm = it->second[i].asStringRef();
        Bit::htobs(pwo, itm.size());
        memcpy(pwo + 2, itm.data(), itm.size());
        pwo += 2 + itm.size();
      }
    }
    // if it fits, write an ending zero to indicate end of page
    if (pwo <= max){Bit::htobl(pwo, 0);}
  }

  /// Reads the list of pushes from a pointer, assumed to end in four zeroes
  static void readPushList(char *pwo){
    activePushes.clear();
    pid_t p = Bit::btohl(pwo);
    HIGH_MSG("Recovering pushes: %" PRIu32, (uint32_t)p);
    while (p > 1){
      JSON::Value push;
      push.append(p);
      pwo += 4;
      for (uint8_t i = 0; i < 3; ++i){
        uint16_t l = Bit::btohs(pwo);
        push.append(std::string(pwo + 2, l));
        pwo += 2 + l;
      }
      INFO_MSG("Recovered push: %s", push.toString().c_str());
      Util::Procs::remember(p);
      mustWritePushList = true;
      activePushes[p] = push;
      p = Bit::btohl(pwo);
    }
  }

  /// \brief Evaluates <value of currentVariable> <operator> <matchedValue>
  ///        Will apply numerical comparison if passed a numerical matchedValue
  //         and apply lexical comparison if passed a nonnumerical matchedValue
  /// \param operator can be:
  ///        0: boolean true
  ///        1: boolean false
  ///        2: ==
  ///        3: !=
  ///        10: > (numerical comparison)
  ///        11: >= (numerical comparison)
  ///        12: < (numerical comparison)
  ///        13: <= (numerical comparison)
  ///        20 > (lexical comparison)
  ///        21: >= (lexical comparison)
  ///        22: < (lexical comparison)
  ///        23: <= (lexical comparison)
  bool checkCondition(const JSON::Value &currentValue, const uint8_t &comparisonOperator, const JSON::Value &matchedValue){
    std::string currentValueAsString = currentValue.asStringRef();
    if (comparisonOperator == 0){
      return Util::stringToBool(currentValueAsString);
    }else if (comparisonOperator == 1){
      return !Util::stringToBool(currentValueAsString);
    }else if (comparisonOperator == 2){
      return currentValue == matchedValue;
    } else if (comparisonOperator == 3){
      return currentValue != matchedValue;
    }else if (comparisonOperator >= 10 && comparisonOperator < 20){
      return checkCondition(currentValue.asInt(), comparisonOperator, matchedValue.asInt());
    }else{
      return checkCondition(currentValueAsString, comparisonOperator, matchedValue.asStringRef());
    }
  }
  bool checkCondition(const int64_t &currentValue, const uint8_t &comparisonOperator, const int64_t &matchedValue){
    switch (comparisonOperator){
      case 10:
        if (currentValue > matchedValue){return true;}
        break;
      case 11:
        if (currentValue >= matchedValue){return true;}
        break;
      case 12:
        if (currentValue < matchedValue){return true;}
        break;
      case 13:
        if (currentValue <= matchedValue){return true;}
        break;
      default:
        ERROR_MSG("Passed invalid comparison operator of type %u", comparisonOperator);
        break;
    }
    return false;
  }
  bool checkCondition(const std::string &currentValue, const uint8_t &comparisonOperator,const  std::string &matchedValue){
    int lexCmpResult = strcmp(currentValue.c_str(), matchedValue.c_str());
    switch (comparisonOperator){
      case 20:
        if (lexCmpResult > 0){return true;}
        break;
      case 21:
        if (lexCmpResult >= 0){return true;}
        break;
      case 22:
        if (lexCmpResult < 0){return true;}
        break;
      case 23:
        if (lexCmpResult <= 0){return true;}
        break;
      default:
        ERROR_MSG("Passed invalid comparison operator of type %u", comparisonOperator);
        break;
    }
    return false;
  }

  /// \brief Returns true if a push should be active, false if it shouldn't be active
  bool checkPush(JSON::Value &thisPush){
    uint64_t startTime = thisPush[2u].asInt();
    std::string startVariableName = thisPush[4u].asString();
    std::string endVariableName = thisPush[7u].asString();
    // Get sanitized stream name
    std::string stream = thisPush[0u].asString();
    Util::sanitizeName(stream);
    // Skip if we have a start time which is in the future
    if (startTime && *(stream.rbegin()) != '+' && startTime > Util::epoch()){return false;}
    // Check if it supposed to stop
    if (endVariableName.size()){
      // Get current value of configured variable
      std::string currentValue = "$" + endVariableName;
      if (!Util::streamVariables(currentValue, stream)){
        WARN_MSG("Could not find a variable with name `%s`", endVariableName.c_str());
        return false;
      }
      // Get matched value and apply variable substitution
      std::string replacedMatchedValue = thisPush[9u].asString();
      if (replacedMatchedValue.size()){Util::streamVariables(replacedMatchedValue, stream);}
      JSON::Value matchedValue(replacedMatchedValue);
      // Finally indicate that the push should not be active if the end condition resolves to true
      if(checkCondition(JSON::Value(currentValue), thisPush[8u].asInt(), matchedValue)){return false;}
    }
    // Check if it is allowed to start
    if (startVariableName.size()){
      // Get current value of configured variable
      std::string currentValue = "$" + startVariableName;
      if (!Util::streamVariables(currentValue, stream)){
        WARN_MSG("Could not find a variable with name `%s`", startVariableName.c_str());
        return false;
      }
      // Get matched value and apply variable substitution
      std::string replacedMatchedValue = thisPush[6u].asString();
      if (replacedMatchedValue.size()){Util::streamVariables(replacedMatchedValue, stream);}
      JSON::Value matchedValue(replacedMatchedValue);
      // Finally indicate that the push should not be active if the end condition resolves to true
      return checkCondition(JSON::Value(currentValue), thisPush[5u].asInt(), matchedValue);
    }
    return true;
  }

  /// Loops, checking every second if any pushes need restarting.
  void pushCheckLoop(void *np){
    {
      IPC::sharedPage pushReadPage("MstPush", 8 * 1024 * 1024, false, false);
      if (pushReadPage.mapped){
        readPushList(pushReadPage.mapped);
        pushReadPage.master = true;
      }
    }
    pushListRead = true;
    IPC::sharedPage pushPage("MstPush", 8 * 1024 * 1024, true, false);
    while (Controller::conf.is_active){
      // this scope prevents the configMutex from being locked constantly
      {
        tthread::lock_guard<tthread::mutex> guard(Controller::configMutex);
        long long maxspeed = Controller::Storage["push_settings"]["maxspeed"].asInt();
        long long waittime = Controller::Storage["push_settings"]["wait"].asInt();
        long long curCount = 0;
        jsonForEach(Controller::Storage["autopushes"], it){
          std::string stream = (*it)[0u].asStringRef();
          std::string target = (*it)[1u].asStringRef();
          uint64_t startTime = (*it)[2u].asInt();
          uint64_t endTime = (*it)[3u].asInt();
          // Stop any auto pushes which have an elapsed end time
          if (endTime && endTime < Util::epoch()){
            INFO_MSG("Deleting autopush from %s to %s because end time passed", stream.c_str(), target.c_str());
            stopActivePushes(stream, target);
            removePush(*it);
            break;
          }
          // Stop any active push if conditions are not met
          if (!checkPush(*it)){
            if (isPushActive(stream, target)){
              MEDIUM_MSG("Conditions of push `%s->%s` evaluate to false. Stopping push...", stream.c_str(), target.c_str());
              stopActivePushes(stream, target);
            }
            continue;
          }
          // We can continue if it is already running
          if (isPushActive(stream, target)){continue;}
          // Start the push if conditions are met
          if (waittime || startTime){
            std::set<std::string> activeStreams = Controller::getActiveStreams(stream);
            if (activeStreams.size()){
              for (std::set<std::string>::iterator jt = activeStreams.begin();
                    jt != activeStreams.end(); ++jt){
                std::string streamname = *jt;
                if (stream == streamname || (*stream.rbegin() == '+' && streamname.substr(0, stream.size()) == stream)){
                  if (!isPushActive(streamname, target)){
                    if (waitingPushes[streamname][target]++ >= waittime && (curCount < maxspeed || !maxspeed)){
                      waitingPushes[streamname].erase(target);
                      if (!waitingPushes[streamname].size()){waitingPushes.erase(streamname);}
                      MEDIUM_MSG("Conditions of push `%s->%s` evaluate to true. Starting push...", stream.c_str(), target.c_str());
                      startPush(streamname, target);
                      curCount++;
                      // If no end time is given but there is a start time, remove the push after starting it
                      if (startTime && !endTime){
                        removePush(*it);
                        break;
                      }
                    }
                  }
                }
              }
            }
          }
        }
        //Check if any pushes have ended, clean them up
        std::set<pid_t> toWipe;
        for (std::map<pid_t, JSON::Value>::iterator it = activePushes.begin(); it != activePushes.end(); ++it){
          if (!Util::Procs::isActive(it->first)){toWipe.insert(it->first);}
        }
        while (toWipe.size()){
          removeActivePush(*toWipe.begin());
          toWipe.erase(toWipe.begin());
          mustWritePushList = true;
        }
        //write push list to shared memory, for restarting/crash recovery/etc
        if (mustWritePushList && pushPage.mapped){
          writePushList(pushPage.mapped);
          mustWritePushList = false;
        }
      }
      Util::wait(1000); // wait at least a second
    }
    // keep the pushPage if we are restarting, so we can restore state from it
    if (Util::Config::is_restarting){
      pushPage.master = false;
      // forget about all pushes, so they keep running
      for (std::map<pid_t, JSON::Value>::iterator it = activePushes.begin(); it != activePushes.end(); ++it){
        Util::Procs::forget(it->first);
      }
    }
  }

  /// Gives a list of all currently active pushes
  void listPush(JSON::Value &output){
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
      removeActivePush(*toWipe.begin());
      toWipe.erase(toWipe.begin());
    }
  }

  /// Adds a push to the list of auto-pushes.
  /// Auto-starts currently active matches immediately.
  void addPush(JSON::Value &request, JSON::Value &response){
    JSON::Value newPush;
    if (request.isArray()){
      newPush = request;
    }else{
      if (!request.isMember("stream") || !request["stream"].isString()){
        ERROR_MSG("Automatic push not added: it does not contain a valid stream name");
        return;
      }
      newPush.append(request["stream"]);
      if (!request.isMember("target") || !request["target"].isString()){
        ERROR_MSG("Automatic push not added: it does not contain a valid target");
        return;
      }
      newPush.append(request["target"]);
      if (request.isMember("scheduletime") && request["scheduletime"].isInt()){
        newPush.append(request["scheduletime"]);
      }else{
        newPush.append(0u);
      }
      if (request.isMember("completetime") && request["completetime"].isInt()){
        newPush.append(request["completetime"]);
      }else{
        newPush.append(0u);
      }
      if (request.isMember("startVariableName")){
        newPush.append(request["startVariableName"]);
      }else{
        newPush.append("");
      }
      if (request.isMember("startVariableOperator")){
        newPush.append(request["startVariableOperator"]);
      }else{
        newPush.append(0);
      }
      if (request.isMember("startVariableValue")){
        newPush.append(request["startVariableValue"]);
      }else{
        newPush.append("");
      }
      if (request.isMember("endVariableName")){
        newPush.append(request["endVariableName"]);
      }else{
        newPush.append("");
      }
      if (request.isMember("endVariableOperator")){
        newPush.append(request["endVariableOperator"]);
      }else{
        newPush.append(0);
      }
      if (request.isMember("endVariableValue")){
        newPush.append(request["endVariableValue"]);
      }else{
        newPush.append("");
      }
    }
    long long epo = Util::epoch();
    if (request.size() < 2){
      ERROR_MSG("Automatic push not added: should contain at least a stream name and target");
      return;
    }
    // Init optional fields if they were omitted from the addPush request
    // We only have a stream and target, so fill in the scheduletime and completetime
    while(newPush.size() < 4){newPush.append(0u);}
    // The request seems to be using variables and likely skipped the scheduletime and completetime set to 0
    if (newPush[2].isString()){
      JSON::Value modPush;
      modPush.append(newPush[0u]);
      modPush.append(newPush[1u]);
      modPush.append(0u);
      modPush.append(0u);
      for (uint8_t idx = 2; idx < newPush.size(); idx++){
        modPush.append(newPush[idx]);
      }
      newPush = modPush;
    }
    // Variable conditions are used. We should have either 7 (only start variable condition) or 10 values (start + stop variable conditions)
    if (newPush.size() > 4){
      if (newPush.size() == 7){
        newPush.append("");
        newPush.append(0u);
        newPush.append("");
      } else if (newPush.size() != 10){
        ERROR_MSG("Automatic push not added: passed incomplete data for the start or stop variable");
        return;
      }
    }else{
      // Init the start and stop variable conditions
      newPush.append("");
      newPush.append(0u);
      newPush.append("");
      newPush.append("");
      newPush.append(0u);
      newPush.append("");
    }
    // Make sure all start variable values have been initialised
    if (newPush.size() == 7 && (!newPush[5u].isString() || !newPush[6u].isInt() || !newPush[7u].isString()));
    // Make sure all stop variable values have been initialised
    if (newPush.size() == 10 && (!newPush[8u].isString() || !newPush[9u].isInt() || !newPush[10u].isString()));
    // Final sanity checks on input
    std::string stream = newPush[0u].asStringRef();
    std::string target = newPush[1u].asStringRef();
    uint64_t startTime = newPush[2u].asInt();
    uint64_t endTime = newPush[3u].asInt();
    if (endTime && endTime <= epo){
      ERROR_MSG("Automatic push not added: removal time is in the past! (%" PRIu64 " <= %lld)", endTime, epo);
      return;
    }

    // If we have an existing push: edit it
    bool shouldSave = true;
    jsonForEach(Controller::Storage["autopushes"], it){
      if ((*it)[0u] == stream && (*it)[1u] == target){
        (*it) = newPush;
        shouldSave = false;
      }
    }
    // If a newly added push only has a defined start time, immediately start it and never save it
    if (startTime && !endTime){
      INFO_MSG("Immediately starting push %s->%s as the added push only has a defined start time"
                , stream.c_str(), target.c_str());
      startPush(stream, target);
      // Return push list
      response["push_auto_list"] = Controller::Storage["autopushes"];
      return;
    }
    // Save as a new variable if we have not edited an existing variable
    if (shouldSave){
      Controller::Storage["autopushes"].append(newPush);
    }
    // and start it immediately if conditions are met
    if (!checkPush(newPush)){return;}
    std::set<std::string> activeStreams = Controller::getActiveStreams(stream);
    if (activeStreams.size()){
      for (std::set<std::string>::iterator jt = activeStreams.begin();
            jt != activeStreams.end(); ++jt){
        std::string streamname = *jt;
        if (stream == streamname || (*stream.rbegin() == '+' && streamname.substr(0, stream.size()) == stream)){
          startPush(streamname, target);
        }
      }
    }
    // Return push list
    response["push_auto_list"] = Controller::Storage["autopushes"];
  }

  /// Removes a push from the list of auto-pushes and returns the new list of pushes
  /// Does not stop currently active matching pushes.
  void removePush(const JSON::Value &request, JSON::Value &response){
    removePush(request);
    // Return push list
    response["push_auto_list"] = Controller::Storage["autopushes"];
  }

  /// Removes a push from the list of auto-pushes
  void removePush(const JSON::Value &pushInfo){
    JSON::Value delPush;
    if (pushInfo.isString()){
      removeAllPush(pushInfo.asStringRef());
      return;
    }
    if (pushInfo.isArray()){
      delPush = pushInfo;
    }else{
      delPush.append(pushInfo["stream"]);
      delPush.append(pushInfo["target"]);
    }
    JSON::Value newautopushes;
    jsonForEach(Controller::Storage["autopushes"], it){
      if ((*it) != delPush){newautopushes.append(*it);}
    }
    Controller::Storage["autopushes"] = newautopushes;
  }

  /// Removes all auto pushes of a given streamname
  void removeAllPush(const std::string &streamname){
    JSON::Value newautopushes;
    jsonForEach(Controller::Storage["autopushes"], it){
      if ((*it)[0u] != streamname){newautopushes.append(*it);}
    }
    Controller::Storage["autopushes"] = newautopushes;
  }

  /// Starts all configured auto pushes for the given stream.
  void doAutoPush(std::string &streamname){
    jsonForEach(Controller::Storage["autopushes"], it){
      if ((*it)[2u].asInt() && (*it)[2u].asInt() < Util::epoch()){continue;}
      const std::string &pStr = (*it)[0u].asStringRef();
      if (pStr == streamname || (*pStr.rbegin() == '+' && streamname.substr(0, pStr.size()) == pStr)){
        std::string stream = streamname;
        Util::sanitizeName(stream);
        // Check variable condition if it exists
        if((*it)[4u].asStringRef().size() && !checkPush(*it)){continue;}
        std::string target = (*it)[1u];
        startPush(stream, target);
      }
    }
  }

  void pushSettings(const JSON::Value &request, JSON::Value &response){
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
}// namespace Controller
