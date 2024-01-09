#include "controller_variables.h"
#include "controller_statistics.h"
#include "controller_storage.h"
#include <mist/downloader.h>
#include <mist/bitfields.h>
#include <mist/config.h>
#include <mist/json.h>
#include <mist/stream.h>
#include <mist/triggers.h>
#include <string>

namespace Controller{
  // Indicates whether the shared memory page is stale compared to the server config
  static bool mutateShm = true;
  // Size of the shared memory page
  static uint64_t pageSize = CUSTOM_VARIABLES_INITSIZE;
  tthread::mutex variableMutex;

  /// \brief Loops, checking every second if any variables require updating
  void variableCheckLoop(void *np){
    while (Controller::conf.is_active){
      {
        tthread::lock_guard<tthread::mutex> guard(variableMutex);
        uint64_t now = Util::epoch();
        // Check if any custom variable target needs to be run
        IPC::sharedPage variablePage(SHM_CUSTOM_VARIABLES, 0, false, false);
        if (variablePage.mapped){
          Util::RelAccX varAccX(variablePage.mapped, false);
          if (varAccX.isReady()){
            for (size_t i = 0; i < varAccX.getEndPos(); i++){
              std::string name = varAccX.getPointer("name", i);
              std::string target = varAccX.getPointer("target", i);
              uint32_t interval = varAccX.getInt("interval", i);
              uint64_t lastRun = varAccX.getInt("lastRun", i);
              uint32_t waitTime = varAccX.getInt("waitTime", i);
              if (target.size() && (!lastRun || (interval && (lastRun + interval < now)))){
                // Set the wait time to the interval, or 1 second if it is less than 1 second
                if (!waitTime){
                  waitTime = interval;
                }
                if (waitTime < 1){
                  waitTime = 1;
                }
                runVariableTarget(name, target, waitTime);
              }
            }
          }
        }
        // Write variables from the server config to shm if any data has changed
        if (mutateShm){
          writeToShm();
          mutateShm = false;
        }
      }
      Util::sleep(1000);
    }
    // Cleanup shared memory page
    IPC::sharedPage variablesPage(SHM_CUSTOM_VARIABLES, pageSize, false, false);
    if (variablesPage.mapped){
      variablesPage.master = true;
    }
  }

  /// \brief Writes custom variable from the server config to shared memory
  void writeToShm(){
    uint64_t variableCount = Controller::Storage["variables"].size();
    IPC::sharedPage variablesPage(SHM_CUSTOM_VARIABLES, pageSize, false, false);
    // If we have an existing page, set the reload flag
    if (variablesPage.mapped){
      variablesPage.master = true;
      Util::RelAccX varAccX = Util::RelAccX(variablesPage.mapped, false);
      // Check if we need a bigger page
      uint64_t sizeRequired = varAccX.getOffset() + varAccX.getRSize() * variableCount;
      if (pageSize < sizeRequired){pageSize = sizeRequired;}
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
    jsonForEach(Controller::Storage["variables"], it){
      std::string name = (*it)[0u].asString();
      std::string target = (*it)[1u].asString();
      uint32_t interval =  (*it)[2u].asInt();
      uint64_t lastRun = (*it)[3u].asInt();
      std::string lastVal = (*it)[4u].asString();
      uint32_t waitTime = (*it)[5u].asInt();
      varAccX.setString(varAccX.getFieldData("name"), name, index);
      varAccX.setString(varAccX.getFieldData("target"), target, index);
      varAccX.setInt(varAccX.getFieldData("interval"), interval, index);
      varAccX.setInt(varAccX.getFieldData("lastRun"), lastRun, index);
      varAccX.setString(varAccX.getFieldData("lastVal"), lastVal, index);
      varAccX.setInt(varAccX.getFieldData("waitTime"), waitTime, index);
      index++;
    }
    varAccX.setReady();
    // Leave the page in memory after returning
    variablesPage.master = false;
  }

  /// \brief Queues a new custom variable to be added
  /// The request should contain a variable name, a target and an interval
  void addVariable(const JSON::Value &request, JSON::Value &output){
    std::string name;
    std::string target;
    std::string value;
    uint32_t interval;
    uint32_t waitTime;
    bool isNew = true;
    if (request.isArray()){
      // With length 2 is a hardcoded custom variable of [name, value]
      if (request.size() == 2){
        name = request[0u].asString();
        value  = request[1u].asString();
        target = "";
        interval = 0;
        waitTime = interval;
      }else if(request.size() == 3){
        name = request[0u].asString();
        target = request[1u].asString();
        interval = request[2u].asInt();
        value = "";
        waitTime = interval;
      }else if(request.size() == 4){
        name = request[0u].asString();
        target = request[1u].asString();
        interval = request[2u].asInt();
        value = request[3u].asString();
        waitTime = interval;
      }else if(request.size() == 5){
        name = request[0u].asString();
        target = request[1u].asString();
        interval = request[2u].asInt();
        value = request[3u].asString();
        waitTime = request[4u].asInt();
      }else{
        ERROR_MSG("Cannot add custom variable, as the request contained %u variables", request.size());
        return;
      }
    }else{
      name = request["name"].asString();
      if (request.isMember("target")){
        target = request["target"].asString();
      }else{
        target = "";
      }
      if (request.isMember("interval")){
        interval = request["interval"].asInt();
      }else{
        interval = 0;
      }
      if (request.isMember("value")){
        value = request["value"].asString();
      }else{
        value = "";
      }
      if (request.isMember("waitTime")){
        waitTime = request["waitTime"].asInt();
      }else{
        waitTime = interval;
      }
    }
    if (!name.size()){
      WARN_MSG("Unable to retrieve variable name from request");
      return;
    }
    if ((target.find("'") != std::string::npos) || (target.find('"') != std::string::npos)){
      ERROR_MSG("Cannot add custom variable, as the request contained a ' or \" character (got '%s')", target.c_str());
      return;
    }
    if (name.size() > 31){
      name = name.substr(0, 31);
      WARN_MSG("Maximum name size is 31 characters, truncating name to '%s'", name.c_str());
    }
    if (target.size() > 511){
      target = target.substr(0, 511);
      WARN_MSG("Maximum target size is 511 characters, truncating target to '%s'", target.c_str());
    }
    if (value.size() > 64){
      value = value.substr(0, 63);
      WARN_MSG("Maximum value size is 63 characters, truncating value to '%s'", value.c_str());
    }
    tthread::lock_guard<tthread::mutex> guard(variableMutex);
    // Check if we have an existing variable with the same name to modify
    jsonForEach(Controller::Storage["variables"], it){
      if ((*it)[0u].asString() == name){
        INFO_MSG("Modifying existing custom variable '%s'", name.c_str());
        (*it)[1u] = target;
        (*it)[2u] = interval;
        // Reset lastRun so that the lastValue gets updated during the next iteration
        (*it)[3u] = 0;
        // If we received a value, overwrite it
        if (value.size()){(*it)[4u] = value;}
        (*it)[5u] = waitTime;
        isNew = false;
      }
    }
    // Else push a new custom variable to the list
    if (isNew){
      INFO_MSG("Adding new custom variable '%s'", name.c_str());
      JSON::Value thisVar;
      thisVar.append(name);
      thisVar.append(target);
      thisVar.append(interval);
      thisVar.append(0);
      thisVar.append(value);
      thisVar.append(waitTime);
      Controller::Storage["variables"].append(thisVar);
    }
    // Modify shm
    writeToShm();
    // Return variable list
    listCustomVariables(output);
  }

  /// \brief Fills output with all defined custom variables
  void listCustomVariables(JSON::Value &output){
    output.null();
    // First check shm for custom variables
    IPC::sharedPage variablePage(SHM_CUSTOM_VARIABLES, 0, false, false);
    if (variablePage.mapped){
      Util::RelAccX varAccX(variablePage.mapped, false);
      if (varAccX.isReady()){
        for (size_t i = 0; i < varAccX.getEndPos(); i++){
          std::string name = varAccX.getPointer("name", i);
          std::string target = varAccX.getPointer("target", i);
          uint32_t interval = varAccX.getInt("interval", i);
          uint64_t lastRun = varAccX.getInt("lastRun", i);
          std::string lastVal = varAccX.getPointer("lastVal", i);
          uint32_t waitTime = varAccX.getInt("waitTime", i);
          // If there is no target, assume this is a static custom variable
          if (target.size()){
            output[name].append(target);
            output[name].append(interval);
            output[name].append(lastRun);
            output[name].append(lastVal);
            output[name].append(waitTime);
          }else{
            output[name] = lastVal;
          }
        }
        return;
      }
    }
    ERROR_MSG("Unable to list custom variables from shm. Retrying from server config");
    jsonForEach(Controller::Storage["variables"], it){
      std::string name = (*it)[0u].asString();
      std::string target = (*it)[1u].asString();
      uint32_t interval =  (*it)[2u].asInt();
      uint64_t lastRun = (*it)[3u].asInt();
      std::string lastVal = (*it)[4u].asString();
      uint32_t waitTime =  (*it)[5u].asInt();
      // If there is no target, assume this is a static custom variable
      if (target.size()){
        output[name].append(target);
        output[name].append(interval);
        output[name].append(lastRun);
        output[name].append(lastVal);
        output[name].append(waitTime);
      }else{
        output[name] = lastVal;
      }
    }
  }

  /// \brief Removes the variable name contained in the request from shm and the sever config
  void removeVariable(const JSON::Value &request, JSON::Value &output){
    if (request.isString()){
      removeVariableByName(request.asStringRef());
      listCustomVariables(output);
      return;
    }
    if (request.isArray()){
      if (request[0u].size()){
        removeVariableByName(request[0u].asStringRef());
        listCustomVariables(output);
        return;
      }
    }else{
      if (request.isMember("name")){
        removeVariableByName(request["name"].asStringRef());
        listCustomVariables(output);
        return;
      }
    }
    WARN_MSG("Received a request to remove a custom variable, but no name was given");
  }

  /// \brief Removes the variable with the given name from shm and the server config
  void removeVariableByName(const std::string &name){
    tthread::lock_guard<tthread::mutex> guard(variableMutex);
    // Modify config
    jsonForEach(Controller::Storage["variables"], it){
      if ((*it)[0u].asString() == name){
        INFO_MSG("Removing variable named `%s`", name.c_str());
        it.remove();
      }
    }
    // Modify shm
    writeToShm();
  }

  /// \brief Runs the target of a specific variable and stores the result
  /// \param name name of the variable we are running
  /// \param target path or url to get results from
  void runVariableTarget(const std::string &name, const std::string &target, const uint64_t &waitTime){
    HIGH_MSG("Updating custom variable '%s' <- '%s'", name.c_str(), target.c_str());
    // Post URL for data
    if (target.substr(0, 7) == "http://" || target.substr(0, 8) == "https://"){
      HTTP::Downloader DL;
      DL.setHeader("X-Custom-Variable", name);
      DL.setHeader("Content-Type", "text/plain");
      HTTP::URL url(target);
      if (DL.post(url, NULL, 0, true) && DL.isOk()){
        mutateVariable(name, DL.data());
        return;
      }
      ERROR_MSG("Custom variable target failed to execute (%s)", DL.getStatusText().c_str());
      return;
    // Fork target executable and catch stdout
    }else{
      // Rewrite target command
      std::string tmpCmd = target;
      char exec_cmd[10240];
      strncpy(exec_cmd, tmpCmd.c_str(), 10240);
      HIGH_MSG("Executing command: %s", exec_cmd);
      uint8_t argCnt = 0;
      char *startCh = 0;
      char *args[1280];
      for (char *i = exec_cmd; i - exec_cmd < 10240; ++i){
        if (!*i){
          if (startCh){args[argCnt++] = startCh;}
          break;
        }
        if (*i == ' '){
          if (startCh){
            args[argCnt++] = startCh;
            startCh = 0;
            *i = 0;
          }
        }else{
          if (!startCh){startCh = i;}
        }
      }
      args[argCnt] = 0;
      // Run and get stdout
      setenv("MIST_CUSTOM_VARIABLE", name.c_str(), 1);
      std::string ret = Util::Procs::getLimitedOutputOf(args, waitTime * 1000, 128);
      unsetenv("MIST_CUSTOM_VARIABLE");
      // Save output to custom variable
      mutateVariable(name, ret);
      return;
    }
  }

  /// \brief Modifies the lastVal of the given custom variable in shm and the server config
  void mutateVariable(const std::string name, std::string &newVal){
    uint64_t lastRun = Util::epoch();
    Util::stringTrim(newVal);
    if (newVal.size() > 127){
      WARN_MSG("Truncating response of custom variable %s to 127 bytes (received %zu bytes)", name.c_str(), newVal.size());
      newVal = newVal.substr(0, 127);
    }
    // Modify config
    jsonForEach(Controller::Storage["variables"], it){
      if ((*it)[0u].asString() == name){
        (*it)[3u] = lastRun;
        (*it)[4u] = newVal;
        mutateShm = true;
      }
    }
  }
}// namespace Controller
