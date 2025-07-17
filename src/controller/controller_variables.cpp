#include "controller_variables.h"
#include "controller_storage.h"
#include <mist/downloader.h>
#include <mist/bitfields.h>
#include <mist/config.h>
#include <mist/json.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <mist/procs.h>
#include <string>

class VarExec {
  public:
    pid_t pid;
    Socket::Connection C;
    size_t timerNo;
    std::string retVal;
    ~VarExec() {
      if (C) { C.close(); }
    }
};

std::map<std::string, HTTP::Downloader> varDowners;
std::map<std::string, VarExec> varExecs;
std::set<std::string> varCollect;

namespace Controller{
  // Indicates whether the shared memory page is stale compared to the server config
  static bool mutateShm = true;
  size_t variableTimer = std::string::npos;
  // Size of the shared memory page
  static uint64_t pageSize = CUSTOM_VARIABLES_INITSIZE;

  /// Check if any variables require updating
  size_t variableRun() {
    uint64_t now = Util::bootMS();

    // Collect anything that needs collecting
    for (const std::string & V : varCollect) {
      if (!varExecs.count(V)) { continue; }
      VarExec & ve = varExecs[V];
      // Kill process if still running
      if (Util::Procs::childRunning(ve.pid)) { Util::Procs::Murder(ve.pid); }
      // Remove timer if it still was set
      if (ve.timerNo != -1) {
        Controller::E.removeInterval(ve.timerNo);
        ve.timerNo = -1;
      }

      // Update the variable with any received data
      mutateVariable(V, ve.retVal);

      // Delete variable executable status object
      varExecs.erase(V);
    }
    varCollect.clear();

    uint64_t nextCheck = now + 10000;
    // Check if any custom variable target needs to be run
    IPC::sharedPage variablePage(SHM_CUSTOM_VARIABLES, 0, false, false);
    if (variablePage.mapped) {
      Util::RelAccX varAccX(variablePage.mapped, false);
      if (varAccX.isReady()) {
        Util::RelAccXFieldData fName = varAccX.getFieldData("name");
        Util::RelAccXFieldData fTarget = varAccX.getFieldData("target");
        Util::RelAccXFieldData fInterval = varAccX.getFieldData("interval");
        Util::RelAccXFieldData fLastRun = varAccX.getFieldData("lastRun");
        Util::RelAccXFieldData fWaitTime = varAccX.getFieldData("waitTime");
        for (size_t i = 0; i < varAccX.getEndPos(); i++) {
          std::string name = varAccX.getPointer(fName, i);
          if (!name.size()) { continue; }
          std::string target = varAccX.getPointer(fTarget, i);
          if (!target.size()) { continue; }
          uint32_t interval = varAccX.getInt(fInterval, i);
          uint64_t lastRun = varAccX.getInt(fLastRun, i);
          uint32_t waitTime = varAccX.getInt(fWaitTime, i);
          if (!lastRun || (interval && (lastRun + interval < now))) {
            // Set the wait time to the interval, or 1 second if it is less than 1 second
            if (!waitTime) { waitTime = interval; }
            if (waitTime < 1000) { waitTime = 1000; }
            if (!runVariableTarget(name, target, waitTime)) {
              if (interval && now + interval < nextCheck) { nextCheck = now + interval; }
            }
          } else {
            if (interval && lastRun + interval < nextCheck) { nextCheck = lastRun + interval; }
          }
        }
      }
    }
    // Write variables from the server config to shm if any data has changed
    if (mutateShm) { writeToShm(); }
    return nextCheck;
  }

  void variableDeinit() {
    // Cleanup shared memory page
    IPC::sharedPage variablesPage(SHM_CUSTOM_VARIABLES, pageSize, false, false);
    if (variablesPage.mapped) { variablesPage.master = true; }
  }

  /// \brief Writes custom variable from the server config to shared memory
  void writeToShm() {
    uint64_t variableCount = Controller::Storage["variables"].size();
    IPC::sharedPage variablesPage(SHM_CUSTOM_VARIABLES, pageSize, false, false);
    // If we have an existing page, set the reload flag
    if (variablesPage.mapped) {
      variablesPage.master = true;
      Util::RelAccX varAccX = Util::RelAccX(variablesPage.mapped, false);
      // Check if we need a bigger page
      uint64_t sizeRequired = varAccX.getOffset() + varAccX.getRSize() * variableCount;
      if (pageSize < sizeRequired) { pageSize = sizeRequired; }
      varAccX.setReload();
    }
    // Close & unlink any existing page and create a new one
    variablesPage.close();
    variablesPage.init(SHM_CUSTOM_VARIABLES, pageSize, true, false);
    Util::RelAccX varAccX = Util::RelAccX(variablesPage.mapped, false);
    varAccX = Util::RelAccX(variablesPage.mapped, false);
    varAccX.addField("name", RAX_32STRING);
    varAccX.addField("target", RAX_512STRING);
    varAccX.addField("interval", RAX_32UINT);
    varAccX.addField("lastRun", RAX_64UINT);
    varAccX.addField("lastVal", RAX_128STRING);
    varAccX.addField("waitTime", RAX_32UINT);
    // Set amount of records that can fit and how many will be used by custom variables
    uint64_t reqCount = (pageSize - varAccX.getOffset()) / varAccX.getRSize();
    varAccX.setRCount(reqCount);
    varAccX.setPresent(reqCount);
    varAccX.setEndPos(variableCount);
    // Write the server config to shm
    uint64_t index = 0;
    uint64_t unx = Util::epoch();
    uint64_t bms = Util::bootMS();
    Util::RelAccXFieldData fName = varAccX.getFieldData("name");
    Util::RelAccXFieldData fTarget = varAccX.getFieldData("target");
    Util::RelAccXFieldData fInterval = varAccX.getFieldData("interval");
    Util::RelAccXFieldData fLastRun = varAccX.getFieldData("lastRun");
    Util::RelAccXFieldData fLastVal = varAccX.getFieldData("lastVal");
    Util::RelAccXFieldData fWaitTime = varAccX.getFieldData("waitTime");
    jsonForEachConst (Controller::Storage["variables"], it) {
      const JSON::Value & V = *it;
      varAccX.setString(fName, it.key(), index);
      if (V.isMember("target")) {
        std::string val = V["target"].asString();
        if (val.size() > 512) { WARN_MSG("Variable %s target truncated to 512 bytes!", it.key().c_str()); }
        varAccX.setString(fTarget, val, index);
      } else {
        varAccX.setString(fTarget, "", index);
      }
      if (V.isMember("interval")) {
        varAccX.setInt(fInterval, V["interval"].asDouble() * 1000, index);
      } else {
        varAccX.setInt(fInterval, 0, index);
      }
      if (V.isMember("lastunix")) {
        int64_t t = bms - (V["lastunix"].asInt() - unx) * 1000;
        if (t < 0) { t = 0; }
        varAccX.setInt(fLastRun, t, index);
      } else {
        varAccX.setInt(fLastRun, 0, index);
      }
      if (V.isMember("value")) {
        varAccX.setString(fLastVal, V["value"].asString(), index);
      } else {
        varAccX.setString(fLastVal, "", index);
      }
      if (V.isMember("waitTime")) {
        varAccX.setInt(fWaitTime, V["waitTime"].asDouble() * 1000, index);
      } else {
        varAccX.setInt(fWaitTime, 0, index);
      }
      index++;
    }
    varAccX.setReady();
    // Leave the page in memory after returning
    variablesPage.master = false;
    mutateShm = false;
  }

  /// \brief Queues a new custom variable to be added
  /// The request should contain a variable name, a target and an interval
  void addVariable(const JSON::Value &request, JSON::Value &output){
    if (request.isArray() && request.size() >= 2) {
      std::string name = request[0u].asStringRef();
      if (name.size() > 31) {
        name.erase(31);
        WARN_MSG("Truncating variable name to '%s'", name.c_str());
      }
      JSON::Value & V = Controller::Storage["variables"][name];
      // With length 2 is a hardcoded custom variable of [name, value]
      if (request.size() == 2){
        V["value"] = request[1u].asString();
      }else if(request.size() == 3){
        V["target"] = request[1u].asString();
        V["interval"] = request[2u].asDouble();
      }else if(request.size() == 4){
        V["target"] = request[1u].asString();
        V["interval"] = request[2u].asDouble();
        V["value"] = request[3u].asString();
      }else if(request.size() == 5){
        V["target"] = request[1u].asString();
        V["interval"] = request[2u].asDouble();
        V["value"] = request[3u].asString();
        V["waitTime"] = request[4u].asDouble();
      }else{
        ERROR_MSG("Cannot add custom variable, as the request array contained %u elements ( > 5)", request.size());
        Controller::Storage["variables"].removeMember(name);
        return;
      }
      INFO_MSG("Updating variable %s", name.c_str());
    } else if (request.isObject() && request.isMember("name")) {
      std::string name = request["name"].asStringRef();
      if (name.size() > 31) {
        name.erase(31);
        WARN_MSG("Truncating variable name to '%s'", name.c_str());
      }
      JSON::Value & V = Controller::Storage["variables"][name];
      if (request.isMember("target")) { V["target"] = request["target"].asString(); }
      if (request.isMember("interval")) { V["interval"] = request["interval"].asDouble(); }
      if (request.isMember("value")) { V["value"] = request["value"].asString(); }
      if (request.isMember("waitTime")) { V["waitTime"] = request["waitTime"].asInt(); }
      INFO_MSG("Updating variable %s", name.c_str());
    } else {
      ERROR_MSG("Cannot add custom variable: invalid API call format");
      return;
    }

    // Modify shm
    writeToShm();
    // Return variable list
    output = Controller::Storage["variables"];
  }

  /// \brief Removes the variable name contained in the request from shm and the sever config
  void removeVariable(const JSON::Value &request, JSON::Value &output){
    if (request.isString()){
      removeVariableByName(request.asStringRef());
      output = Controller::Storage["variables"];
      return;
    }
    if (request.isArray()){
      jsonForEachConst (request, it) { removeVariableByName(it->asStringRef()); }
      output = Controller::Storage["variables"];
      return;
    }
    WARN_MSG("Received a malformed request to remove custom variable(s)");
  }

  /// \brief Removes the variable with the given name from shm and the server config
  void removeVariableByName(const std::string & name) {
    // Modify config
    if (Controller::Storage["variables"].isMember(name)) {
      Controller::Storage["variables"].removeMember(name);
      INFO_MSG("Removing variable %s", name.c_str());
      writeToShm();
    }
  }

  /// \brief Runs the target of a specific variable and stores the result
  /// \param name name of the variable we are running
  /// \param target path or url to get results from
  bool runVariableTarget(const std::string & name, const std::string & target, const uint64_t & waitTime) {
    HIGH_MSG("Updating custom variable '%s': %s", name.c_str(), target.c_str());

    // Get URL for data
    if (target.substr(0, 7) == "http://" || target.substr(0, 8) == "https://") {
      HTTP::Downloader & DL = varDowners[name];
      if (DL.isEventLooping()) { return false; }
      DL.dataTimeout = waitTime;
      DL.setHeader("X-Custom-Variable", name);
      DL.setHeader("Content-Type", "text/plain");
      DL.getEventLooped(Controller::E, HTTP::URL(target), 6, [name, &DL]() {
        if (DL.isOk()) { mutateVariable(name, DL.data()); }
      }, [&DL]() { ERROR_MSG("Custom variable target failed to execute (%s)", DL.getStatusText().c_str()); });
      return true;
    }

    // Fork target executable and catch stdout
    if (varExecs.count(name)) { return false; }
    VarExec & ve = varExecs[name];

    // Rewrite target command
    std::deque<std::string> args;
    Util::shellSplit(target, args);
    // Run and get stdout
    int fdOut = -1;
    setenv("MIST_CUSTOM_VARIABLE", name.c_str(), 1);
    ve.pid = Util::Procs::StartPiped(args, 0, &fdOut, 0);
    unsetenv("MIST_CUSTOM_VARIABLE");

    if (!ve.pid) {
      varExecs.erase(name);
      return true;
    }
    Util::Procs::socketList.insert(fdOut);
    ve.C.open(-1, fdOut);
    ve.timerNo = Controller::E.addInterval([&ve]() {
      // Close the socket and remove the timer (return false).
      // Closing the socket will kill the process (if needed) and trigger clean-up
      ve.C.close();
      ve.timerNo = -1;
      return 0;
    }, waitTime);
    Controller::E.addSocket(fdOut, [&ve](void *) {
      // Receive data into buffer; if we have more than 127 bytes - close the socket (triggering clean-up)
      while (ve.C.spool()) {
        ve.retVal = ve.C.Received().copy(ve.C.Received().bytes(128));
        if (ve.C.Received().bytes(128) >= 128) { ve.C.close(); }
      }
    }, 0);
    ve.C.onClose([name](int s) {
      // Remove from socket list
      Util::Procs::socketList.erase(s);
      // Remove socket handler from event loop
      Controller::E.remove(s);
      // Set it to be collected
      varCollect.insert(name);
      // Reschedule the variable timer to go off as soon as possible
      Controller::E.rescheduleInterval(variableTimer, 0);
    });
    return true;
  }

  /// \brief Modifies the lastVal of the given custom variable in shm and the server config
  void mutateVariable(const std::string name, std::string & newVal) {
    Util::stringTrim(newVal);
    if (newVal.size() > 127) {
      WARN_MSG("Truncating response of custom variable %s to 127 bytes (received %zu bytes)", name.c_str(), newVal.size());
      newVal = newVal.substr(0, 127);
    }

    // Modify config
    JSON::Value & V = Controller::Storage["variables"][name];
    V["lastunix"] = Util::epoch();
    V["value"] = newVal;

    // Update shm, if it exists (otherwise trigger a shm rewrite)
    IPC::sharedPage variablePage(SHM_CUSTOM_VARIABLES, 0, false, false);
    if (!variablePage.mapped) { mutateShm = true; }
    Util::RelAccX varAccX(variablePage.mapped, false);
    if (varAccX.isReady()) {
      Util::RelAccXFieldData fName = varAccX.getFieldData("name");
      Util::RelAccXFieldData fLastRun = varAccX.getFieldData("lastRun");
      Util::RelAccXFieldData fLastVal = varAccX.getFieldData("lastVal");
      for (size_t i = 0; i < varAccX.getEndPos(); i++) {
        if (name == varAccX.getPointer(fName, i)) {
          varAccX.setInt(fLastRun, Util::bootMS(), i);
          varAccX.setString(fLastVal, newVal, i);
          return;
        }
      }
    }
  }
}// namespace Controller
