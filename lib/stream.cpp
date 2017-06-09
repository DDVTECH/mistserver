/// \file stream.cpp
/// Utilities for handling streams.

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <semaphore.h>
#include "json.h"
#include "stream.h"
#include "procs.h"
#include "config.h"
#include "socket.h"
#include "defines.h"
#include "shared_memory.h"
#include "dtsc.h"
#include "triggers.h"//LTS

/* roxlu-begin */
static std::string strftime_now(const std::string& format);
static void replace(std::string& str, const std::string& from, const std::string& to);
static void replace_variables(std::string& str);
/* roxlu-end */

std::string Util::getTmpFolder() {
  std::string dir;
  char * tmp_char = 0;
  if (!tmp_char) {
    tmp_char = getenv("TMP");
  }
  if (!tmp_char) {
    tmp_char = getenv("TEMP");
  }
  if (!tmp_char) {
    tmp_char = getenv("TMPDIR");
  }
  if (tmp_char) {
    dir = tmp_char;
    dir += "/mist";
  } else {
#if defined(_WIN32) || defined(_CYGWIN_)
    dir = "C:/tmp/mist";
#else
    dir = "/tmp/mist";
#endif
  }
  if (access(dir.c_str(), 0) != 0) {
    mkdir(dir.c_str(), S_IRWXU | S_IRWXG | S_IRWXO); //attempt to create mist folder - ignore failures
  }
  return dir + "/";
}

/// Filters the streamname, removing invalid characters and converting all
/// letters to lowercase. If a '?' character is found, everything following
/// that character is deleted. The original string is modified. If a '+' or space
/// exists, then only the part before that is sanitized.
void Util::sanitizeName(std::string & streamname) {
  //strip anything that isn't numbers, digits or underscores
  size_t index = streamname.find_first_of("+ ");
  if(index != std::string::npos){
    std::string preplus = streamname.substr(0,index);
    sanitizeName(preplus);
    std::string postplus = streamname.substr(index+1);
    if (postplus.find('?') != std::string::npos){
      postplus = postplus.substr(0, (postplus.find('?')));
    }
    streamname = preplus+"+"+postplus;
    return;
  }
  for (std::string::iterator i = streamname.end() - 1; i >= streamname.begin(); --i) {
    if (*i == '?') {
      streamname.erase(i, streamname.end());
      break;
    }
    if ( !isalpha( *i) && !isdigit( *i) && *i != '_' && *i != '.'){
      streamname.erase(i);
    } else {
      *i = tolower(*i);
    }
  }
}

JSON::Value Util::getStreamConfig(std::string streamname){
  JSON::Value result;
  if (streamname.size() > 100){
    FAIL_MSG("Stream opening denied: %s is longer than 100 characters (%lu).", streamname.c_str(), streamname.size());
    return result;
  }
  IPC::sharedPage mistConfOut(SHM_CONF, DEFAULT_CONF_PAGE_SIZE, false, false);
  IPC::semaphore configLock(SEM_CONF, O_CREAT | O_RDWR, ACCESSPERMS, 1);
  configLock.wait();
  DTSC::Scan config = DTSC::Scan(mistConfOut.mapped, mistConfOut.len);

  sanitizeName(streamname);
  std::string smp = streamname.substr(0, streamname.find_first_of("+ "));
  //check if smp (everything before + or space) exists
  DTSC::Scan stream_cfg = config.getMember("streams").getMember(smp);
  if (!stream_cfg){
    DEBUG_MSG(DLVL_MEDIUM, "Stream %s not configured", streamname.c_str());
  }else{
    result = stream_cfg.asJSON();
  }
  configLock.post();//unlock the config semaphore
  return result;
}

/// Checks if the given streamname has an active input serving it. Returns true if this is the case.
/// Assumes the streamname has already been through sanitizeName()!
bool Util::streamAlive(std::string & streamname){
  char semName[NAME_BUFFER_SIZE];
  snprintf(semName, NAME_BUFFER_SIZE, SEM_INPUT, streamname.c_str());
  IPC::semaphore playerLock(semName, O_RDWR, ACCESSPERMS, 1, true);
  if (!playerLock){return false;}
  if (!playerLock.tryWait()) {
    playerLock.close();
    return true;
  }else{
    playerLock.post();
    playerLock.close();
    return false;
  }
}

/// Assures the input for the given stream name is active.
/// Does stream name sanitation first, followed by a stream name length check (<= 100 chars).
/// Then, checks if an input is already active by running streamAlive(). If yes, return true.
/// If no, loads up the server configuration and attempts to start the given stream according to current configuration.
/// At this point, fails and aborts if MistController isn't running.
bool Util::startInput(std::string streamname, std::string filename, bool forkFirst, bool isProvider) {
  sanitizeName(streamname);
  if (streamname.size() > 100){
    FAIL_MSG("Stream opening denied: %s is longer than 100 characters (%lu).", streamname.c_str(), streamname.size());
    return false;
  }
  //Check if the stream is already active.
  //If yes, don't activate again to prevent duplicate inputs.
  //It's still possible a duplicate starts anyway, this is caught in the inputs initializer.
  //Note: this uses the _whole_ stream name, including + (if any).
  //This means "test+a" and "test+b" have separate locks and do not interact with each other.
  if (streamAlive(streamname)){
    DEBUG_MSG(DLVL_MEDIUM, "Stream %s already active; continuing", streamname.c_str());
    return true;
  }

  //Attempt to load up configuration and find this stream
  IPC::sharedPage mistConfOut(SHM_CONF, DEFAULT_CONF_PAGE_SIZE);
  IPC::semaphore configLock(SEM_CONF, O_CREAT | O_RDWR, ACCESSPERMS, 1);
  //Lock the config to prevent race conditions and corruption issues while reading
  configLock.wait();
  DTSC::Scan config = DTSC::Scan(mistConfOut.mapped, mistConfOut.len);
  //Abort if no config available
  if (!config){
    FAIL_MSG("Configuration not available, aborting! Is MistController running?");
    configLock.post();//unlock the config semaphore
    return false;
  }
  /*LTS-START*/
  if (config.getMember("hardlimit_active")) {
    configLock.post();//unlock the config semaphore
    return false;
  }
  /*LTS-END*/
  //Find stream base name
  std::string smp = streamname.substr(0, streamname.find_first_of("+ "));
  //check if base name (everything before + or space) exists
  DTSC::Scan stream_cfg = config.getMember("streams").getMember(smp);
  if (!stream_cfg){
    DEBUG_MSG(DLVL_HIGH, "Stream %s not configured - attempting to ignore", streamname.c_str());
  }
  /*LTS-START*/
  if (!filename.size()){
    if (stream_cfg && stream_cfg.getMember("hardlimit_active")) {
      configLock.post();//unlock the config semaphore
      return false;
    }
    if(Triggers::shouldTrigger("STREAM_LOAD", smp)){
      if (!Triggers::doTrigger("STREAM_LOAD", streamname, smp)){
        configLock.post();//unlock the config semaphore
        return false;
      }
    }
    if(Triggers::shouldTrigger("STREAM_SOURCE", smp)){
      Triggers::doTrigger("STREAM_SOURCE", streamname, smp, false, filename);
    }
  }
  /*LTS-END*/

  
  //Only use configured source if not manually overridden. Abort if no config is available.
  if (!filename.size()){
    if (!stream_cfg){
      DEBUG_MSG(DLVL_MEDIUM, "Stream %s not configured, no source manually given, cannot start", streamname.c_str());
      configLock.post();//unlock the config semaphore
      return false;
    }
    filename = stream_cfg.getMember("source").asString();
  }
  
  //check in curConf for capabilities-inputs-<naam>-priority/source_match
  std::string player_bin;
  bool selected = false;
  long long int curPrio = -1;
  DTSC::Scan inputs = config.getMember("capabilities").getMember("inputs");
  DTSC::Scan input;
  unsigned int input_size = inputs.getSize();
  bool noProviderNoPick = false;
  for (unsigned int i = 0; i < input_size; ++i){
    DTSC::Scan tmp_input = inputs.getIndice(i);
    
    //if match voor current stream && priority is hoger dan wat we al hebben
    if (tmp_input.getMember("source_match") && curPrio < tmp_input.getMember("priority").asInt()){
      if (tmp_input.getMember("source_match").getSize()){
        for(unsigned int j = 0; j < tmp_input.getMember("source_match").getSize(); ++j){
          std::string source = tmp_input.getMember("source_match").getIndice(j).asString();
          std::string front = source.substr(0,source.find('*'));
          std::string back = source.substr(source.find('*')+1);
          MEDIUM_MSG("Checking input %s: %s (%s)", inputs.getIndiceName(i).c_str(), tmp_input.getMember("name").asString().c_str(), source.c_str());
          
          if (filename.substr(0,front.size()) == front && filename.substr(filename.size()-back.size()) == back){
            if (tmp_input.getMember("non-provider") && !isProvider){
              noProviderNoPick = true;
              continue;
            }
            player_bin = Util::getMyPath() + "MistIn" + tmp_input.getMember("name").asString();
            curPrio = tmp_input.getMember("priority").asInt();
            selected = true;
            input = tmp_input;
          }
        }
      }else{
        std::string source = tmp_input.getMember("source_match").asString();
        std::string front = source.substr(0,source.find('*'));
        std::string back = source.substr(source.find('*')+1);
        MEDIUM_MSG("Checking input %s: %s (%s)", inputs.getIndiceName(i).c_str(), tmp_input.getMember("name").asString().c_str(), source.c_str());
        
        if (filename.substr(0,front.size()) == front && filename.substr(filename.size()-back.size()) == back){
          if (tmp_input.getMember("non-provider") && !isProvider){
            noProviderNoPick = true;
            continue;
          }
          player_bin = Util::getMyPath() + "MistIn" + tmp_input.getMember("name").asString();
          curPrio = tmp_input.getMember("priority").asInt();
          selected = true;
          input = tmp_input;
        }
      }

    }
  }
  
  if (!selected){
    configLock.post();//unlock the config semaphore
    if (noProviderNoPick){
      INFO_MSG("Not a media provider for stream %s: %s", streamname.c_str(), filename.c_str());
    }else{
      FAIL_MSG("No compatible input found for stream %s: %s", streamname.c_str(), filename.c_str());
    }
    return false;
  }

  //copy the necessary arguments to separate storage so we can unlock the config semaphore safely
  std::map<std::string, std::string> str_args;
  //check required parameters
  DTSC::Scan required = input.getMember("required");
  unsigned int req_size = required.getSize();
  for (unsigned int i = 0; i < req_size; ++i){
    std::string opt = required.getIndiceName(i);
    if (!stream_cfg.getMember(opt)){
      configLock.post();//unlock the config semaphore
      FAIL_MSG("Required parameter %s for stream %s missing", opt.c_str(), streamname.c_str());
      return false;
    }
    str_args[required.getIndice(i).getMember("option").asString()] = stream_cfg.getMember(opt).asString();
  }
  //check optional parameters
  DTSC::Scan optional = input.getMember("optional");
  unsigned int opt_size = optional.getSize();
  for (unsigned int i = 0; i < opt_size; ++i){
    std::string opt = optional.getIndiceName(i);
    VERYHIGH_MSG("Checking optional %u: %s", i, opt.c_str());
    if (stream_cfg.getMember(opt)){
      str_args[optional.getIndice(i).getMember("option").asString()] = stream_cfg.getMember(opt).asString();
    }
  }
  
  //finally, unlock the config semaphore
  configLock.post();

  INFO_MSG("Starting %s -s %s %s", player_bin.c_str(), streamname.c_str(), filename.c_str());
  char * argv[30] = {(char *)player_bin.c_str(), (char *)"-s", (char *)streamname.c_str(), (char *)filename.c_str()};
  int argNum = 3;
  std::string debugLvl;
  if (Util::Config::printDebugLevel != DEBUG && !str_args.count("--debug")){
    debugLvl = JSON::Value((long long)Util::Config::printDebugLevel).asString();
    argv[++argNum] = (char *)"--debug";
    argv[++argNum] = (char *)debugLvl.c_str();
  }
  for (std::map<std::string, std::string>::iterator it = str_args.begin(); it != str_args.end(); ++it){
    argv[++argNum] = (char *)it->first.c_str();
    argv[++argNum] = (char *)it->second.c_str();
    INFO_MSG("  Option %s = %s", it->first.c_str(), it->second.c_str());
  }
  argv[++argNum] = (char *)0;
  
  int pid = 0;
  if (forkFirst){
    DEBUG_MSG(DLVL_DONTEVEN, "Forking");
    pid = fork();
    if (pid == -1) {
      FAIL_MSG("Forking process for stream %s failed: %s", streamname.c_str(), strerror(errno));
      return false;
    }
  }else{
    DEBUG_MSG(DLVL_DONTEVEN, "Not forking");
  }
  
  if (pid == 0){
    Socket::Connection io(0, 1);
    io.close();
    DEBUG_MSG(DLVL_DONTEVEN, "execvp");
    execvp(argv[0], argv);
    FAIL_MSG("Starting process %s for stream %s failed: %s", argv[0], streamname.c_str(), strerror(errno));
    _exit(42);
  }

  unsigned int waiting = 0;
  while (!streamAlive(streamname) && ++waiting < 40){
    Util::wait(250);
  }

  return streamAlive(streamname);
}

/// Attempt to start a push for streamname to target.
/// streamname MUST be pre-sanitized
/// target gets variables replaced and may be altered by the PUSH_OUT_START trigger response.
/// Attempts to match the altered target to an output that can push to it.
pid_t Util::startPush(const std::string & streamname, std::string & target) {
  if (Triggers::shouldTrigger("PUSH_OUT_START", streamname)) {
    std::string payload = streamname+"\n"+target;
    std::string filepath_response;
    Triggers::doTrigger("PUSH_OUT_START", payload, streamname.c_str(), false,  filepath_response);
    target = filepath_response;
  }
  if (!target.size()){
    INFO_MSG("Aborting push of stream %s - target is empty", streamname.c_str());
    return 0;
  }

  // The target can hold variables like current time etc
  replace_variables(target);
  replace(target, "$stream", streamname);

  //Attempt to load up configuration and find this stream
  IPC::sharedPage mistConfOut(SHM_CONF, DEFAULT_CONF_PAGE_SIZE);
  IPC::semaphore configLock(SEM_CONF, O_CREAT | O_RDWR, ACCESSPERMS, 1);
  //Lock the config to prevent race conditions and corruption issues while reading
  configLock.wait();

  DTSC::Scan config = DTSC::Scan(mistConfOut.mapped, mistConfOut.len);
  DTSC::Scan outputs = config.getMember("capabilities").getMember("connectors");
  std::string output_bin = "";
  unsigned int outputs_size = outputs.getSize();
  for (unsigned int i = 0; i<outputs_size && !output_bin.size(); ++i){
    DTSC::Scan output = outputs.getIndice(i);
    if (output.getMember("push_urls")){
      unsigned int push_count = output.getMember("push_urls").getSize();
      for (unsigned int j = 0; j < push_count; ++j){
        std::string tar_match = output.getMember("push_urls").getIndice(j).asString();
        std::string front = tar_match.substr(0,tar_match.find('*'));
        std::string back = tar_match.substr(tar_match.find('*')+1);
        MEDIUM_MSG("Checking output %s: %s (%s)", outputs.getIndiceName(i).c_str(), output.getMember("name").asString().c_str(), target.c_str());
        
        if (target.substr(0,front.size()) == front && target.substr(target.size()-back.size()) == back){
          output_bin = Util::getMyPath() + "MistOut" + output.getMember("name").asString();
          break;
        }
      }
    }
  }
  configLock.post();
  
  if (output_bin == ""){
    FAIL_MSG("No output found for target %s, aborting push.", target.c_str());
    return 0;
  }
  INFO_MSG("Pushing %s to %s through %s", streamname.c_str(), target.c_str(), output_bin.c_str());
  // Start  output.
  char* argv[] = {
    (char*)output_bin.c_str(),
    (char*)"--stream", (char*)streamname.c_str(),
    (char*)target.c_str(),
    (char*)NULL
  };

  int stdErr = 2;
  return Util::Procs::StartPiped(argv, 0, 0, &stdErr);

}

static void replace(std::string& str, const std::string& from, const std::string& to) {
  if(from.empty()) {
    return;
  }
  size_t start_pos = 0;
  while((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
}

static void replace_variables(std::string& str) {
  
  char buffer[80] = { 0 };
  std::map<std::string, std::string> vars;
  std::string day = strftime_now("%d");
  std::string month = strftime_now("%m");
  std::string year = strftime_now("%Y");
  std::string hour = strftime_now("%H");
  std::string minute = strftime_now("%M");
  std::string seconds = strftime_now("%S");
  std::string datetime = year +"." +month +"." +day +"." +hour +"." +minute +"." +seconds;

  if (0 == day.size()) {
    WARN_MSG("Failed to retrieve the current day with strftime_now().");
  }
  if (0 == month.size()) {
    WARN_MSG("Failed to retrieve the current month with strftime_now().");
  }
  if (0 == year.size()) {
    WARN_MSG("Failed to retrieve the current year with strftime_now().");
  }
  if (0 == hour.size()) {
    WARN_MSG("Failed to retrieve the current hour with strftime_now().");
  }
  if (0 == minute.size()) {
    WARN_MSG("Failed to retrieve the current minute with strftime_now().");
  }
  if (0 == seconds.size()) {
    WARN_MSG("Failed to retrieve the current seconds with strftime_now().");
  }
  
  vars.insert(std::pair<std::string, std::string>("$day", day));
  vars.insert(std::pair<std::string, std::string>("$month", month));
  vars.insert(std::pair<std::string, std::string>("$year", year));
  vars.insert(std::pair<std::string, std::string>("$hour", hour));
  vars.insert(std::pair<std::string, std::string>("$minute", minute));
  vars.insert(std::pair<std::string, std::string>("$second", seconds));
  vars.insert(std::pair<std::string, std::string>("$datetime", datetime));

  std::map<std::string, std::string>::iterator it = vars.begin();
  while (it != vars.end()) {
    replace(str, it->first, it->second);
    ++it;
  }
}

static std::string strftime_now(const std::string& format) {
  
  time_t rawtime;
  struct tm* timeinfo = NULL;
  char buffer [80] = { 0 };

  time(&rawtime);
  timeinfo = localtime (&rawtime);

  if (0 == strftime(buffer, 80, format.c_str(), timeinfo)) {
    FAIL_MSG("Call to stftime() failed with format: %s, maybe our buffer is not big enough (80 bytes).", format.c_str());
    return "";
  }

  return buffer;
}

