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
        if (it->second[1u].asStringRef() == streamname && it->second[2u].asStringRef() == target){
          return true;
        }
      }else{
        toWipe.insert(it->first);
      }
    }
    while (toWipe.size()){
      removeActivePush(*toWipe.begin());
      toWipe.erase(toWipe.begin());
    }
    //Check if the stream has a pull source, if yes, pretend all pushes are already active
    DTSC::Meta M(streamname, false);
    if (M && M.getSource().find("INTERNAL_ONLY:dtsc") != std::string::npos){return true;}
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
          if (it->size() > 3 && (*it)[3u].asInt() < Util::epoch()){
            INFO_MSG("Deleting autopush from %s to %s because end time passed",
                     (*it)[0u].asStringRef().c_str(), (*it)[1u].asStringRef().c_str());
            stopActivePushes((*it)[0u], (*it)[1u]);
            removePush(*it);
            break;
          }
          if (it->size() > 2 && *((*it)[0u].asStringRef().rbegin()) != '+'){
            if ((*it)[2u].asInt() <= Util::epoch()){
              std::string streamname = (*it)[0u];
              std::string target = (*it)[1u];
              if (!isPushActive(streamname, target)){
                if (waitingPushes[streamname][target]++ >= waittime && (curCount < maxspeed || !maxspeed)){
                  waitingPushes[streamname].erase(target);
                  if (!waitingPushes[streamname].size()){waitingPushes.erase(streamname);}
                  startPush(streamname, target);
                  curCount++;
                }
              }
            }
            continue;
          }
          if (waittime || it->size() > 2){
            const std::string &pStr = (*it)[0u].asStringRef();
            std::set<std::string> activeStreams = Controller::getActiveStreams(pStr);
            if (activeStreams.size()){
              for (std::set<std::string>::iterator jt = activeStreams.begin();
                   jt != activeStreams.end(); ++jt){
                std::string streamname = *jt;
                std::string target = (*it)[1u];
                if (pStr == streamname || (*pStr.rbegin() == '+' && streamname.substr(0, pStr.size()) == pStr)){
                  if (!isPushActive(streamname, target)){
                    if (waitingPushes[streamname][target]++ >= waittime && (curCount < maxspeed || !maxspeed)){
                      waitingPushes[streamname].erase(target);
                      if (!waitingPushes[streamname].size()){waitingPushes.erase(streamname);}
                      startPush(streamname, target);
                      curCount++;
                    }
                  }
                }
              }
            }
          }
          if (it->size() == 3){
            removePush(*it);
            break;
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
  void addPush(JSON::Value &request){
    JSON::Value newPush;
    if (request.isArray()){
      newPush = request;
    }else{
      newPush.append(request["stream"]);
      newPush.append(request["target"]);
      bool startTime = false;
      if (request.isMember("scheduletime") && request["scheduletime"].isInt()){
        newPush.append(request["scheduletime"]);
        startTime = true;
      }
      if (request.isMember("completetime") && request["completetime"].isInt()){
        if (!startTime){newPush.append(0u);}
        newPush.append(request["completetime"]);
      }
    }
    long long epo = Util::epoch();
    if (newPush.size() > 3 && newPush[3u].asInt() <= epo){
      WARN_MSG("Automatic push not added: removal time is in the past! (%" PRId64 " <= %" PRIu64 ")",
               newPush[3u].asInt(), Util::epoch());
      return;
    }
    bool edited = false;
    jsonForEach(Controller::Storage["autopushes"], it){
      if ((*it)[0u] == newPush[0u] && (*it)[1u] == newPush[1u]){
        (*it) = newPush;
        edited = true;
      }
    }
    if (!edited && (newPush.size() != 3 || newPush[2u].asInt() > epo)){
      Controller::Storage["autopushes"].append(newPush);
    }
    if (newPush.size() < 3 || newPush[2u].asInt() <= epo){
      if (newPush.size() > 2 && *(newPush[0u].asStringRef().rbegin()) != '+'){
        std::string streamname = newPush[0u].asStringRef();
        std::string target = newPush[1u].asStringRef();
        startPush(streamname, target);
        return;
      }
      const std::string &pStr = newPush[0u].asStringRef();
      std::set<std::string> activeStreams = Controller::getActiveStreams(pStr);
      if (activeStreams.size()){
        std::string target = newPush[1u].asStringRef();
        for (std::set<std::string>::iterator it = activeStreams.begin(); it != activeStreams.end(); ++it){
          std::string streamname = *it;
          if (pStr == streamname || (*pStr.rbegin() == '+' && streamname.substr(0, pStr.size()) == pStr)){
            std::string tmpName = streamname;
            std::string tmpTarget = target;
            startPush(tmpName, tmpTarget);
          }
        }
      }
    }
  }

  /// Removes a push from the list of auto-pushes.
  /// Does not stop currently active matching pushes.
  void removePush(const JSON::Value &request){
    JSON::Value delPush;
    if (request.isString()){
      removeAllPush(request.asStringRef());
      return;
    }
    if (request.isArray()){
      delPush = request;
    }else{
      delPush.append(request["stream"]);
      delPush.append(request["target"]);
    }
    JSON::Value newautopushes;
    jsonForEach(Controller::Storage["autopushes"], it){
      if ((*it) != delPush){newautopushes.append(*it);}
    }
    Controller::Storage["autopushes"] = newautopushes;
  }

  /// Removes a push from the list of auto-pushes.
  /// Does not stop currently active matching pushes.
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
      if (it->size() > 2 && (*it)[2u].asInt() < Util::epoch()){continue;}
      const std::string &pStr = (*it)[0u].asStringRef();
      if (pStr == streamname || (*pStr.rbegin() == '+' && streamname.substr(0, pStr.size()) == pStr)){
        std::string stream = streamname;
        Util::sanitizeName(stream);
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
