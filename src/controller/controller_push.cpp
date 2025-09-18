#include "controller_push.h"
#include "controller_statistics.h"
#include "controller_storage.h"
#include <mist/auth.h>
#include <mist/bitfields.h>
#include <mist/config.h>
#include <mist/json.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <string>
#include <mutex>

namespace Controller{

  /// Internal list of currently active pushes
  std::map<pid_t, JSON::Value> activePushes;
  std::recursive_mutex actPushMut;

  /// Internal list of waiting pushes
  std::map<std::string, std::map<std::string, unsigned int> > waitingPushes;

  static bool mustWritePushList = false;
  static bool pushListRead = false;

  /// Immediately starts a push for the given stream to the given target.
  /// Simply calls Util::startPush and stores the resulting PID in the local activePushes map.
  void startPush(const std::string &stream, std::string &target){
    std::lock_guard<std::recursive_mutex> actGuard(actPushMut);
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

  /// Canonizes the autopush argument from all previously used syntaxes into modern syntax
  JSON::Value makePushObject(const JSON::Value & input){
    JSON::Value ret; 
    if (input.isArray() && input.size() >= 2){
      ret["stream"] = input[0u].asStringRef();
      ret["target"] = input[1u].asStringRef();
      if (input.size() >= 3 && input[2u].asInt()){ret["scheduletime"] = input[2u].asInt();}
      if (input.size() >= 4 && input[3u].asInt()){ret["completetime"] = input[3u].asInt();}
      if (input.size() >= 7 && input[4u].asStringRef().size()){
        ret["start_rule"][0u] = input[4u].asStringRef();
        ret["start_rule"][1u] = input[5u].asInt();
        ret["start_rule"][2u] = input[6u];
      }
      if (input.size() >= 10 && input[7u].asStringRef().size()){
        ret["end_rule"][0u] = input[7u].asStringRef();
        ret["end_rule"][1u] = input[8u].asInt();
        ret["end_rule"][2u] = input[9u];
      }
      return ret;
    }
    if (input.isObject() && input.isMember("stream") && input.isMember("target")){
      ret["stream"] = input["stream"];
      ret["target"] = input["target"];
      if (input.isMember("x-LSP-notes")) { ret["x-LSP-notes"] = input["x-LSP-notes"]; }
      if (input.isMember("scheduletime") && input["scheduletime"].asInt()){
        ret["scheduletime"] = input["scheduletime"].asInt();
      }
      if (input.isMember("completetime") && input["completetime"].asInt()){
        ret["completetime"] = input["completetime"].asInt();
      }
      if (input.isMember("start_rule") && input["start_rule"].isArray() && input["start_rule"].size() == 3){
        ret["start_rule"] = input["start_rule"];
      }
      if (input.isMember("end_rule") && input["end_rule"].isArray() && input["end_rule"].size() == 3){
        ret["end_rule"] = input["end_rule"];
      }
      if (input.isMember("startVariableName") && input.isMember("startVariableOperator") && input.isMember("startVariableValue")){
        ret["start_rule"][0u] = input["startVariableName"];
        ret["start_rule"][1u] = input["startVariableOperator"];
        ret["start_rule"][2u] = input["startVariableValue"];
      }
      if (input.isMember("endVariableName") && input.isMember("endVariableOperator") && input.isMember("endVariableValue")){
        ret["end_rule"][0u] = input["endVariableName"];
        ret["end_rule"][1u] = input["endVariableOperator"];
        ret["end_rule"][2u] = input["endVariableValue"];
      }
      if (input.isMember("inhibit")) { ret["inhibit"] = input["inhibit"]; }
      return ret;
    }
    ret.null();
    return ret;
  }

  void setPushStatus(uint64_t id, const JSON::Value & status){
    std::lock_guard<std::recursive_mutex> actGuard(actPushMut);
    if (!activePushes.count(id)){return;}
    activePushes[id][5].extend(status);
  }

  void pushLogMessage(uint64_t id, const JSON::Value & msg){
    std::lock_guard<std::recursive_mutex> actGuard(actPushMut);
    if (!activePushes.count(id)){return;}
    JSON::Value &log = activePushes[id][4];
    log.append(msg);
    log.shrink(10);
  }

  bool isPushActive(uint64_t id){
    while (Controller::conf.is_active && !pushListRead){Util::sleep(100);}
    {
      std::lock_guard<std::recursive_mutex> actGuard(actPushMut);
      return activePushes.count(id);
    }
  }

  /// Only used internally, to remove pushes
  static void removeActivePush(pid_t id){
    JSON::Value p;

    {
      std::lock_guard<std::recursive_mutex> actGuard(actPushMut);
      //ignore if the push does not exist
      if (!activePushes.count(id)){return;}
      p = activePushes[id];
      //actually remove, make sure next pass the new list is written out too
      activePushes.erase(id);
      mustWritePushList = true;
    }

    if (Triggers::shouldTrigger("PUSH_END", p[1].asStringRef())){
      std::string payload = p[0u].asString() + "\n" + p[1u].asString() + "\n" + p[2u].asString() + "\n" + p[3u].asString() + "\n" + p[4u].toString() + "\n" + p[5u].toString();
      Triggers::doTrigger("PUSH_END", payload, p[1].asStringRef());
    }

  }

  /// Returns true if the push is currently active, false otherwise.
  bool isPushActive(const std::string &streamname, const std::string &target){
    while (Controller::conf.is_active && !pushListRead){Util::sleep(100);}
    {
      std::lock_guard<std::recursive_mutex> actGuard(actPushMut);
      std::set<pid_t> toWipe;
      for (std::map<pid_t, JSON::Value>::iterator it = activePushes.begin(); it != activePushes.end(); ++it){
        if (Util::Procs::isActive(it->first)) {
          if (it->second[1u].asStringRef() == streamname){
            // First compare as-is, in case the string is simply identical
            std::string activeTarget = it->second[2u].asStringRef();
            std::string cmpTarget = target;
            if (activeTarget == cmpTarget) { return true; }
            // Also apply variable substitution to make sure another push target does not resolve to the same target
            Util::streamVariables(activeTarget, streamname);
            Util::streamVariables(cmpTarget, streamname);
            if (activeTarget == cmpTarget) { return true; }
          }
        } else {
          toWipe.insert(it->first);
        }
      }
      while (toWipe.size()){
        removeActivePush(*toWipe.begin());
        toWipe.erase(toWipe.begin());
      }
    }
    return false;
  }

  /// Stops any pushes matching the stream name (pattern) and target
  void stopActivePushes(const std::string &streamname, const std::string &target){
    while (Controller::conf.is_active && !pushListRead){Util::sleep(100);}
    {
      std::lock_guard<std::recursive_mutex> actGuard(actPushMut);
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
  }

  /// Immediately stops a push with the given ID
  void stopPush(unsigned int ID){
    std::lock_guard<std::recursive_mutex> actGuard(actPushMut);
    if (ID > 1 && activePushes.count(ID)){Util::Procs::Stop(ID);}
  }

  /// Immediately stops all pushes of the given stream
  void stopPush(const std::string & stream) {
    for (std::map<pid_t, JSON::Value>::iterator it = activePushes.begin(); it != activePushes.end(); ++it) {
      if (it->second[1].asStringRef() == stream && Util::Procs::isActive(it->first)) { Util::Procs::Stop(it->first); }
    }
  }

  /// Stops a push with the given ID by sending HUP, which will complete sending current data and then disconnect
  void stopPushGraceful(unsigned int ID) {
    if (ID > 1 && activePushes.count(ID)) { Util::Procs::hangup(ID); }
  }

  /// Stops all pushes of the given stream by sending HUP, which will complete sending current data and then disconnect
  void stopPushGraceful(const std::string & stream) {
    for (std::map<pid_t, JSON::Value>::iterator it = activePushes.begin(); it != activePushes.end(); ++it) {
      if (it->second[1].asStringRef() == stream && Util::Procs::isActive(it->first)) { Util::Procs::hangup(it->first); }
    }
  }

  /// Compactly writes the list of pushes to a pointer, assumed to be 8MiB in size
  static void writePushList(char *pwo){
    std::lock_guard<std::recursive_mutex> actGuard(actPushMut);
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
  void initPushCheck() {
    size_t recoverCount = 0;
    {
      std::lock_guard<std::recursive_mutex> actGuard(actPushMut);
      IPC::sharedPage pushReadPage("/MstPush", 8 * 1024 * 1024, false, false);
      char * pwo = pushReadPage.mapped;
      if (pwo){
        pushReadPage.master = true;
        activePushes.clear();
        uint32_t p = Bit::btohl(pwo);
        while (p > 1){
          JSON::Value push;
          push.append(p);
          pwo += 4;
          for (uint8_t i = 0; i < 3; ++i){
            uint16_t l = Bit::btohs(pwo);
            push.append(std::string(pwo + 2, l));
            pwo += 2 + l;
          }
          Util::Procs::remember(p);
          mustWritePushList = true;
          activePushes[p] = push;
          ++recoverCount;
          p = Bit::btohl(pwo);
        }
      }
      pushListRead = true;
    }
    INFO_MSG("Recovered %zu pushes:", recoverCount);
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

  /// \brief Returns what the push rule currently evaluates to
  bool checkPushRule(const std::string & stream, const JSON::Value &rule){
    // Invalid rule? Always false.
    if (!rule.isArray() || rule.size() < 3){return false;}

    // Get configured variable
    std::string variable = "$" + rule[0u].asStringRef();
    if (!Util::streamVariables(variable, stream)){
      WARN_MSG("Could not find a variable with name \"%s\"", rule[0u].asStringRef().c_str());
      return false;
    }

    // Get value to compare to
    std::string compare = rule[2u].asStringRef();
    if (compare.size()){Util::streamVariables(compare, stream);}

    return checkCondition(JSON::Value(variable), rule[1u].asInt(), JSON::Value(compare));
  }

  bool checkPush(const JSON::Value & thisPush){
    // Get sanitized stream name
    std::string stream = thisPush["stream"].asString();
    Util::sanitizeName(stream);

    // Skip if we have a start time which is in the future
    if (thisPush.isMember("scheduletime") && *(stream.rbegin()) != '+' && thisPush["scheduletime"].asInt() > Util::epoch()){
      return false;
    }

    // Check if it supposed to stop
    if (thisPush.isMember("end_rule") && checkPushRule(stream, thisPush["end_rule"])){return false;}

    // Check if it is allowed to start
    if (thisPush.isMember("start_rule")){
      return checkPushRule(stream, thisPush["start_rule"]);
    }
    // Without start rule, default to true.
    return true;
  }

  static IPC::sharedPage pushPage;

  void deinitPushCheck() {
    // keep the pushPage if we are restarting, so we can restore state from it
    if (Util::Config::is_restarting) {
      pushPage.master = false;
      // forget about all pushes, so they keep running
      std::lock_guard<std::recursive_mutex> actGuard(actPushMut);
      for (std::map<pid_t, JSON::Value>::iterator it = activePushes.begin(); it != activePushes.end(); ++it) {
        Util::Procs::forget(it->first);
      }
    }
  }

  /// Loops, checking every second if any pushes need restarting.
  size_t runPushCheck() {
    std::lock_guard<std::mutex> guard(Controller::configMutex);
    long long maxspeed = Controller::Storage["push_settings"]["maxspeed"].asInt();
    long long waittime = Controller::Storage["push_settings"]["wait"].asInt();
    long long curCount = 0;
    jsonForEach (Controller::Storage["auto_push"], it) {
      // Ignore invalid entries
      if (!it->isMember("stream") || !it->isMember("target")) { continue; }

      std::string stream = (*it)["stream"].asStringRef();
      std::string target = (*it)["target"].asStringRef();
      // Stop any auto pushes which have an elapsed end time
      if (it->isMember("completetime") && (*it)["completetime"].asInt() < Util::epoch()) {
        INFO_MSG("Deleting autopush %s because end time passed", it.key().c_str());
        stopActivePushes(stream, target);
        removePush(it.key());
        break;
      }
      // Stop any active push if conditions are not met
      if (!checkPush(*it)) {
        if (isPushActive(stream, target)) {
          MEDIUM_MSG("Conditions of push %s evaluate to false. Stopping push...", it.key().c_str());
          stopActivePushes(stream, target);
        }
        continue;
      }
      // We can continue if it is already running
      if (isPushActive(stream, target)) { continue; }
      // Start the push if conditions are met
      if (waittime || it->isMember("scheduletime")) {
        std::set<std::string> activeStreams = Controller::getActiveStreams(stream);
        if (activeStreams.size()) {
          for (const std::string & streamname : activeStreams) {
            if (!isPushActive(streamname, target)) {
              if (waitingPushes[streamname][target]++ >= waittime && (curCount < maxspeed || !maxspeed)) {
                waitingPushes[streamname].erase(target);
                if (!waitingPushes[streamname].size()) { waitingPushes.erase(streamname); }

                // If the inhibitor matches, do _not_ start the push after all
                if (it->isMember("inhibit")) {
                  if ((*it)["inhibit"].isString() && (*it)["inhibit"].asStringRef().size()) {
                    if (Controller::streamMatches(streamname, (*it)["inhibit"].asStringRef())) { continue; }
                  }
                  if ((*it)["inhibit"].isArray()) {
                    bool noMatch = false;
                    jsonForEachConst ((*it)["inhibit"], ii) {
                      if (Controller::streamMatches(streamname, ii->asStringRef())) {
                        noMatch = true;
                        break;
                      }
                    }
                    if (noMatch) { continue; }
                  }
                }

                MEDIUM_MSG("Conditions of push \"%s\" evaluate to true. Starting push...", it.key().c_str());
                std::string tmpTarget = target;
                startPush(streamname, tmpTarget);
                curCount++;
                // If no end time is given but there is a start time, remove the push after starting it
                if (it->isMember("scheduletime") && !it->isMember("completetime")) {
                  removePush(*it);
                  break;
                }
              }
            }
          }
        }
      }
    }
    // Check if any pushes have ended, clean them up
    std::set<pid_t> toWipe;
    {
      std::lock_guard<std::recursive_mutex> actGuard(actPushMut);
      for (std::map<pid_t, JSON::Value>::iterator it = activePushes.begin(); it != activePushes.end(); ++it){
        if (!Util::Procs::isActive(it->first)) { toWipe.insert(it->first); }
      }
    }
    while (toWipe.size()) {
      removeActivePush(*toWipe.begin());
      toWipe.erase(toWipe.begin());
      mustWritePushList = true;
    }
    // write push list to shared memory, for restarting/crash recovery/etc
    if (!pushPage) { pushPage.init("/MstPush", 8 * 1024 * 1024, true, false); }
    if (mustWritePushList && pushPage.mapped) {
      writePushList(pushPage.mapped);
      mustWritePushList = false;
    }
    return 1000;
  }

  /// Gives a list of all currently active pushes
  void listPush(JSON::Value &output){
    std::set<pid_t> toWipe;
    {
      std::lock_guard<std::recursive_mutex> actGuard(actPushMut);
      output.null();
      for (std::map<pid_t, JSON::Value>::iterator it = activePushes.begin(); it != activePushes.end(); ++it){
        if (Util::Procs::isActive(it->first)){
          output.append(it->second);
        }else{
          toWipe.insert(it->first);
        }
      }
    }
    while (toWipe.size()){
      removeActivePush(*toWipe.begin());
      toWipe.erase(toWipe.begin());
    }
  }

  /// Adds a push to the list of auto-pushes.
  /// Auto-starts currently active matches immediately.
  void addPush(const JSON::Value &request, JSON::Value &response){
    if (
        (request.isArray() && request.size() >= 2 && request[0u].isString() && request[1u].isString())
        ||
        (request.isObject() && request.isMember("stream") && request.isMember("target"))
        ){
      // Object- or Array-style with direct stream/target arguments (and optional other params)
      JSON::Value newPush = makePushObject(request);
      if (newPush.isNull()){
        WARN_MSG("Not a valid autopush definition: %s", request.toString().c_str());
        return;
      }
      Controller::Storage["auto_push"][Secure::md5(newPush.toString())] = newPush;
    } else if (request.isArray() || request.isObject()){
      // Array or Object list of pushes to add
      jsonForEachConst(request, it){
        JSON::Value newPush = makePushObject(*it);
        if (newPush.isNull()){
          WARN_MSG("Not a valid autopush definition: %s", it->toString().c_str());
          continue;
        }
        // Use the given key, if any (only objects have them)
        std::string key = it.key();
        if (!key.size()){
          key = Secure::md5(newPush.toString());
        }
        Controller::Storage["auto_push"][key] = newPush;
      }
    }
  }

  /// Removes a push from the list of auto-pushes
  /// Does not stop currently active matching pushes.
  void removePush(const JSON::Value &pushInfo){
    // If it's a push specification, remove all matching autopushes
    JSON::Value delPush = makePushObject(pushInfo);
    if (!delPush.isNull()){
      jsonForEach(Controller::Storage["auto_push"], it){
        if ((*it) == delPush){it.remove();}
      }
      return;
    }
    // If it's a string...
    if (pushInfo.isString()){
      // Remove by identifier if it exists
      if (Controller::Storage["auto_push"].isMember(pushInfo.asStringRef())){
        Controller::Storage["auto_push"].removeMember(pushInfo.asStringRef());
      }else{
        // Otherwise assume it's a stream name and remove all by stream name
        removeAllPush(pushInfo.asStringRef());
      }
      return;
    }
    // Array of strings or push specifications? Delete them all one by one.
    if (pushInfo.isArray()){
      jsonForEachConst(pushInfo, it){removePush(*it);}
    }
  }

  /// Removes all auto pushes of a given streamname
  void removeAllPush(const std::string &streamname){
    jsonForEach(Controller::Storage["auto_push"], it){
      if (!it->isMember("stream") || (*it)["stream"] == streamname){
        it.remove();
      }
    }
  }

  /// Starts all configured auto pushes for the given stream.
  void doAutoPush(std::string &streamname){
    jsonForEachConst(Controller::Storage["auto_push"], it){
      // Skip invalid
      if (!it->isMember("stream") || !it->isMember("target")){continue;}
      // Skip if scheduled in the future
      if (it->isMember("scheduletime") && (*it)["scheduletime"].asInt() < Util::epoch()){continue;}
      // Check if the stream name matches
      if (!Controller::streamMatches(streamname, (*it)["stream"].asStringRef())) { continue; }

      // If the inhibitor matches, do _not_ start the push after all
      if (it->isMember("inhibit")) {
        if ((*it)["inhibit"].isString() && (*it)["inhibit"].asStringRef().size()) {
          if (Controller::streamMatches(streamname, (*it)["inhibit"].asStringRef())) { continue; }
        }
        if ((*it)["inhibit"].isArray()) {
          bool noMatch = false;
          jsonForEachConst ((*it)["inhibit"], ii) {
            if (Controller::streamMatches(streamname, ii->asStringRef())) {
              noMatch = true;
              break;
            }
          }
          if (noMatch) { continue; }
        }
      }

      // Clean up the stream name if needed
      std::string stream = streamname;
      Util::sanitizeName(stream);
      // Check variable conditions if they exist
      if (it->isMember("end_rule") && checkPushRule(stream, (*it)["end_rule"])) { continue; }
      if (it->isMember("start_rule") && !checkPushRule(stream, (*it)["start_rule"])) { continue; }
      // Actually do the push; use a temp target as it might be rewritten
      std::string target = (*it)["target"].asStringRef();
      startPush(stream, target);
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
