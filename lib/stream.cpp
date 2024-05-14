/// \file stream.cpp
/// Utilities for handling streams.

#include "config.h"
#include "defines.h"
#include "dtsc.h"
#include "h265.h"
#include "http_parser.h"
#include "json.h"
#include "langcodes.h"
#include "mp4_generic.h"
#include "procs.h"
#include "shared_memory.h"
#include "socket.h"
#include "url.h"
#include "stream.h"
#include "triggers.h" //LTS
#include <semaphore.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum Util::trackSortOrder Util::defaultTrackSortOrder = TRKSORT_DEFAULT;

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

std::string Util::codecString(const std::string &codec, const std::string &initData){
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
    if (mInfo.general_tier_flag){
      r << 'H';
    }else{
      r << 'L';
    }
    r << (unsigned long)mInfo.general_level_idc;
    if (mInfo.constraint_flags[0]){
      r << '.' << std::hex << (unsigned long)mInfo.constraint_flags[0] << std::dec;
    }
    return r.str();
  }
  if (codec == "AAC"){return "mp4a.40.2";}
  if (codec == "MP3"){return "mp4a.40.34";}
  if (codec == "AC3"){return "mp4a.a5";}
  if (codec == "AV1"){
    if (initData.size() < 4){return "av01";}// Can't determine properties. :-(
    std::stringstream r;
    r << "av01.";
    r << (int)((initData[1] & 0b11100000) >> 5); //profile
    r << ".";
    r << std::setw(2) << std::setfill('0') << (int)(initData[1] & 0b00011111); //level
    r << ((initData[2] & 0b10000000)?"H":"M"); //tier
    r << ".";
    switch (initData[2] & 0b01100000){
      case 0b00000000: r << "08"; break;
      case 0b01000000: r << "10"; break;
      case 0b01100000: r << "12"; break;
      case 0b00100000: r << "??"; break;
    }
    /// \TODO Implement the full descriptor as in https://aomediacodec.github.io/av1-isobmff/#codecsparam
    /// Init data follows this format:
    /// https://aomediacodec.github.io/av1-isobmff/#av1codecconfigurationbox-syntax
    return r.str();
  }
  return "";
}

/// Local-only helper function that replaces a variable and returns the amount of replacements done
size_t replaceVar(std::string & input, const std::string & var, const std::string & rep){
  size_t count = 0;
  const std::string withBraces = "${"+var+"}";
  const std::string noBraces = "$"+var;
  count += Util::replace(input, withBraces, rep);
  count += Util::replace(input, noBraces, rep);
  return count;
}

size_t Util::streamCustomVariables(std::string &str){
  size_t count = 0;
  // Read shared memory page containing custom variables
  static IPC::sharedPage variablePage(SHM_CUSTOM_VARIABLES, 0, false, false);
  // Check if the page needs to be reopened
  if (variablePage.mapped){
    Util::RelAccX varAccX(variablePage.mapped, false);
    if (varAccX.isReload()){variablePage.close();}
  }
  // Reopen memory page if it has been closed
  if (!variablePage.mapped){
    variablePage.init(SHM_CUSTOM_VARIABLES, 0, false, false);
    if(!variablePage.mapped){
      ERROR_MSG("Unable to substitute custom variables, as memory page %s failed to open", SHM_CUSTOM_VARIABLES);
      return 0;
    }
  }
  // Extract variables
  Util::RelAccX varAccX(variablePage.mapped, false);
  for (size_t i = 0; i < varAccX.getEndPos(); i++){
    if (varAccX.getPointer("name", i)){
      count += replaceVar(str, varAccX.getPointer("name", i), varAccX.getPointer("lastVal", i));
    }
  }
  return count;
}

/// Replaces all stream-related variables in the given 'str' with their values.
size_t Util::streamVariables(std::string &str, const std::string &streamname, const std::string &source, uint8_t depth){
  size_t replaced = 0;
  if (depth > 9){
    WARN_MSG("Reached a depth of %u when replacing stream variables", depth);
    return 0;
  }
  // If there are no variables, abort
  if (str.find('$') == std::string::npos){return 0;}
  // Find and replace any custom variables
  replaced += streamCustomVariables(str);
  replaced += replaceVar(str, "source", source);
  replaced += replaceVar(str, "datetime", "$year.$month.$day.$hour.$minute.$second");
  replaced += replaceVar(str, "day", strftime_now("%d"));
  replaced += replaceVar(str, "month", strftime_now("%m"));
  replaced += replaceVar(str, "year", strftime_now("%Y"));
  replaced += replaceVar(str, "hour", strftime_now("%H"));
  replaced += replaceVar(str, "minute", strftime_now("%M"));
  replaced += replaceVar(str, "second", strftime_now("%S"));
  replaced += replaceVar(str, "wday", strftime_now("%u")); // weekday, 1-7, monday=1
  replaced += replaceVar(str, "yday", strftime_now("%j")); // yearday, 001-366
  replaced += replaceVar(str, "week", strftime_now("%V")); // week number, 01-53
  replaced += replaceVar(str, "stream", streamname);
  if (streamname.find('+') != std::string::npos){
    std::string strbase = streamname.substr(0, streamname.find('+'));
    std::string strext = streamname.substr(streamname.find('+') + 1);
    replaced += replaceVar(str, "basename", strbase);
    replaced += replaceVar(str, "wildcard", strext);
    if (strext.size()){
      replaced += replaceVar(str, "pluswildcard", "+" + strext);
    }else{
      replaced += replaceVar(str, "pluswildcard", "");
    }
  }else{
    replaced += replaceVar(str, "basename", streamname);
    replaced += replaceVar(str, "wildcard", "");
    replaced += replaceVar(str, "pluswildcard", "");
  }
  // Continue recursively if we've replaced a variable which exposed another variable to be replaced
  if (replaced && str.find('$') != std::string::npos){replaced += streamVariables(str, streamName, source, ++depth);}
  return replaced;
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
    if (!isalpha(*i) && !isdigit(*i) && *i != '_' && *i != '.' && *i != '-'){
      streamname.erase(i);
    }else{
      *i = tolower(*i);
    }
  }
}

/// Initalizes the packetSorter in sync mode.
Util::packetSorter::packetSorter(){
  dequeMode = false;
}

/// Sets sync mode on if true (sync), off if false (async).
void Util::packetSorter::setSyncMode(bool synced){
  if (dequeMode != !synced){
    dequeMode = !synced;
    if (!dequeMode){
      //we've switched away from deque
      for (std::deque<Util::sortedPageInfo>::iterator it = dequeBuffer.begin(); it != dequeBuffer.end(); ++it){
        insert(*it);
      }
      dequeBuffer.clear();
    }else{
      //we've switched away from set
      for (std::set<Util::sortedPageInfo>::iterator it = setBuffer.begin(); it != setBuffer.end(); ++it){
        insert(*it);
      }
      setBuffer.clear();
    }
  }
}

/// Returns true if we're synced, false if async.
bool Util::packetSorter::getSyncMode() const{return !dequeMode;}

/// Returns the amount of packets currently in the sorter.
size_t Util::packetSorter::size() const{
  if (dequeMode){return dequeBuffer.size();}else{return setBuffer.size();}
}

/// Clears all packets from the sorter; does not reset mode.
void Util::packetSorter::clear(){
  dequeBuffer.clear();
  setBuffer.clear();
}

/// Returns a pointer to the first packet in the sorter.
const Util::sortedPageInfo * Util::packetSorter::begin() const{
  if (dequeMode){
    return &*dequeBuffer.begin();
  }else{
    return &*setBuffer.begin();
  }
}

/// Inserts a new packet in the sorter.
void Util::packetSorter::insert(const sortedPageInfo &pInfo){
  if (dequeMode){
    dequeBuffer.push_back(pInfo);
  }else{
    setBuffer.insert(pInfo);
  }
}

/// Removes the given track ID packet from the sorter. Removes at most one packet, make sure to prevent duplicates elsewhere!
void Util::packetSorter::dropTrack(size_t tid){
  if (dequeMode){
    for (std::deque<Util::sortedPageInfo>::iterator it = dequeBuffer.begin(); it != dequeBuffer.end(); ++it){
      if (it->tid == tid){
        dequeBuffer.erase(it);
        return;
      }
    }
  }else{
    for (std::set<Util::sortedPageInfo>::iterator it = setBuffer.begin(); it != setBuffer.end(); ++it){
      if (it->tid == tid){
        setBuffer.erase(it);
        return;
      }
    }
  }
}

/// Removes the first packet from the sorter and inserts the given packet.
void Util::packetSorter::replaceFirst(const sortedPageInfo &pInfo){
  if (dequeMode){
    //in deque mode, insertion of the new packet is at the back
    //this works, as a failure to retrieve a packet will swap the front entry to the back as well
    dequeBuffer.pop_front();
    dequeBuffer.push_back(pInfo);
  }else{
    setBuffer.erase(setBuffer.begin());
    setBuffer.insert(pInfo);
  }
}

/// Removes the first packet from the sorter and inserts it back at the end. No-op for sync mode.
void Util::packetSorter::moveFirstToEnd(){
  if (dequeMode){
    dequeBuffer.push_back(dequeBuffer.front());
    dequeBuffer.pop_front();
  }
}

/// Returns true if there is an entry in the sorter for the given track ID.
bool Util::packetSorter::hasEntry(size_t tid) const{
  if (dequeMode){
    for (std::deque<Util::sortedPageInfo>::const_iterator it = dequeBuffer.begin(); it != dequeBuffer.end(); ++it){
      if (it->tid == tid){return true;}
    }
  }else{
    for (std::set<Util::sortedPageInfo>::const_iterator it = setBuffer.begin(); it != setBuffer.end(); ++it){
      if (it->tid == tid){return true;}
    }
  }
  return false;
}

/// Fills toFill with track IDs of tracks that are in the sorter.
void Util::packetSorter::getTrackList(std::set<size_t> &toFill) const{
  toFill.clear();
  if (dequeMode){
    for (std::deque<Util::sortedPageInfo>::const_iterator it = dequeBuffer.begin(); it != dequeBuffer.end(); ++it){
      toFill.insert(it->tid);
    }
  }else{
    for (std::set<Util::sortedPageInfo>::const_iterator it = setBuffer.begin(); it != setBuffer.end(); ++it){
      toFill.insert(it->tid);
    }
  }
}

/// Fills toFill with track IDs and current playback position of tracks that are in the sorter.
void Util::packetSorter::getTrackList(std::map<size_t, uint64_t> &toFill) const{
  toFill.clear();
  if (dequeMode){
    for (std::deque<Util::sortedPageInfo>::const_iterator it = dequeBuffer.begin(); it != dequeBuffer.end(); ++it){
      toFill[it->tid] = it->time;
    }
  }else{
    for (std::set<Util::sortedPageInfo>::const_iterator it = setBuffer.begin(); it != setBuffer.end(); ++it){
      toFill[it->tid] = it->time;
    }
  }
}

JSON::Value Util::getStreamConfig(const std::string &streamname){
  JSON::Value result;
  if (streamname.size() > 100){
    FAIL_MSG("Stream opening denied: %s is longer than 100 characters (%zu).", streamname.c_str(),
             streamname.size());
    return result;
  }
  std::string smp = streamname.substr(0, streamname.find_first_of("+ "));

  char tmpBuf[NAME_BUFFER_SIZE];
  snprintf(tmpBuf, NAME_BUFFER_SIZE, SHM_STREAM_CONF, smp.c_str());
  Util::DTSCShmReader rStrmConf(tmpBuf);
  DTSC::Scan stream_cfg = rStrmConf.getScan();
  if (!stream_cfg){
    if (!Util::getGlobalConfig("defaultStream")){
      WARN_MSG("Could not get stream '%s' config!", smp.c_str());
    }else{
      INFO_MSG("Could not get stream '%s' config, not emitting WARN message because fallback is "
               "configured",
               smp.c_str());
    }
    return result;
  }
  return stream_cfg.asJSON();
}

JSON::Value Util::getGlobalConfig(const std::string &optionName){
  IPC::sharedPage globCfg(SHM_GLOBAL_CONF);
  if (!globCfg.mapped){
    FAIL_MSG("Could not open global configuration options to read setting for '%s'", optionName.c_str());
    return JSON::Value();
  }
  Util::RelAccX cfgData(globCfg.mapped);
  if (!cfgData.isReady()){
    FAIL_MSG("Global configuration options not ready; cannot read setting for '%s'", optionName.c_str());
    return JSON::Value();
  }
  Util::RelAccXFieldData dataField = cfgData.getFieldData(optionName);
  switch (dataField.type & 0xF0){
  case RAX_INT:
  case RAX_UINT:
    // Integer types, return JSON::Value integer
    return JSON::Value(cfgData.getInt(dataField));
  case RAX_RAW:
  case RAX_STRING:
    // String types, return JSON::Value string
    return JSON::Value(std::string(cfgData.getPointer(dataField), cfgData.getSize(optionName)));
  default:
    // Unimplemented types
    FAIL_MSG("Global configuration setting for '%s' is not an implemented datatype!", optionName.c_str());
    return JSON::Value();
  }
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

/// Returns active tags for an exact-matching (already sanitized) streamname
std::set<std::string> Util::streamTags(const std::string &streamname){
  std::set<std::string> ret;

  IPC::sharedPage shmStreams(SHM_STATE_STREAMS, 0, false, false);
  // Abort silently if page cannot be loaded
  if (!shmStreams){return ret;}

  Util::RelAccX rlxStreams(shmStreams.mapped);
  // Abort silently if page cannot be loaded
  if (!rlxStreams.isReady()){return ret;}

  uint64_t startPos = rlxStreams.getDeleted();
  uint64_t endPos = rlxStreams.getEndPos();
  for (uint64_t cPos = startPos; cPos < endPos; ++cPos){
    const std::string & strm = rlxStreams.getPointer("stream", cPos);
    if (strm != streamname){continue;}

    // Found it! Fill and break, since only one match can exist.
    std::string tags = rlxStreams.getPointer("tags", cPos);
    while (tags.size()){
      size_t endPos = tags.find(' ');
      if (!endPos){
        //extra space, ignore
        tags.erase(0, 1);
        continue;
      }
      if (endPos == std::string::npos){endPos = tags.size();}
      ret.insert(tags.substr(0, endPos));
      if (endPos == tags.size()){break;}
      tags.erase(0, endPos+1);
    }
    break;
  }
  return ret;
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
    FAIL_MSG("Stream opening denied: %s is longer than 100 characters (%zu).", streamname.c_str(),
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

  bool hadOriginal = getenv("MIST_ORIGINAL_SOURCE");
  if (!hadOriginal){setenv("MIST_ORIGINAL_SOURCE", filename.c_str(), 1);}
  streamVariables(filename, streamname);
  const JSON::Value input = getInputBySource(filename, isProvider);
  if (!input){return false;}

  // copy the necessary arguments to separate storage so we can unlock the config semaphore safely
  std::map<std::string, std::string> str_args;
  // check required parameters
  if (input.isMember("required")){
    jsonForEachConst(input["required"], prm){
      if (!prm->isMember("option")){continue;}
      const std::string opt = (*prm)["option"].asStringRef();
      // check for overrides
      if (overrides.count(prm.key())){
        HIGH_MSG("Overriding option '%s' to '%s'", prm.key().c_str(), overrides.at(prm.key()).c_str());
        str_args[opt] = overrides.at(prm.key());
      }else{
        if (!stream_cfg.isMember(prm.key())){
          FAIL_MSG("Required parameter %s for stream %s missing", prm.key().c_str(), streamname.c_str());
          return false;
        }
        if (stream_cfg[prm.key()].isString()){
          str_args[opt] = stream_cfg[prm.key()].asStringRef();
        }else{
          str_args[opt] = stream_cfg[prm.key()].toString();
        }
      }
    }
  }
  // check optional parameters
  if (input.isMember("optional")){
    jsonForEachConst(input["optional"], prm){
      if (!prm->isMember("option")){continue;}
      const std::string opt = (*prm)["option"].asStringRef();
      // check for overrides
      if (overrides.count(prm.key())){
        HIGH_MSG("Overriding option '%s' to '%s'", prm.key().c_str(), overrides.at(prm.key()).c_str());
        str_args[opt] = overrides.at(prm.key());
      }else{
        if (stream_cfg.isMember(prm.key()) && ((!prm->isMember("default") && stream_cfg[prm.key()]) || (prm->isMember("default") && stream_cfg[prm.key()] != (*prm)["default"]))){
          if (stream_cfg[prm.key()].isString()){
            str_args[opt] = stream_cfg[prm.key()].asStringRef();
          }else{
            str_args[opt] = stream_cfg[prm.key()].toString();
          }
        }
      }
      if (!prm->isMember("type") && str_args.count(opt)){str_args[opt] = "";}
    }
  }

  if (isProvider){
    // Set environment variable so we can know if we have a provider when re-exec'ing.
    setenv("MISTPROVIDER", "1", 1);
  }

  std::string player_bin = "MistIn" + input["name"].asStringRef();
  std::string mistserver_bin = Util::getMyPath() + "MistServer";
  char *argv[30] ={(char *)(mistserver_bin).c_str(), (char *)player_bin.c_str(), (char *)"-s", (char *)streamname.c_str(),
                    (char *)filename.c_str()};
  int argNum = 4;
  std::string debugLvl;
  if (Util::printDebugLevel != DEBUG && !str_args.count("--debug")){
    debugLvl = JSON::Value(Util::printDebugLevel).asString();
    argv[++argNum] = (char *)"--debug";
    argv[++argNum] = (char *)debugLvl.c_str();
  }
  for (std::map<std::string, std::string>::iterator it = str_args.begin(); it != str_args.end(); ++it){
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
      if (!hadOriginal){unsetenv("MIST_ORIGINAL_SOURCE");}
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
    for (std::set<int>::iterator it = Util::Procs::socketList.begin();
         it != Util::Procs::socketList.end(); ++it){
      close(*it);
    }
    Socket::Connection io(0, 1);
    io.drop();
    std::stringstream args;
    for (size_t i = 0; i < 30; ++i){
      if (!argv[i] || !argv[i][0]){break;}
      args << argv[i] << " ";
    }
    INFO_MSG("Starting %s", args.str().c_str());
    execvp(argv[0], argv);
    FAIL_MSG("Starting process %s failed: %s", argv[0], strerror(errno));
    _exit(42);
  }else if (spawn_pid != NULL){
    *spawn_pid = pid;
  }
  if (!hadOriginal){unsetenv("MIST_ORIGINAL_SOURCE");}

  unsigned int waiting = 0;
  while (!streamAlive(streamname) && ++waiting < 240){
    Util::wait(250);
    if (!Util::Procs::isRunning(pid)){
      FAIL_MSG("Input process (PID %d) shut down before stream coming online, aborting.", pid);
      break;
    }
  }

  return streamAlive(streamname);
}

JSON::Value Util::getInputBySource(const std::string &filename, bool isProvider){
  std::string tmpFn = filename;
  if (tmpFn.find('?') != std::string::npos){tmpFn.erase(tmpFn.find('?'), std::string::npos);}
  JSON::Value ret;

  // Attempt to load up configuration and find this stream
  Util::DTSCShmReader rCapa(SHM_CAPA);
  DTSC::Scan inputs = rCapa.getMember("inputs");
  // Abort if not available
  if (!inputs){
    FAIL_MSG("Capabilities not available, aborting! Is MistController running?");
    return JSON::Value();
  }

  // check in curConf for <naam>-priority/source_match
  bool selected = false;
  long long int curPrio = -1;
  DTSC::Scan input;
  unsigned int input_size = inputs.getSize();
  bool noProviderNoPick = false;
  for (unsigned int i = 0; i < input_size; ++i){
    DTSC::Scan tmp_input = inputs.getIndice(i);

    // if name prefix based match, always force 99 priority
    if (tmp_input.getMember("name")){
      std::string inPrefix = tmp_input.getMember("name").asString() + ":";
      if (tmpFn.size() > inPrefix.size()){
        Util::stringToLower(inPrefix);
        std::string fnPrefix = tmpFn.substr(0, inPrefix.size());
        Util::stringToLower(fnPrefix);
        if (inPrefix == fnPrefix){
          if (tmp_input.getMember("non-provider") && !isProvider){
            noProviderNoPick = true;
            continue;
          }
          curPrio = 99;
          selected = true;
          input = tmp_input;
        }
      }
    }

    // if match voor current stream && priority is hoger dan wat we al hebben
    if (tmp_input.getMember("source_match") && curPrio < tmp_input.getMember("priority").asInt()){
      if (tmp_input.getMember("source_match").getSize()){
        for (unsigned int j = 0; j < tmp_input.getMember("source_match").getSize(); ++j){
          std::string source = tmp_input.getMember("source_match").getIndice(j).asString();
          std::string front = source.substr(0, source.find('*'));
          std::string back = source.substr(source.find('*') + 1);
          MEDIUM_MSG("Checking input %s: %s (%s)", inputs.getIndiceName(i).c_str(),
                     tmp_input.getMember("name").asString().c_str(), source.c_str());

          if (tmpFn.size() >= front.size()+back.size() && tmpFn.substr(0, front.size()) == front && tmpFn.substr(tmpFn.size() - back.size()) == back){
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

        if (tmpFn.size() >= front.size()+back.size() && tmpFn.substr(0, front.size()) == front && tmpFn.substr(tmpFn.size() - back.size()) == back){
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
      INFO_MSG("Not a media provider for input: %s", tmpFn.c_str());
    }else{
      FAIL_MSG("No compatible input found for: %s", tmpFn.c_str());
    }
  }else{
    ret = input.asJSON();
  }
  return ret;
}

/// Sends a message to the local UDP API port
void Util::sendUDPApi(JSON::Value & cmd){
  HTTP::URL UDPAddr(getGlobalConfig("udpApi").asStringRef());
  if (UDPAddr.protocol != "udp"){
    FAIL_MSG("Local UDP API address not defined; can't send command to MistController!");
    return;
  }
  Socket::UDPConnection uSock;
  uSock.SetDestination(UDPAddr.host, UDPAddr.getPort());
  uSock.SendNow(cmd.toString());
}

/// Attempt to start a push for streamname to target.
/// streamname MUST be pre-sanitized
/// target gets variables replaced and may be altered by the PUSH_OUT_START trigger response.
/// Attempts to match the altered target to an output that can push to it.
pid_t Util::startPush(const std::string &streamname, std::string &target, int debugLvl){
  if (Triggers::shouldTrigger("PUSH_OUT_START", streamname)){
    std::string payload = streamname + "\n" + target;
    std::string filepath_response = target;
    Triggers::doTrigger("PUSH_OUT_START", payload, streamname.c_str(), false, filepath_response);
    target = filepath_response;
  }
  if (!target.size()){
    INFO_MSG("Aborting push of stream %s - target is empty", streamname.c_str());
    return 0;
  }

  // Set original target string in environment
  setenv("MST_ORIG_TARGET", target.c_str(), 1);
  //If no debug level set, default to level of starting process
  if (debugLvl < 0){debugLvl = Util::printDebugLevel;}

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
          //Check for external writer support
          if (front == "/" && back.size() && checkTarget.substr(checkTarget.size() - back.size()) == back){
            HTTP::URL tUri(target);
            // If it is a remote target, we might need to spawn an external binary
            if (tUri.isLocalPath()){continue;}
            // Read configured external writers
            IPC::sharedPage extwriPage(EXTWRITERS, 0, false, false);
            if (extwriPage.mapped){
              Util::RelAccX extWri(extwriPage.mapped, false);
              if (extWri.isReady()){
                for (uint64_t i = 0; i < extWri.getEndPos(); i++){
                  Util::RelAccX protocols = Util::RelAccX(extWri.getPointer("protocols", i));
                  uint8_t protocolCount = protocols.getPresent();
                  JSON::Value protocolArray;
                  for (uint8_t idx = 0; idx < protocolCount; idx++){
                    if (tUri.protocol == protocols.getPointer("protocol", idx)){
                      output_bin = Util::getMyPath() + "MistOut" + output.getMember("name").asString();
                      break;
                    }
                    if (output_bin.size()){break;}
                  }
                  if (output_bin.size()){break;}
                }
              }
            }
          }
        }
      }
    }
  }

  if (!output_bin.size()){
    FAIL_MSG("No output found for target %s, aborting push.", target.c_str());
    return 0;
  }
  INFO_MSG("Pushing %s to %s through %s", streamname.c_str(), target.c_str(), output_bin.c_str());
  // Start  output.
  std::string dLvl = JSON::Value(debugLvl).asString();
  char *argv[] ={(char *)output_bin.c_str(), (char *)"--stream", (char *)streamname.c_str(),
                  (char *)target.c_str(), 0, 0, 0};
  if (debugLvl != DEBUG){
    argv[4] = (char*)"-g";
    argv[5] = (char*)dLvl.c_str();
  }
  int stdErr = 2;
  // Cache return value so we can do some cleaning before we return
  pid_t ret = Util::Procs::StartPiped(argv, 0, 0, &stdErr);
  // Clean up environment
  unsetenv("MST_ORIG_TARGET");
  // Actually return the resulting PID
  return ret;
}

uint8_t Util::getStreamStatus(const std::string &streamname){
  char pageName[NAME_BUFFER_SIZE];
  snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_STATE, streamname.c_str());
  IPC::sharedPage streamStatus(pageName, 2, false, false);
  if (!streamStatus){return STRMSTAT_OFF;}
  return streamStatus.mapped[0];
}

uint8_t Util::getStreamStatusPercentage(const std::string &streamname){
  char pageName[NAME_BUFFER_SIZE];
  snprintf(pageName, NAME_BUFFER_SIZE, SHM_STREAM_STATE, streamname.c_str());
  IPC::sharedPage streamStatus(pageName, 2, false, false);
  if (!streamStatus || streamStatus.len < 2){return 0;}
  return streamStatus.mapped[1];
}

/// Checks if a given user agent is allowed according to the given exception.
bool Util::checkException(const JSON::Value &ex, const std::string &useragent){
  // No user agent? Always allow everything.
  if (!useragent.size()){return true;}
  if (!ex.isArray() || !ex.size()){return true;}
  bool ret = true;
  jsonForEachConst(ex, e){
    if (!e->isArray() || !e->size()){continue;}
    bool setTo = false;
    bool except = false;
    // whitelist makes the return value true if any value is contained in the UA, blacklist makes it
    // false. the '_except' variants do so only if none of the values are contained in the UA.
    if ((*e)[0u].asStringRef() == "whitelist"){
      setTo = true;
      except = false;
    }
    if ((*e)[0u].asStringRef() == "whitelist_except"){
      setTo = true;
      except = true;
    }
    if ((*e)[0u].asStringRef() == "blacklist"){
      setTo = false;
      except = false;
    }
    if ((*e)[0u].asStringRef() == "blacklist_except"){
      setTo = false;
      except = true;
    }
    if (e->size() == 1){
      ret = setTo;
      continue;
    }
    if (!(*e)[1].isArray()){continue;}
    bool match = false;
    jsonForEachConst((*e)[1u], i){
      if (useragent.find(i->asStringRef()) != std::string::npos){match = true;}
    }
    // set the (temp) return value if this was either a match in regular mode, or a non-match in except-mode.
    if (except != match){ret = setTo;}
  }
  return ret;
}

Util::DTSCShmReader::DTSCShmReader(const std::string &pageName){
  size_t attempts = 0;
  do {
    rPage.init(pageName, 0, false, false);
    ++attempts;
    if (!rPage && attempts < 5){Util::sleep(10);}
  } while (!rPage && attempts < 5);
  if (rPage){rAcc = Util::RelAccX(rPage.mapped);}
}

DTSC::Scan Util::DTSCShmReader::getMember(const std::string &indice){
  if (!rPage){return DTSC::Scan();}
  return DTSC::Scan(rAcc.getPointer("dtsc_data"), rAcc.getSize("dtsc_data")).getMember(indice.c_str());
}

DTSC::Scan Util::DTSCShmReader::getScan(){
  if (!rPage){return DTSC::Scan();}
  return DTSC::Scan(rAcc.getPointer("dtsc_data"), rAcc.getSize("dtsc_data"));
}

/// Takes an existing track list, and selects tracks from it according to the given track type and selector
std::set<size_t> Util::pickTracks(const DTSC::Meta &M, const std::set<size_t> trackList, const std::string &trackType, const std::string &trackVal){
  std::set<size_t> result;
  if (!trackVal.size()){return result;}//empty selector
  if (trackVal == "-1" || trackVal == "none"){return result;}// don't select anything in particular

  // Comma-separated list, loop over each in order.
  if (trackVal.find(',') != std::string::npos){
    std::stringstream ss(trackVal);
    std::string item;
    while (std::getline(ss, item, ',')){
      switch (item[0]){
        case '|':{//Narrow existing selection
          result = pickTracks(M, result, trackType, item.substr(1));
        } break;
        case '!':{//Remove from existing selection
          std::set<size_t> items = pickTracks(M, result, trackType, item.substr(1));
          if (items.size()){
            for (std::set<size_t>::const_iterator it = items.begin(); it != items.end(); ++it){
              result.erase(*it);
            }
          }
        } break;
        default:{ //Union with existing selection
          std::set<size_t> items = pickTracks(M, trackList, trackType, item);
          result.insert(items.begin(), items.end());
        }
      }
    }
    return result;
  }

  //Literal track ID, does not check against trackList
  size_t idx = JSON::Value(trackVal).asInt();
  if (trackVal == JSON::Value((uint64_t)idx).asString()){
    if (!M.trackValid(idx)){
      WARN_MSG("Track %zu does not exist in stream, cannot select", idx);
      return result;
    }
    if (trackType.size() && M.getType(idx) != trackType && M.getCodec(idx) != trackType){
      WARN_MSG("Track %zu is not %s (%s/%s), cannot select", idx, trackType.c_str(),
               M.getType(idx).c_str(), M.getCodec(idx).c_str());
      return result;
    }
    result.insert(idx);
    return result;
  }

  //Convert selector to lower case
  std::string trackLow = trackVal;
  Util::stringToLower(trackLow);

  //Select all tracks in trackList of the given type
  if (trackLow == "all" || trackLow == "*"){
    // select all tracks of this type
    for (std::set<size_t>::iterator it = trackList.begin(); it != trackList.end(); it++){
      if (!trackType.size() || M.getType(*it) == trackType || M.getCodec(*it) == trackType){result.insert(*it);}
    }
    return result;
  }

  //Select highest bit rate in trackList of given type
  if (trackLow == "highbps" || trackLow == "bestbps" || trackLow == "maxbps"){
    size_t currVal = INVALID_TRACK_ID;
    uint32_t currRate = 0;
    for (std::set<size_t>::iterator it = trackList.begin(); it != trackList.end(); it++){
      if (!trackType.size() || M.getType(*it) == trackType || M.getCodec(*it) == trackType){
        if (currRate < M.getBps(*it)){
          currVal = *it;
          currRate = M.getBps(*it);
        }
      }
    }
    if (currVal != INVALID_TRACK_ID){result.insert(currVal);}
    return result;
  }

  //Select lowest bit rate in trackList of this type
  if (trackLow == "lowbps" || trackLow == "worstbps" || trackLow == "minbps"){
    size_t currVal = INVALID_TRACK_ID;
    uint32_t currRate = INVALID_TRACK_ID;
    for (std::set<size_t>::iterator it = trackList.begin(); it != trackList.end(); it++){
      if (!trackType.size() || M.getType(*it) == trackType || M.getCodec(*it) == trackType){
        if (currRate > M.getBps(*it)){
          currVal = *it;
          currRate = M.getBps(*it);
        }
      }
    }
    if (currVal != INVALID_TRACK_ID){result.insert(currVal);}
    return result;
  }

  //less-than or greater-than track matching on bit rate or resolution
  if (trackLow[0] == '<' || trackLow[0] == '>'){
    unsigned int bpsVal;
    uint64_t targetBps = 0;
    if (trackLow.find("bps") != std::string::npos && sscanf(trackLow.c_str(), "<%ubps", &bpsVal) == 1){targetBps = bpsVal;}
    if (trackLow.find("kbps") != std::string::npos && sscanf(trackLow.c_str(), "<%ukbps", &bpsVal) == 1){targetBps = bpsVal*1024;}
    if (trackLow.find("mbps") != std::string::npos && sscanf(trackLow.c_str(), "<%umbps", &bpsVal) == 1){targetBps = bpsVal*1024*1024;}
    if (trackLow.find("bps") != std::string::npos && sscanf(trackLow.c_str(), ">%ubps", &bpsVal) == 1){targetBps = bpsVal;}
    if (trackLow.find("kbps") != std::string::npos && sscanf(trackLow.c_str(), ">%ukbps", &bpsVal) == 1){targetBps = bpsVal*1024;}
    if (trackLow.find("mbps") != std::string::npos && sscanf(trackLow.c_str(), ">%umbps", &bpsVal) == 1){targetBps = bpsVal*1024*1024;}
    if (targetBps){
      targetBps /= 8;
      // select all tracks of this type that match the requirements
      for (std::set<size_t>::iterator it = trackList.begin(); it != trackList.end(); it++){
        if (!trackType.size() || M.getType(*it) == trackType || M.getCodec(*it) == trackType){
          if (trackLow[0] == '>' && M.getBps(*it) > targetBps){result.insert(*it);}
          if (trackLow[0] == '<' && M.getBps(*it) < targetBps){result.insert(*it);}
        }
      }
      return result;
    }
    unsigned int resX, resY;
    uint64_t targetArea = 0;
    if (sscanf(trackLow.c_str(), "<%ux%u", &resX, &resY) == 2){targetArea = resX*resY;}
    if (sscanf(trackLow.c_str(), ">%ux%u", &resX, &resY) == 2){targetArea = resX*resY;}
    if (targetArea){
      // select all tracks of this type that match the requirements
      for (std::set<size_t>::iterator it = trackList.begin(); it != trackList.end(); it++){
        if (!trackType.size() || M.getType(*it) == trackType || M.getCodec(*it) == trackType){
          uint64_t trackArea = M.getWidth(*it)*M.getHeight(*it);
          if (trackLow[0] == '>' && trackArea > targetArea){result.insert(*it);}
          if (trackLow[0] == '<' && trackArea < targetArea){result.insert(*it);}
        }
      }
      return result;
    }
  }


  //Select the single highest bit rate at or under the given bit rate
  if (trackLow.size() > 7 && trackLow.substr(0, 4) == "max<"){
    unsigned int bpsVal;
    uint64_t targetBps = 0;
    if (trackLow.find("bps") != std::string::npos && sscanf(trackLow.c_str(), "max<%ubps", &bpsVal) == 1){targetBps = bpsVal;}
    if (trackLow.find("kbps") != std::string::npos && sscanf(trackLow.c_str(), "max<%ukbps", &bpsVal) == 1){targetBps = bpsVal*1024;}
    if (trackLow.find("mbps") != std::string::npos && sscanf(trackLow.c_str(), "max<%umbps", &bpsVal) == 1){targetBps = bpsVal*1024*1024;}
    if (targetBps){
      targetBps /= 8;
      // select nearest bit rate track of this type
      size_t currVal = INVALID_TRACK_ID;
      uint32_t currDist = 0;
      bool foundUnder = false;
      for (std::set<size_t>::iterator it = trackList.begin(); it != trackList.end(); it++){
        if (!trackType.size() || M.getType(*it) == trackType || M.getCodec(*it) == trackType){
          if (M.getBps(*it) <= targetBps){
            if (!foundUnder || currDist > (targetBps-M.getBps(*it))){
              currVal = *it;
              currDist = (M.getBps(*it) >= targetBps)?(M.getBps(*it)-targetBps):(targetBps-M.getBps(*it));
            }
            foundUnder = true;
          }else if(!foundUnder){
            if (currVal == INVALID_TRACK_ID || currDist > (M.getBps(*it)-targetBps)){
              currVal = *it;
              currDist = (M.getBps(*it) >= targetBps)?(M.getBps(*it)-targetBps):(targetBps-M.getBps(*it));
            }
          }
        }
      }
      if (currVal != INVALID_TRACK_ID){result.insert(currVal);}
      return result;
    }
  }

  //Select single track with bit rate closest to the given bit rate
  {
    unsigned int bpsVal;
    uint64_t targetBps = 0;
    if (trackLow.find("bps") != std::string::npos && sscanf(trackLow.c_str(), "%ubps", &bpsVal) == 1){targetBps = bpsVal;}
    if (trackLow.find("kbps") != std::string::npos && sscanf(trackLow.c_str(), "%ukbps", &bpsVal) == 1){targetBps = bpsVal*1024;}
    if (trackLow.find("mbps") != std::string::npos && sscanf(trackLow.c_str(), "%umbps", &bpsVal) == 1){targetBps = bpsVal*1024*1024;}
    if (targetBps){
      targetBps /= 8;
      // select nearest bit rate track of this type
      size_t currVal = INVALID_TRACK_ID;
      uint32_t currDist = 0;
      for (std::set<size_t>::iterator it = trackList.begin(); it != trackList.end(); it++){
        if (!trackType.size() || M.getType(*it) == trackType || M.getCodec(*it) == trackType){
          if (currVal == INVALID_TRACK_ID || (M.getBps(*it) >= targetBps && currDist > (M.getBps(*it)-targetBps)) || (M.getBps(*it) < targetBps && currDist > (targetBps-M.getBps(*it)))){
            currVal = *it;
            currDist = (M.getBps(*it) >= targetBps)?(M.getBps(*it)-targetBps):(targetBps-M.getBps(*it));
          }
        }
      }
      if (currVal != INVALID_TRACK_ID){result.insert(currVal);}
      return result;
    }
  }

  //Match on audio channel count (the only audio-specific match)
  if (!trackType.size() || trackType == "audio"){
    if (trackLow == "surround"){
      for (std::set<size_t>::iterator it = trackList.begin(); it != trackList.end(); it++){
        if (!trackType.size() || M.getType(*it) == trackType || M.getCodec(*it) == trackType){
          if (M.getChannels(*it) > 2){result.insert(*it);}
        }
      }
      return result;
    }
    unsigned int channelVal;
    uint32_t targetChannel = 0;
    if (trackLow == "mono"){targetChannel = 1;}
    if (trackLow == "stereo"){targetChannel = 2;}
    if (trackLow == "stereo"){targetChannel = 2;}
    if (trackLow.find("ch") != std::string::npos && sscanf(trackLow.c_str(), "%uch", &channelVal) == 1){targetChannel = channelVal;}
    if (targetChannel){
      for (std::set<size_t>::iterator it = trackList.begin(); it != trackList.end(); it++){
        if (!trackType.size() || M.getType(*it) == trackType || M.getCodec(*it) == trackType){
          if (M.getChannels(*it) == targetChannel){result.insert(*it);}
        }
      }
      return result;
    }
  }

  // Video-specific matches
  if (!trackType.size() || trackType == "video"){
    //All tracks containing bframes
    if (trackLow == "bframes"){
      for (std::set<size_t>::iterator it = trackList.begin(); it != trackList.end(); it++){
        if (!trackType.size() || M.getType(*it) == trackType || M.getCodec(*it) == trackType){
          if (M.hasBFrames(*it)){result.insert(*it);}
        }
      }
      return result;
    }
    //Highest resolution
    if (trackLow == "highres" || trackLow == "bestres" || trackLow == "maxres"){
      //Select highest resolution track of this type
      size_t currVal = INVALID_TRACK_ID;
      uint64_t currRes = 0;
      for (std::set<size_t>::iterator it = trackList.begin(); it != trackList.end(); it++){
        if (!trackType.size() || M.getType(*it) == trackType || M.getCodec(*it) == trackType){
          uint64_t trackRes = M.getWidth(*it)*M.getHeight(*it);
          if (currRes < trackRes){
            currVal = *it;
            currRes = trackRes;
          }
        }
      }
      if (currVal != INVALID_TRACK_ID){result.insert(currVal);}
      return result;
    }
    if (trackLow == "lowres" || trackLow == "worstres" || trackLow == "minres"){
      //Select lowest resolution track of this type
      size_t currVal = INVALID_TRACK_ID;
      uint64_t currRes = INVALID_TRACK_ID;
      for (std::set<size_t>::iterator it = trackList.begin(); it != trackList.end(); it++){
        if (!trackType.size() || M.getType(*it) == trackType || M.getCodec(*it) == trackType){
          uint64_t trackRes = M.getWidth(*it)*M.getHeight(*it);
          if (currRes > trackRes){
            currVal = *it;
            currRes = trackRes;
          }
        }
      }
      if (currVal != INVALID_TRACK_ID){result.insert(currVal);}
      return result;
    }
    {
      unsigned int resX, resY;
      if (sscanf(trackLow.c_str(), "~%ux%u", &resX, &resY) == 2){
        //Select nearest resolution track of this type
        size_t currVal = INVALID_TRACK_ID;
        uint64_t currDist = 0;
        uint64_t targetArea = resX*resY;
        for (std::set<size_t>::iterator it = trackList.begin(); it != trackList.end(); it++){
          if (!trackType.size() || M.getType(*it) == trackType || M.getCodec(*it) == trackType){
            uint64_t trackArea = M.getWidth(*it)*M.getHeight(*it);
            if (currVal == INVALID_TRACK_ID || (trackArea >= targetArea && currDist > (trackArea-targetArea)) || (trackArea < targetArea && currDist > (targetArea-trackArea))){
              currVal = *it;
              currDist = (trackArea >= targetArea)?(trackArea-targetArea):(targetArea-trackArea);
            }
          }
        }
        if (currVal != INVALID_TRACK_ID){result.insert(currVal);}
        return result;
      }
    }
  }// video track specific

  // attempt to do language/codec matching
  // convert 2-character language codes into 3-character language codes
  if (trackLow.size() == 2){trackLow = Encodings::ISO639::twoToThree(trackLow);}
  for (std::set<size_t>::iterator it = trackList.begin(); it != trackList.end(); it++){
    if (!trackType.size() || M.getType(*it) == trackType || M.getCodec(*it) == trackType){
      std::string codecLow = M.getCodec(*it);
      Util::stringToLower(codecLow);
      if (M.getLang(*it) == trackLow || trackLow == codecLow){result.insert(*it);}
      if (!trackType.size() || trackType == "video"){
        unsigned int resX, resY;
        if (trackLow == "720p" && M.getWidth(*it) == 1280 && M.getHeight(*it) == 720){result.insert(*it);}
        if (trackLow == "1080p" && M.getWidth(*it) == 1920 && M.getHeight(*it) == 1080){result.insert(*it);}
        if (trackLow == "1440p" && M.getWidth(*it) == 2560 && M.getHeight(*it) == 1440){result.insert(*it);}
        if (trackLow == "2k" && M.getWidth(*it) == 2048 && M.getHeight(*it) == 1080){result.insert(*it);}
        if (trackLow == "4k" && M.getWidth(*it) == 3840 && M.getHeight(*it) == 2160){result.insert(*it);}
        if (trackLow == "5k" && M.getWidth(*it) == 5120 && M.getHeight(*it) == 2880){result.insert(*it);}
        if (trackLow == "8k" && M.getWidth(*it) == 7680 && M.getHeight(*it) == 4320){result.insert(*it);}
        //match "XxY" format
        if (sscanf(trackLow.c_str(), "%ux%u", &resX, &resY) == 2){
          if (M.getWidth(*it) == resX && M.getHeight(*it) == resY){result.insert(*it);}
        }
      }
    }
  }
  return result;
}

/// Selects a specific track or set of tracks of the given trackType, using trackVal to decide.
/// trackVal may be a comma-separated list of numbers, codecs or the word "all" or an asterisk.
/// Does not do any checks if the protocol supports these tracks, just selects blindly.
/// It is necessary to follow up with a selectDefaultTracks() call to strip unsupported
/// codecs/combinations.
std::set<size_t> Util::findTracks(const DTSC::Meta &M, const JSON::Value &capa, const std::string &trackType, const std::string &trackVal, const std::string &UA){
  std::set<size_t> validTracks = capa?getSupportedTracks(M, capa, "", UA):M.getValidTracks(true);
  return pickTracks(M, validTracks, trackType, trackVal);
}

std::set<size_t> Util::wouldSelect(const DTSC::Meta &M, const std::string &trackSelector,
                                   const JSON::Value &capa, const std::string &UA){
  std::map<std::string, std::string> parsedVariables;
  HTTP::parseVars(trackSelector, parsedVariables);

  return wouldSelect(M, parsedVariables, capa, UA);
}

std::set<size_t> Util::getSupportedTracks(const DTSC::Meta &M, const JSON::Value &capa,
                                          const std::string &type, const std::string &UA){
  std::set<size_t> validTracks = M.getValidTracks(true);
  uint64_t maxLastMs = 0;
  std::set<size_t> toRemove;
  for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
    // Remove unrequested tracks
    if (type != "" && type != M.getType(*it)){
      toRemove.insert(*it);
      continue;
    }
    // Remove tracks for which we don't have codec support
    if (capa.isMember("codecs")){
      std::string codec = M.getCodec(*it);
      std::string type = M.getType(*it);
      bool found = false;
      jsonForEachConst(capa["codecs"], itb){
        jsonForEachConst(*itb, itc){
          jsonForEachConst(*itc, itd){
            const std::string &strRef = (*itd).asStringRef();
            bool byType = false;
            uint8_t shift = 0;
            if (strRef[shift] == '@'){
              byType = true;
              ++shift;
            }
            if (strRef[shift] == '+'){
              ++shift;
            }// Multiselect is ignored here, we only need to determine the types...
            if ((!byType && codec == strRef.substr(shift)) ||
                (byType && type == strRef.substr(shift)) || strRef.substr(shift) == "*"){
              // user-agent-check
              bool problems = false;
              if (capa.isMember("exceptions") && capa["exceptions"].isObject() && capa["exceptions"].size()){
                jsonForEachConst(capa["exceptions"], ex){
                  if (ex.key() == "codec:" + strRef.substr(shift)){
                    problems = !Util::checkException(*ex, UA);
                    break;
                  }
                }
              }
              if (problems){break;}
              found = true;
              break;
            }
          }
          if (found){break;}
        }
        if (found){break;}
      }
      if (!found){
        HIGH_MSG("Track %zu with codec %s not supported!", *it, codec.c_str());
        toRemove.insert(*it);
        continue;
      }
    }
    // Remove encrypted tracks if not supported
    if (M.getEncryption(*it) != ""){
      std::string encryptionType = M.getEncryption(*it);
      encryptionType = encryptionType.substr(0, encryptionType.find('/'));
      bool found = false;
      jsonForEachConst(capa["encryption"], itb){
        if (itb->asStringRef() == encryptionType){
          found = true;
          break;
        }
      }
      if (!found){
        INFO_MSG("Track %zu with encryption type %s not supported!", *it, encryptionType.c_str());
        toRemove.insert(*it);
      }
    }
    //not removing this track? Keep track of highest lastMs
    if (capa.isMember("maxdelay")){
      uint64_t lMs = M.getLastms(*it);
      if (lMs > maxLastMs){maxLastMs = lMs;}
    }
  }
  for (std::set<size_t>::iterator it = toRemove.begin(); it != toRemove.end(); it++){
    validTracks.erase(*it);
  }
  //if there is a max delay configured, remove tracks that are further behind than this
  if (capa.isMember("maxdelay")){
    uint64_t maxDelay = capa["maxdelay"].asInt();
    if (maxDelay > maxLastMs){maxDelay = maxLastMs;}
    toRemove.clear();
    for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
      if (M.getLastms(*it) < maxLastMs - maxDelay){toRemove.insert(*it);}
    }
    for (std::set<size_t>::iterator it = toRemove.begin(); it != toRemove.end(); it++){
      validTracks.erase(*it);
    }
  }
  return validTracks;
}

std::set<size_t> Util::wouldSelect(const DTSC::Meta &M, const std::map<std::string, std::string> &targetParams,
                                   const JSON::Value &capa, const std::string &UA, uint64_t seekTarget){
  std::set<size_t> result;

  /*LTS-START*/
  bool noSelAudio = false, noSelVideo = false, noSelSub = false, noSelMeta = false;
  // Then, select the tracks we've been asked to select.
  if (targetParams.count("audio") && targetParams.at("audio").size()){
    if (targetParams.at("audio") != "-1" && targetParams.at("audio") != "none"){
      std::set<size_t> tracks = Util::findTracks(M, capa, "audio", targetParams.at("audio"), UA);
      result.insert(tracks.begin(), tracks.end());
    }
    noSelAudio = true;
  }
  if (targetParams.count("video") && targetParams.at("video").size()){
    if (targetParams.at("video") != "-1" && targetParams.at("video") != "none"){
      std::set<size_t> tracks = Util::findTracks(M, capa, "video", targetParams.at("video"), UA);
      result.insert(tracks.begin(), tracks.end());
    }
    noSelVideo = true;
  }
  if (targetParams.count("subtitle") && targetParams.at("subtitle").size()){
    std::set<size_t> tracks = Util::findTracks(M, capa, "subtitle", targetParams.at("subtitle"), UA);
    result.insert(tracks.begin(), tracks.end());
    noSelSub = true;
  }
  if (targetParams.count("meta") && targetParams.at("meta").size()){
    std::set<size_t> tracks = Util::findTracks(M, capa, "meta", targetParams.at("meta"), UA);
    result.insert(tracks.begin(), tracks.end());
    noSelMeta = true;
  }
  /*LTS-END*/

  std::set<size_t> validTracks = M.getValidTracks(true);
  if (capa){validTracks = getSupportedTracks(M, capa);}

  // check which tracks don't actually exist
  std::set<size_t> toRemove;
  for (std::set<size_t>::iterator it = result.begin(); it != result.end(); it++){
    if (!validTracks.count(*it)){
      toRemove.insert(*it);
      continue;
    }
  }
  // remove those from selectedtracks
  for (std::set<size_t>::iterator it = toRemove.begin(); it != toRemove.end(); it++){
    result.erase(*it);
  }

  // loop through all codec combinations, count max simultaneous active
  unsigned int bestSoFar = 0;
  unsigned int bestSoFarCount = 0;
  unsigned int bestSoFarCountExtra = 0;
  unsigned int index = 0;
  bool allowBFrames = true;
  if (capa.isMember("methods")){
    jsonForEachConst(capa["methods"], mthd){
      if (mthd->isMember("nobframes") && (*mthd)["nobframes"]){
        allowBFrames = false;
        break;
      }
    }
  }

  /*LTS-START*/
  if (!capa.isMember("codecs")){
    for (std::set<size_t>::iterator trit = validTracks.begin(); trit != validTracks.end(); trit++){
        bool problems = false;
        if (capa.isMember("exceptions") && capa["exceptions"].isObject() &&
            capa["exceptions"].size()){
          jsonForEachConst(capa["exceptions"], ex){
            if (ex.key() == "codec:" + M.getCodec(*trit)){
              problems = !Util::checkException(*ex, UA);
              break;
            }
          }
        }
        if (!allowBFrames && M.hasBFrames(*trit)){problems = true;}
        if (problems){continue;}
        if (noSelAudio && M.getType(*trit) == "audio"){continue;}
        if (noSelVideo && M.getType(*trit) == "video"){continue;}
        if (noSelMeta && M.getType(*trit) == "meta"){continue;}
        if (noSelSub && (M.getType(*trit) == "subtitle" || M.getCodec(*trit) == "subtitle")){continue;}
        result.insert(*trit);
    }
    return result;
  }
  /*LTS-END*/

  jsonForEachConst(capa["codecs"], it){
    unsigned int selCounter = 0;
    unsigned int extraCounter = 0;
    if ((*it).size() > 0){
      jsonForEachConst((*it), itb){
        if ((*itb).size() > 0){
          jsonForEachConst(*itb, itc){
            const std::string &strRef = (*itc).asStringRef();
            bool byType = false;
            bool multiSel = false;
            uint8_t shift = 0;
            if (strRef[shift] == '@'){
              byType = true;
              ++shift;
            }
            if (strRef[shift] == '+'){
              multiSel = true;
              ++shift;
            }
            for (std::set<size_t>::iterator itd = result.begin(); itd != result.end(); itd++){
              if ((!byType && M.getCodec(*itd) == strRef.substr(shift)) ||
                  (byType && M.getType(*itd) == strRef.substr(shift)) || strRef.substr(shift) == "*"){
                // user-agent-check
                bool problems = false;
                if (capa.isMember("exceptions") && capa["exceptions"].isObject() &&
                    capa["exceptions"].size()){
                  jsonForEachConst(capa["exceptions"], ex){
                    if (ex.key() == "codec:" + strRef.substr(shift)){
                      problems = !Util::checkException(*ex, UA);
                      break;
                    }
                  }
                }
                if (problems){break;}
                selCounter++;
                extraCounter++;
                if (!multiSel){break;}
              }
            }
            for (std::set<size_t>::iterator itd = validTracks.begin(); itd != validTracks.end(); itd++){
              if ((!byType && M.getCodec(*itd) == strRef.substr(shift)) ||
                  (byType && M.getType(*itd) == strRef.substr(shift)) || strRef.substr(shift) == "*"){
                // user-agent-check
                bool problems = false;
                if (capa.isMember("exceptions") && capa["exceptions"].isObject() &&
                    capa["exceptions"].size()){
                  jsonForEachConst(capa["exceptions"], ex){
                    if (ex.key() == "codec:" + strRef.substr(shift)){
                      problems = !Util::checkException(*ex, UA);
                      break;
                    }
                  }
                }
                if (problems){break;}
                extraCounter++;
                if (!multiSel){break;}
              }
            }
          }
        }
      }
      if (selCounter == result.size()){
        if (selCounter > bestSoFarCount || (selCounter == bestSoFarCount && extraCounter > bestSoFarCountExtra)){
          bestSoFarCount = selCounter;
          bestSoFarCountExtra = extraCounter;
          bestSoFar = index;
          HIGH_MSG("Matched %u: %s", selCounter, (*it).toString().c_str());
        }
      }else{
        VERYHIGH_MSG("Not a match for currently selected tracks: %s", it->toString().c_str());
      }
    }
    index++;
  }

  HIGH_MSG("Trying to fill: %s", capa["codecs"][bestSoFar].toString().c_str());

  std::list<size_t> srtTrks;
  Util::sortTracks(validTracks, M, Util::defaultTrackSortOrder, srtTrks);

  // try to fill as many codecs simultaneously as possible
  if (capa["codecs"][bestSoFar].size() > 0){
    jsonForEachConst(capa["codecs"][bestSoFar], itb){
      if (itb->size() && validTracks.size()){
        bool found = false;
        bool multiFind = false;
        jsonForEachConst((*itb), itc){
          const std::string &strRef = (*itc).asStringRef();
          bool byType = false;
          uint8_t shift = 0;
          if (strRef[shift] == '@'){
            byType = true;
            ++shift;
          }
          if (strRef[shift] == '+'){
            multiFind = true;
            ++shift;
          }
          for (std::set<size_t>::iterator itd = result.begin(); itd != result.end(); itd++){
            if ((!byType && M.getCodec(*itd) == strRef.substr(shift)) ||
                (byType && M.getType(*itd) == strRef.substr(shift)) || strRef.substr(shift) == "*"){
              // user-agent-check
              bool problems = false;
              if (capa.isMember("exceptions") && capa["exceptions"].isObject() && capa["exceptions"].size()){
                jsonForEachConst(capa["exceptions"], ex){
                  if (ex.key() == "codec:" + strRef.substr(shift)){
                    problems = !Util::checkException(*ex, UA);
                    break;
                  }
                }
              }
              if (problems){break;}
              found = true;
              break;
            }
          }
        }
        if (!found || multiFind){
          jsonForEachConst((*itb), itc){
            const std::string &strRef = (*itc).asStringRef();
            bool byType = false;
            bool multiSel = false;
            uint8_t shift = 0;
            if (strRef[shift] == '@'){
              byType = true;
              ++shift;
            }
            if (strRef[shift] == '+'){
              multiSel = true;
              ++shift;
            }
            if (found && !multiSel){continue;}

            for (std::list<size_t>::iterator trit = srtTrks.begin();
                 trit != srtTrks.end(); trit++){
              if ((!byType && M.getCodec(*trit) == strRef.substr(shift)) ||
                  (byType && M.getType(*trit) == strRef.substr(shift)) || strRef.substr(shift) == "*"){
                // user-agent-check
                bool problems = false;
                if (capa.isMember("exceptions") && capa["exceptions"].isObject() &&
                    capa["exceptions"].size()){
                  jsonForEachConst(capa["exceptions"], ex){
                    if (ex.key() == "codec:" + strRef.substr(shift)){
                      problems = !Util::checkException(*ex, UA);
                      break;
                    }
                  }
                }
                if (!allowBFrames && M.hasBFrames(*trit)){problems = true;}
                if (problems){break;}
                /*LTS-START*/
                if (noSelAudio && M.getType(*trit) == "audio"){continue;}
                if (noSelVideo && M.getType(*trit) == "video"){continue;}
                if (noSelMeta && M.getType(*trit) == "meta"){continue;}
                if (noSelSub &&
                    (M.getType(*trit) == "subtitle" || M.getCodec(*trit) == "subtitle")){
                  continue;
                }
                /*LTS-END*/
                result.insert(*trit);
                found = true;
                if (!multiSel){break;}
              }
            }
          }
        }
      }
    }
  }

  if (Util::printDebugLevel >= DLVL_MEDIUM){
    // print the selected tracks
    std::stringstream selected;
    for (std::set<size_t>::iterator it = result.begin(); it != result.end(); it++){
      if (it != result.begin()){selected << ", ";}
      selected << *it;
    }
    MEDIUM_MSG("Would select tracks: %s (%zu)", selected.str().c_str(), result.size());
  }

  if (!result.size() && validTracks.size() && capa["codecs"][bestSoFar].size()){
    WARN_MSG("Would select no tracks (%zu total) for stream!", validTracks.size());
  }
  return result;
}

/// Sorts the given set of track IDs by the given sort order, according to the given metadata, and returns it by reference as the given list.
/// Will clear the list automatically if not empty.
void Util::sortTracks(std::set<size_t> & validTracks, const DTSC::Meta & M, Util::trackSortOrder sorting, std::list<size_t> & srtTrks){
  srtTrks.clear();
  if (sorting == TRKSORT_DEFAULT){
    if (M.getLive()){
      sorting = TRKSORT_ID_HTL;
    }else{
      sorting = TRKSORT_ID_LTH;
    }
  }
  if (!validTracks.size()){return;}
  for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); ++it){
    //The first element is always at the beginning of the list. Yeah. That makes sense.
    if (!srtTrks.size()){
      srtTrks.push_front(*it);
      continue;
    }
    if (sorting == TRKSORT_ID_LTH){
      //ID low to high needs no comparison, already sorted
      srtTrks.push_back(*it);
      continue;
    }else if (sorting == TRKSORT_ID_HTL){
      //ID high to low needs no comparison either, already sorted in reverse
      srtTrks.push_front(*it);
      continue;
    }
    bool inserted = false;
    for (std::list<size_t>::iterator lt = srtTrks.begin(); lt != srtTrks.end(); ++lt){
      if (sorting == TRKSORT_BPS_LTH){
        if (M.getBps(*it) <= M.getBps(*lt)){
          srtTrks.insert(lt, *it);
          inserted = true;
          break;
        }
      }else if (sorting == TRKSORT_BPS_HTL){
        if (M.getBps(*it) >= M.getBps(*lt)){
          srtTrks.insert(lt, *it);
          inserted = true;
          break;
        }
      }else if (sorting == TRKSORT_RES_LTH){
        if (M.getWidth(*it) * M.getHeight(*it) < M.getWidth(*lt) * M.getHeight(*lt) || M.getRate(*it) < M.getRate(*lt)){
          srtTrks.insert(lt, *it);
          inserted = true;
          break;
        }
      }else if (sorting == TRKSORT_RES_HTL){
        if (M.getWidth(*it) * M.getHeight(*it) > M.getWidth(*lt) * M.getHeight(*lt) || M.getRate(*it) > M.getRate(*lt)){
          srtTrks.insert(lt, *it);
          inserted = true;
          break;
        }
      }
    }
    //Insert at end of list if not inserted yet
    if (!inserted){srtTrks.push_back(*it);}
  }

}

