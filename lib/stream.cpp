/// \file stream.cpp
/// Utilities for handling streams.

#include "stream.h"
#include "config.h"
#include "defines.h"
#include "dtsc.h"
#include "json.h"
#include "procs.h"
#include "shared_memory.h"
#include "socket.h"
#include "triggers.h" //LTS
#include "h265.h"
#include "mp4_generic.h"
#include <semaphore.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/// Calls strftime using the current local time, returning empty string on any error.
static std::string strftime_now(const std::string &format){
  time_t rawtime;
  char buffer[80];
  time(&rawtime);
  struct tm timebuf;
  struct tm *timeinfo = localtime_r(&rawtime, &timebuf);
  if (!timeinfo || !strftime(buffer, 80, format.c_str(), timeinfo)){return "";}
  return buffer;
}

/// Replaces any occurrences of 'from' with 'to' in 'str'.
static void replace(std::string &str, const std::string &from, const std::string &to){
  if (from.empty()){return;}
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos){
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
}

std::string Util::codecString(const std::string & codec, const std::string & initData){
  if (codec == "H264"){ 
    std::stringstream r;
    MP4::AVCC avccBox;
    avccBox.setPayload(initData);
    r << "avc1.";
    r << std::hex << std::setw(2) << std::setfill('0') << (int)initData[1] << std::dec;
    r << std::hex << std::setw(2) << std::setfill('0') << (int)initData[2] << std::dec;
    r << std::hex << std::setw(2) << std::setfill('0') << (int)initData[3] << std::dec;
    return r.str();
  }
  if (codec == "HEVC"){
    h265::initData init(initData);
    h265::metaInfo mInfo = init.getMeta();
    std::stringstream r;
    r << "hev1.";
    switch (mInfo.general_profile_space){
      case 0: break;
      case 1: r << 'A'; break;
      case 2: r << 'B'; break;
      case 3: r << 'C'; break;
    }
    r << (unsigned long)mInfo.general_profile_idc << '.';
    uint32_t mappedFlags = 0;
    if (mInfo.general_profile_compatflags & 0x00000001ul){mappedFlags += 0x80000000ul;}
    if (mInfo.general_profile_compatflags & 0x00000002ul){mappedFlags += 0x40000000ul;}
    if (mInfo.general_profile_compatflags & 0x00000004ul){mappedFlags += 0x20000000ul;}
    if (mInfo.general_profile_compatflags & 0x00000008ul){mappedFlags += 0x10000000ul;}
    if (mInfo.general_profile_compatflags & 0x00000010ul){mappedFlags += 0x08000000ul;}
    if (mInfo.general_profile_compatflags & 0x00000020ul){mappedFlags += 0x04000000ul;}
    if (mInfo.general_profile_compatflags & 0x00000040ul){mappedFlags += 0x02000000ul;}
    if (mInfo.general_profile_compatflags & 0x00000080ul){mappedFlags += 0x01000000ul;}
    if (mInfo.general_profile_compatflags & 0x00000100ul){mappedFlags += 0x00800000ul;}
    if (mInfo.general_profile_compatflags & 0x00000200ul){mappedFlags += 0x00400000ul;}
    if (mInfo.general_profile_compatflags & 0x00000400ul){mappedFlags += 0x00200000ul;}
    if (mInfo.general_profile_compatflags & 0x00000800ul){mappedFlags += 0x00100000ul;}
    if (mInfo.general_profile_compatflags & 0x00001000ul){mappedFlags += 0x00080000ul;}
    if (mInfo.general_profile_compatflags & 0x00002000ul){mappedFlags += 0x00040000ul;}
    if (mInfo.general_profile_compatflags & 0x00004000ul){mappedFlags += 0x00020000ul;}
    if (mInfo.general_profile_compatflags & 0x00008000ul){mappedFlags += 0x00010000ul;}
    if (mInfo.general_profile_compatflags & 0x00010000ul){mappedFlags += 0x00008000ul;}
    if (mInfo.general_profile_compatflags & 0x00020000ul){mappedFlags += 0x00004000ul;}
    if (mInfo.general_profile_compatflags & 0x00040000ul){mappedFlags += 0x00002000ul;}
    if (mInfo.general_profile_compatflags & 0x00080000ul){mappedFlags += 0x00001000ul;}
    if (mInfo.general_profile_compatflags & 0x00100000ul){mappedFlags += 0x00000800ul;}
    if (mInfo.general_profile_compatflags & 0x00200000ul){mappedFlags += 0x00000400ul;}
    if (mInfo.general_profile_compatflags & 0x00400000ul){mappedFlags += 0x00000200ul;}
    if (mInfo.general_profile_compatflags & 0x00800000ul){mappedFlags += 0x00000100ul;}
    if (mInfo.general_profile_compatflags & 0x01000000ul){mappedFlags += 0x00000080ul;}
    if (mInfo.general_profile_compatflags & 0x02000000ul){mappedFlags += 0x00000040ul;}
    if (mInfo.general_profile_compatflags & 0x04000000ul){mappedFlags += 0x00000020ul;}
    if (mInfo.general_profile_compatflags & 0x08000000ul){mappedFlags += 0x00000010ul;}
    if (mInfo.general_profile_compatflags & 0x10000000ul){mappedFlags += 0x00000008ul;}
    if (mInfo.general_profile_compatflags & 0x20000000ul){mappedFlags += 0x00000004ul;}
    if (mInfo.general_profile_compatflags & 0x40000000ul){mappedFlags += 0x00000002ul;}
    if (mInfo.general_profile_compatflags & 0x80000000ul){mappedFlags += 0x00000001ul;}
    r << std::hex << (unsigned long)mappedFlags << std::dec << '.';
    if (mInfo.general_tier_flag){r << 'H';}else{r << 'L';}
    r << (unsigned long)mInfo.general_level_idc;
    if (mInfo.constraint_flags[0]){
      r << '.' << std::hex << (unsigned long)mInfo.constraint_flags[0] << std::dec;
    }
    return r.str();
  }
  if (codec == "AAC"){return "mp4a.40.2";}
  if (codec == "MP3"){return "mp4a.40.34";}
  if (codec == "AC3"){return "ec-3";}
  return "";
}

/// Replaces all stream-related variables in the given 'str' with their values.
void Util::streamVariables(std::string &str, const std::string &streamname,
                           const std::string &source){
  replace(str, "$source", source);
  replace(str, "$datetime", "$year.$month.$day.$hour.$minute.$second");
  replace(str, "$day", strftime_now("%d"));
  replace(str, "$month", strftime_now("%m"));
  replace(str, "$year", strftime_now("%Y"));
  replace(str, "$hour", strftime_now("%H"));
  replace(str, "$minute", strftime_now("%M"));
  replace(str, "$second", strftime_now("%S"));
  replace(str, "$stream", streamname);
  if (streamname.find('+') != std::string::npos){
    std::string strbase = streamname.substr(0, streamname.find('+'));
    std::string strext = streamname.substr(streamname.find('+') + 1);
    replace(str, "$basename", strbase);
    replace(str, "$wildcard", strext);
    if (strext.size()){
      replace(str, "$pluswildcard", "+" + strext);
    }else{
      replace(str, "$pluswildcard", "");
    }
  }else{
    replace(str, "$basename", streamname);
    replace(str, "$wildcard", "");
    replace(str, "$pluswildcard", "");
  }
}

std::string Util::getTmpFolder(){
  std::string dir;
  char *tmp_char = 0;
  if (!tmp_char){tmp_char = getenv("TMP");}
  if (!tmp_char){tmp_char = getenv("TEMP");}
  if (!tmp_char){tmp_char = getenv("TMPDIR");}
  if (tmp_char){
    dir = tmp_char;
    dir += "/mist";
  }else{
#if defined(_WIN32) || defined(_CYGWIN_)
    dir = "C:/tmp/mist";
#else
    dir = "/tmp/mist";
#endif
  }
  if (access(dir.c_str(), 0) != 0){
    mkdir(dir.c_str(),
          S_IRWXU | S_IRWXG | S_IRWXO); // attempt to create mist folder - ignore failures
  }
  return dir + "/";
}

/// Filters the streamname, removing invalid characters and converting all
/// letters to lowercase. If a '?' character is found, everything following
/// that character is deleted. The original string is modified. If a '+' or space
/// exists, then only the part before that is sanitized.
void Util::sanitizeName(std::string &streamname){
  // strip anything that isn't numbers, digits or underscores
  size_t index = streamname.find_first_of("+ ");
  if (index != std::string::npos){
    std::string preplus = streamname.substr(0, index);
    sanitizeName(preplus);
    std::string postplus = streamname.substr(index + 1);
    if (postplus.find('?') != std::string::npos){
      postplus = postplus.substr(0, (postplus.find('?')));
    }
    streamname = preplus + "+" + postplus;
    return;
  }
  for (std::string::iterator i = streamname.end() - 1; i >= streamname.begin(); --i){
    if (*i == '?'){
      streamname.erase(i, streamname.end());
      break;
    }
    if (!isalpha(*i) && !isdigit(*i) && *i != '_' && *i != '.'){
      streamname.erase(i);
    }else{
      *i = tolower(*i);
    }
  }
}

JSON::Value Util::getStreamConfig(const std::string &streamname){
  JSON::Value result;
  if (streamname.size() > 100){
    FAIL_MSG("Stream opening denied: %s is longer than 100 characters (%lu).", streamname.c_str(),
             streamname.size());
    return result;
  }
  std::string smp = streamname.substr(0, streamname.find_first_of("+ "));

  char tmpBuf[NAME_BUFFER_SIZE];
  snprintf(tmpBuf, NAME_BUFFER_SIZE, SHM_STREAM_CONF, smp.c_str());
  Util::DTSCShmReader rStrmConf(tmpBuf);
  DTSC::Scan stream_cfg = rStrmConf.getScan();
  if (!stream_cfg){
    WARN_MSG("Could not get stream '%s' config!", smp.c_str());
    return result;
  }
  return stream_cfg.asJSON();
}

DTSC::Meta Util::getStreamMeta(const std::string &streamname){
  DTSC::Meta ret;
  char pageId[NAME_BUFFER_SIZE];
  snprintf(pageId, NAME_BUFFER_SIZE, SHM_STREAM_INDEX, streamname.c_str());
  IPC::sharedPage mPage(pageId, DEFAULT_STRM_PAGE_SIZE);
  if (!mPage.mapped){
    FAIL_MSG("Could not connect to metadata for %s", streamname.c_str());
    return ret;
  }
  DTSC::Packet tmpMeta(mPage.mapped, mPage.len, true);
  if (tmpMeta.getVersion()){ret.reinit(tmpMeta);}
  return ret;
}

/// Checks if the given streamname has an active input serving it. Returns true if this is the case.
/// Assumes the streamname has already been through sanitizeName()!
bool Util::streamAlive(std::string &streamname){
  char semName[NAME_BUFFER_SIZE];
  snprintf(semName, NAME_BUFFER_SIZE, SEM_INPUT, streamname.c_str());
  IPC::semaphore playerLock(semName, O_RDWR, ACCESSPERMS, 0, true);
  if (!playerLock){return false;}
  if (!playerLock.tryWait()){
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
/// If no, loads up the server configuration and attempts to start the given stream according to
/// current configuration. At this point, fails and aborts if MistController isn't running.
bool Util::startInput(std::string streamname, std::string filename, bool forkFirst, bool isProvider,
                      const std::map<std::string, std::string> &overrides, pid_t *spawn_pid){
  sanitizeName(streamname);
  if (streamname.size() > 100){
    FAIL_MSG("Stream opening denied: %s is longer than 100 characters (%lu).", streamname.c_str(),
             streamname.size());
    return false;
  }
  // Check if the stream is already active.
  // If yes, don't activate again to prevent duplicate inputs.
  // It's still possible a duplicate starts anyway, this is caught in the inputs initializer.
  // Note: this uses the _whole_ stream name, including + (if any).
  // This means "test+a" and "test+b" have separate locks and do not interact with each other.
  uint8_t streamStat = getStreamStatus(streamname);
  // Wait for a maximum of 240 x 250ms sleeps = 60 seconds
  size_t sleeps = 0;
  while (++sleeps < 240 && streamStat != STRMSTAT_OFF && streamStat != STRMSTAT_READY &&
         (!isProvider || streamStat != STRMSTAT_WAIT)){
    if (streamStat == STRMSTAT_BOOT && overrides.count("throughboot")){break;}
    Util::sleep(250);
    streamStat = getStreamStatus(streamname);
  }
  if (streamAlive(streamname) && !overrides.count("alwaysStart")){
    MEDIUM_MSG("Stream %s already active; continuing", streamname.c_str());
    return true;
  }

  /*
   * OLD CODE FOR HARDLIMITS.
   * Maybe re-enable?
   * Still sorta-works, but undocumented...
  {
    IPC::ConfigWrapper confLock(15);
    if (confLock){
      IPC::sharedPage mistConfOut(SHM_CONF, DEFAULT_CONF_PAGE_SIZE);
      DTSC::Scan config = DTSC::Scan(mistConfOut.mapped, mistConfOut.len);
      //Abort if we loaded a config and there is a hardlimit active in it.
      if (config && config.getMember("hardlimit_active")){return false;}
    }
  }
  */

  // Find stream base name
  std::string smp = streamname.substr(0, streamname.find_first_of("+ "));
  // check if base name (everything before + or space) exists
  const JSON::Value stream_cfg = getStreamConfig(streamname);
  if (!stream_cfg){
    HIGH_MSG("Stream %s not configured - attempting to ignore", streamname.c_str());
  }
  /*LTS-START*/
  if (!filename.size()){
    if (stream_cfg && stream_cfg.isMember("hardlimit_active")){return false;}
    if (Triggers::shouldTrigger("STREAM_LOAD", smp)){
      if (!Triggers::doTrigger("STREAM_LOAD", streamname, smp)){return false;}
    }
    if (Triggers::shouldTrigger("STREAM_SOURCE", smp)){
      Triggers::doTrigger("STREAM_SOURCE", streamname, smp, false, filename);
    }
  }
  /*LTS-END*/

  // Only use configured source if not manually overridden. Abort if no config is available.
  if (!filename.size()){
    if (!stream_cfg){
      MEDIUM_MSG("Stream %s not configured, no source manually given, cannot start", streamname.c_str());
      return false;
    }
    filename = stream_cfg["source"].asStringRef();
  }

  streamVariables(filename, streamname);
  const JSON::Value input = getInputBySource(filename, isProvider);
  if (!input){return false;}

  // copy the necessary arguments to separate storage so we can unlock the config semaphore safely
  std::map<std::string, std::string> str_args;
  // check required parameters
  if (input.isMember("required")){
    jsonForEachConst(input["required"], prm){
      const std::string opt = (*prm)["option"].asStringRef();
      // check for overrides
      if (overrides.count(opt)){
        str_args[opt] = overrides.at(opt);
      }else{
        if (!stream_cfg.isMember(prm.key())){
          FAIL_MSG("Required parameter %s for stream %s missing", prm.key().c_str(),
                   streamname.c_str());
          return false;
        }
        str_args[opt] = stream_cfg[opt].asStringRef();
      }
    }
  }
  // check optional parameters
  if (input.isMember("optional")){
    jsonForEachConst(input["optional"], prm){
      const std::string opt = (*prm)["option"].asStringRef();
      // check for overrides
      if (overrides.count(opt)){
        str_args[opt] = overrides.at(opt);
      }else{
        if (stream_cfg.isMember(prm.key())){str_args[opt] = stream_cfg[prm.key()].asStringRef();}
      }
      if (!prm->isMember("type") && str_args.count(opt)){str_args[opt] = "";}
    }
  }

  if (isProvider){
    // Set environment variable so we can know if we have a provider when re-exec'ing.
    setenv("MISTPROVIDER", "1", 1);
  }

  std::string player_bin = Util::getMyPath() + "MistIn" + input["name"].asStringRef();
  INFO_MSG("Starting %s -s %s %s", player_bin.c_str(), streamname.c_str(), filename.c_str());
  char *argv[30] ={(char *)player_bin.c_str(), (char *)"-s", (char *)streamname.c_str(),
                    (char *)filename.c_str()};
  int argNum = 3;
  std::string debugLvl;
  if (Util::Config::printDebugLevel != DEBUG && !str_args.count("--debug")){
    debugLvl = JSON::Value(Util::Config::printDebugLevel).asString();
    argv[++argNum] = (char *)"--debug";
    argv[++argNum] = (char *)debugLvl.c_str();
  }
  for (std::map<std::string, std::string>::iterator it = str_args.begin(); it != str_args.end();
       ++it){
    argv[++argNum] = (char *)it->first.c_str();
    if (it->second.size()){argv[++argNum] = (char *)it->second.c_str();}
  }
  argv[++argNum] = (char *)0;

  Util::Procs::setHandler();

  int pid = 0;
  if (forkFirst){
    DONTEVEN_MSG("Forking");
    pid = fork();
    if (pid == -1){
      FAIL_MSG("Forking process for stream %s failed: %s", streamname.c_str(), strerror(errno));
      return false;
    }
    if (pid && overrides.count("singular")){
      Util::Procs::setHandler();
      Util::Procs::remember(pid);
    }
  }else{
    DONTEVEN_MSG("Not forking");
  }

  if (pid == 0){
    Socket::Connection io(0, 1);
    io.drop();
    DONTEVEN_MSG("execvp");
    execvp(argv[0], argv);
    FAIL_MSG("Starting process %s for stream %s failed: %s", argv[0], streamname.c_str(),
             strerror(errno));
    _exit(42);
  }else if (spawn_pid != NULL){
    *spawn_pid = pid;
  }

  unsigned int waiting = 0;
  while (!streamAlive(streamname) && ++waiting < 240){
    Util::wait(250);
    if (!Util::Procs::isRunning(pid)){
      FAIL_MSG("Input process shut down before stream coming online, aborting.");
      break;
    }
  }

  return streamAlive(streamname);
}

JSON::Value Util::getInputBySource(const std::string &filename, bool isProvider){
  JSON::Value ret;

  // Attempt to load up configuration and find this stream
  Util::DTSCShmReader rCapa(SHM_CAPA);
  DTSC::Scan inputs = rCapa.getMember("inputs");
  // Abort if not available
  if (!inputs){
    FAIL_MSG("Capabilities not available, aborting! Is MistController running?");
    return false;
  }

  // check in curConf for <naam>-priority/source_match
  bool selected = false;
  long long int curPrio = -1;
  DTSC::Scan input;
  unsigned int input_size = inputs.getSize();
  bool noProviderNoPick = false;
  for (unsigned int i = 0; i < input_size; ++i){
    DTSC::Scan tmp_input = inputs.getIndice(i);

    // if match voor current stream && priority is hoger dan wat we al hebben
    if (tmp_input.getMember("source_match") && curPrio < tmp_input.getMember("priority").asInt()){
      if (tmp_input.getMember("source_match").getSize()){
        for (unsigned int j = 0; j < tmp_input.getMember("source_match").getSize(); ++j){
          std::string source = tmp_input.getMember("source_match").getIndice(j).asString();
          std::string front = source.substr(0, source.find('*'));
          std::string back = source.substr(source.find('*') + 1);
          MEDIUM_MSG("Checking input %s: %s (%s)", inputs.getIndiceName(i).c_str(),
                     tmp_input.getMember("name").asString().c_str(), source.c_str());

          if (filename.substr(0, front.size()) == front &&
              filename.substr(filename.size() - back.size()) == back){
            if (tmp_input.getMember("non-provider") && !isProvider){
              noProviderNoPick = true;
              continue;
            }
            curPrio = tmp_input.getMember("priority").asInt();
            selected = true;
            input = tmp_input;
          }
        }
      }else{
        std::string source = tmp_input.getMember("source_match").asString();
        std::string front = source.substr(0, source.find('*'));
        std::string back = source.substr(source.find('*') + 1);
        MEDIUM_MSG("Checking input %s: %s (%s)", inputs.getIndiceName(i).c_str(),
                   tmp_input.getMember("name").asString().c_str(), source.c_str());

        if (filename.substr(0, front.size()) == front &&
            filename.substr(filename.size() - back.size()) == back){
          if (tmp_input.getMember("non-provider") && !isProvider){
            noProviderNoPick = true;
            continue;
          }
          curPrio = tmp_input.getMember("priority").asInt();
          selected = true;
          input = tmp_input;
        }
      }
    }
  }
  if (!selected){
    if (noProviderNoPick){
      INFO_MSG("Not a media provider for input: %s", filename.c_str());
    }else{
      FAIL_MSG("No compatible input found for: %s", filename.c_str());
    }
  }else{
    ret = input.asJSON();
  }
  return ret;
}

/// Attempt to start a push for streamname to target.
/// streamname MUST be pre-sanitized
/// target gets variables replaced and may be altered by the PUSH_OUT_START trigger response.
/// Attempts to match the altered target to an output that can push to it.
pid_t Util::startPush(const std::string &streamname, std::string &target){
  if (Triggers::shouldTrigger("PUSH_OUT_START", streamname)){
    std::string payload = streamname + "\n" + target;
    std::string filepath_response;
    Triggers::doTrigger("PUSH_OUT_START", payload, streamname.c_str(), false, filepath_response);
    target = filepath_response;
  }
  if (!target.size()){
    INFO_MSG("Aborting push of stream %s - target is empty", streamname.c_str());
    return 0;
  }

  //Set original target string in environment
  setenv("MST_ORIG_TARGET", target.c_str(), 1);

  // The target can hold variables like current time etc
  streamVariables(target, streamname);

  // Attempt to load up configuration and find this stream
  std::string output_bin = "";
  {
    Util::DTSCShmReader rCapa(SHM_CAPA);
    DTSC::Scan outputs = rCapa.getMember("connectors");
    if (!outputs){
      FAIL_MSG("Capabilities not available, aborting! Is MistController running?");
      return 0;
    }
    std::string checkTarget = target.substr(0, target.rfind('?'));
    unsigned int outputs_size = outputs.getSize();
    for (unsigned int i = 0; i < outputs_size && !output_bin.size(); ++i){
      DTSC::Scan output = outputs.getIndice(i);
      if (output.getMember("push_urls")){
        unsigned int push_count = output.getMember("push_urls").getSize();
        for (unsigned int j = 0; j < push_count; ++j){
          std::string tar_match = output.getMember("push_urls").getIndice(j).asString();
          std::string front = tar_match.substr(0, tar_match.find('*'));
          std::string back = tar_match.substr(tar_match.find('*') + 1);
          MEDIUM_MSG("Checking output %s: %s (%s)", outputs.getIndiceName(i).c_str(),
                     output.getMember("name").asString().c_str(), checkTarget.c_str());

          if (checkTarget.substr(0, front.size()) == front &&
              checkTarget.substr(checkTarget.size() - back.size()) == back){
            output_bin = Util::getMyPath() + "MistOut" + output.getMember("name").asString();
            break;
          }
        }
      }
    }
  }

  if (output_bin == ""){
    FAIL_MSG("No output found for target %s, aborting push.", target.c_str());
    return 0;
  }
  INFO_MSG("Pushing %s to %s through %s", streamname.c_str(), target.c_str(), output_bin.c_str());
  // Start  output.
  char *argv[] ={(char *)output_bin.c_str(), (char *)"--stream", (char *)streamname.c_str(),
                  (char *)target.c_str(), (char *)NULL};

  int stdErr = 2;
  //Cache return value so we can do some cleaning before we return
  pid_t ret = Util::Procs::StartPiped(argv, 0, 0, &stdErr);
  //Clean up environment
  unsetenv("MST_ORIG_TARGET");
  //Actually return the resulting PID
  return ret;
}

uint8_t Util::getStreamStatus(const std::string &streamname){
  char pageName[NAME_BUFFER_SIZE];
  snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_STATE, streamname.c_str());
  IPC::sharedPage streamStatus(pageName, 1, false, false);
  if (!streamStatus){return STRMSTAT_OFF;}
  return streamStatus.mapped[0];
}

/// Checks if a given user agent is allowed according to the given exception.
bool Util::checkException(const JSON::Value & ex, const std::string & useragent){
  //No user agent? Always allow everything.
  if (!useragent.size()){return true;}
  if (!ex.isArray() || !ex.size()){return true;}
  bool ret = true;
  jsonForEachConst(ex, e){
    if (!e->isArray() || !e->size()){continue;}
    bool setTo = false;
    bool except = false;
    //whitelist makes the return value true if any value is contained in the UA, blacklist makes it false.
    //the '_except' variants do so only if none of the values are contained in the UA.
    if ((*e)[0u].asStringRef() == "whitelist"){setTo = true; except = false;}
    if ((*e)[0u].asStringRef() == "whitelist_except"){setTo = true; except = true;}
    if ((*e)[0u].asStringRef() == "blacklist"){setTo = false; except = false;}
    if ((*e)[0u].asStringRef() == "blacklist_except"){setTo = false; except = true;}
    if (e->size() == 1){
      ret = setTo;
      continue;
    }
    if (!(*e)[1].isArray()){continue;}
    bool match = false;
    jsonForEachConst((*e)[1u], i){
      if (useragent.find(i->asStringRef()) != std::string::npos){
        match = true;
      }
    }
    //set the (temp) return value if this was either a match in regular mode, or a non-match in except-mode.
    if (except != match){ret = setTo;}
  }
  return ret;
}

Util::DTSCShmReader::DTSCShmReader(const std::string &pageName){
  rPage.init(pageName, 0, false, false);
  if (rPage){rAcc = Util::RelAccX(rPage.mapped);}
}

DTSC::Scan Util::DTSCShmReader::getMember(const std::string &indice){
  if (!rPage){return DTSC::Scan();}
  return DTSC::Scan(rAcc.getPointer("dtsc_data"), rAcc.getSize("dtsc_data"))
      .getMember(indice.c_str());
}

DTSC::Scan Util::DTSCShmReader::getScan(){
  if (!rPage){return DTSC::Scan();}
  return DTSC::Scan(rAcc.getPointer("dtsc_data"), rAcc.getSize("dtsc_data"));
}

