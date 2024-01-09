#include "controller_capabilities.h"
#include "controller_limits.h" /*LTS*/
#include "controller_statistics.h"
#include "controller_storage.h"
#include "controller_streams.h"
#include <mist/timing.h>
#include <map>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/dtsc.h>
#include <mist/procs.h>
#include <mist/shared_memory.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/triggers.h> //LTS
#include <sys/stat.h>

///\brief Holds everything unique to the controller.
namespace Controller{
  std::map<std::string, pid_t> inputProcesses;

  /// Internal list of currently active processes
  class procInfo{
    public:
    JSON::Value stats;
    std::string source;
    std::string proc;
    std::string sink;
    uint64_t lastupdate;
    JSON::Value logs;
  };
  std::map<pid_t, procInfo> activeProcs;

  void procLogMessage(uint64_t id, const JSON::Value & msg){
    JSON::Value &log = activeProcs[id].logs;
    log.append(msg);
    log.shrink(25);
  }

  bool isProcActive(uint64_t id){
    return activeProcs.count(id);
  }


  void getProcsForStream(const std::string & stream, JSON::Value & returnedProcList){
    std::set<pid_t> wipeList;
    for (std::map<pid_t, procInfo>::iterator it = activeProcs.begin(); it != activeProcs.end(); ++it){
      if (!stream.size() || stream == it->second.sink || stream == it->second.source){
        JSON::Value & thisProc = returnedProcList[JSON::Value(it->first).asString()];
        thisProc = it->second.stats;
        thisProc["source"] = it->second.source;
        thisProc["sink"] = it->second.sink;
        thisProc["process"] = it->second.proc;
        thisProc["logs"] = it->second.logs;
        if (!Util::Procs::isRunning(it->first)){
          thisProc["terminated"] = true;
          wipeList.insert(it->first);
        }
      }
    }
    while (wipeList.size()){
      activeProcs.erase(*wipeList.begin());
      wipeList.erase(wipeList.begin());
    }
  }

  void setProcStatus(uint64_t id, const std::string & proc, const std::string & source, const std::string & sink, const JSON::Value & status){
    procInfo & prc = activeProcs[id];
    prc.lastupdate = Util::bootSecs();
    prc.stats.extend(status);
    if (!prc.proc.size() && sink.size() && source.size() && proc.size()){
      prc.sink = sink;
      prc.source = source;
      prc.proc = proc;
    }
  }

  ///\brief Checks whether two streams are equal.
  ///\param one The first stream for the comparison.
  ///\param two The second stream for the comparison.
  ///\return True if the streams are equal, false otherwise.
  bool streamsEqual(JSON::Value &one, JSON::Value &two){
    if (one.isMember("source") != two.isMember("source") || one["source"] != two["source"]){
      return false;
    }

    /// \todo Change this to use capabilities["inputs"] and only compare required/optional
    /// parameters. \todo Maybe change this to check for correct source and/or required parameters.

    // temporary: compare the two JSON::Value objects.
    return one == two;

    // nothing different? return true by default
    // return true;
  }

  ///\brief Checks the validity of a stream, updates internal stream status.
  ///\param name The name of the stream
  ///\param data The corresponding configuration values.
  void checkStream(std::string name, JSON::Value &data){
    if (!data.isMember("name")){data["name"] = name;}
    std::string prevState = data["error"].asStringRef();
    data["online"] = (std::string) "Checking...";
    data.removeMember("error");
    data.removeNullMembers();
    switch (Util::getStreamStatus(name)){
    case STRMSTAT_OFF:
      // Do nothing
      break;
    case STRMSTAT_INIT:
      data["online"] = 2;
      data["error"] = "Initializing...";
      return;
    case STRMSTAT_BOOT:
      data["online"] = 2;
      data["error"] = "Loading...";
      return;
    case STRMSTAT_WAIT:
      data["online"] = 2;
      data["error"] = "Waiting for data...";
      return;
    case STRMSTAT_READY: data["online"] = 1; return;
    case STRMSTAT_SHUTDOWN:
      data["online"] = 2;
      data["error"] = "Shutting down...";
      return;
    default:
      // Unknown state?
      data["error"] = "Unrecognized stream state";
      break;
    }
    data["online"] = 0;
    std::string URL;
    if (data.isMember("channel") && data["channel"].isMember("URL")){
      URL = data["channel"]["URL"].asString();
    }
    if (data.isMember("source")){URL = data["source"].asString();}
    if (!URL.size()){
      data["error"] = "Stream offline: Missing source parameter!";
      if (data["error"].asStringRef() != prevState){
        Log("STRM", "Error for stream " + name + "! Source parameter missing.");
      }
      return;
    }
    // Old style always on
    if (data.isMember("udpport") && data["udpport"].asStringRef().size() &&
        (!inputProcesses.count(name) || !Util::Procs::isRunning(inputProcesses[name]))){
      const std::string &udpPort = data["udpport"].asStringRef();
      const std::string &multicast = data["multicastinterface"].asStringRef();
      URL = "tsudp://" + udpPort;
      if (multicast.size()){URL.append("/" + multicast);}
      //  False: start TS input
      INFO_MSG("No TS input for stream %s, starting it: %s", name.c_str(), URL.c_str());
      std::deque<std::string> command;
      command.push_back(Util::getMyPath() + "MistInTS");
      command.push_back("-s");
      command.push_back(name);
      command.push_back(URL);
      int stdIn = 0;
      int stdOut = 1;
      int stdErr = 2;
      pid_t program = Util::Procs::StartPiped(command, &stdIn, &stdOut, &stdErr);
      if (program){inputProcesses[name] = program;}
    }
    // new style always on
    if (data.isMember("always_on") && data["always_on"].asBool() &&
        (!inputProcesses.count(name) || !Util::Procs::isRunning(inputProcesses[name]))){
      INFO_MSG("Starting always-on input %s: %s", name.c_str(), URL.c_str());
      std::map<std::string, std::string> empty_overrides;
      pid_t program = 0;
      Util::startInput(name, URL, true, false, empty_overrides, &program);
      if (program){inputProcesses[name] = program;}
    }
    // non-VoD stream
    if (URL.substr(0, 1) != "/"){return;}
    Util::streamVariables(URL, name, "");
    // VoD-style stream
    struct stat fileinfo;
    if (stat(URL.c_str(), &fileinfo) != 0){
      data["error"] = "Stream offline: Not found: " + URL;
      if (data["error"].asStringRef() != prevState){
        Log("BUFF", "Warning for VoD stream " + name + "! File not found: " + URL);
      }
      return;
    }
    if (!data.isMember("error")){data["error"] = "Available";}
    data["online"] = 2;
    return;
  }

  ///\brief Checks all streams, restoring if needed.
  ///\param data The stream configuration for the server.
  ///\returns True if the server status changed
  bool CheckAllStreams(JSON::Value &data){
    jsonForEach(data, jit){checkStream(jit.key(), (*jit));}

    // check for changes in config or streams
    static JSON::Value strlist;
    if (strlist["config"] != Storage["config"] || strlist["streams"] != Storage["streams"]){
      strlist["config"] = Storage["config"];
      strlist["streams"] = Storage["streams"];
      return true;
    }
    return false;
  }

  ///
  /// \triggers
  /// The `"STREAM_ADD"` trigger is stream-specific, and is ran whenever a new stream is added to
  /// the server configuration. If cancelled, the stream is not added. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// configuration in JSON format
  /// ~~~~~~~~~~~~~~~
  /// The `"STREAM_CONFIG"` trigger is stream-specific, and is ran whenever a stream's configuration
  /// is changed. If cancelled, the configuration is not changed. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// configuration in JSON format
  /// ~~~~~~~~~~~~~~~
  ///
  void AddStreams(JSON::Value &in, JSON::Value &out){
    // check for new streams and updates
    jsonForEach(in, jit){
      if (out.isMember(jit.key())){
        if (!streamsEqual((*jit), out[jit.key()])){
          /*LTS-START*/
          if (Triggers::shouldTrigger("STREAM_CONFIG")){
            std::string payload = jit.key() + "\n" + jit->toString();
            if (!Triggers::doTrigger("STREAM_CONFIG", payload, jit.key())){continue;}
          }
          /*LTS-END*/
          out[jit.key()] = (*jit);
          out[jit.key()].removeNullMembers();
          out[jit.key()]["name"] = jit.key();
          checkParameters(out[jit.key()]);
          Log("STRM", std::string("Updated stream ") + jit.key());
        }
      }else{
        std::string checked = jit.key();
        Util::sanitizeName(checked);
        if (checked != jit.key() || !checked.size()){
          if (!checked.size()){
            FAIL_MSG("Invalid stream name '%s'", jit.key().c_str());
          }else{
            FAIL_MSG("Invalid stream name '%s'. Suggested alternative: '%s'", jit.key().c_str(),
                     checked.c_str());
          }
          continue;
        }
        /*LTS-START*/
        if (Triggers::shouldTrigger("STREAM_ADD")){
          std::string payload = jit.key() + "\n" + jit->toString();
          if (!Triggers::doTrigger("STREAM_ADD", payload, jit.key())){continue;}
        }
        /*LTS-END*/
        out[jit.key()] = (*jit);
        out[jit.key()].removeNullMembers();
        out[jit.key()]["name"] = jit.key();
        checkParameters(out[jit.key()]);
        Log("STRM", std::string("New stream ") + jit.key());
      }
      Controller::writeStream(jit.key(), out[jit.key()]);
    }
  }

  ///\brief Parse a given stream configuration.
  ///\param in The requested configuration.
  ///\param out The new configuration after parsing.
  ///
  /// \api
  /// `"streams"` requests take the form of:
  /// ~~~~~~~~~~~~~~~{.js}
  ///{
  ///   "streamname_here":{//name of the stream
  ///     "source": "/mnt/media/a.dtsc" //full path to a VoD file, or "push://" followed by the IP
  ///     or hostname of the machine allowed to push live data. Empty means everyone is allowed to
  ///     push live data. "DVR": 30000 //optional. For live streams, indicates the requested minimum
  ///     size of the available DVR buffer in milliseconds.
  ///},
  ///   //the above structure repeated for all configured streams
  ///}
  /// ~~~~~~~~~~~~~~~
  /// and are responded to as:
  /// ~~~~~~~~~~~~~~~{.js}
  ///{
  ///   "streamname_here":{//name of the configured stream
  ///     "error": "Available", //error state, if any. "Available" is a special value for VoD
  ///     streams, indicating it has no current viewers (is not active), but is available for
  ///     activation. "h_meta": 1398113185, //unix time the stream header (if any) was last
  ///     processed for metadata "l_meta": 1398115447, //unix time the stream itself was last
  ///     processed for metadata "meta":{//available metadata for this stream, if any
  ///       "format": "dtsc", //detected media source format
  ///       "tracks":{//list of tracks in this stream
  ///         "audio_AAC_2ch_48000hz_2":{//human-readable track name
  ///           "bps": 16043,
  ///           "channels": 2,
  ///           "codec": "AAC",
  ///           "firstms": 0,
  ///           "init": "\u0011Vå\u0000",
  ///           "lastms": 596480,
  ///           "rate": 48000,
  ///           "size": 16,
  ///           "trackid": 2,
  ///           "type": "audio"
  ///},
  ///         //the above structure repeated for all tracks
  ///},
  ///       "vod": 1 //indicates VoD stream, or "live" to indicated live stream.
  ///},
  ///     "name": "a", //the stream name, guaranteed to be equal to the object name.
  ///     "online": 2, //online state. 0 = error, 1 = active, 2 = inactive.
  ///     "source": "/home/thulinma/a.dtsc" //source for this stream, as configured.
  ///},
  ///   //the above structure repeated for all configured streams
  ///}
  /// ~~~~~~~~~~~~~~~
  /// Through this request, ALL streams must always be configured. To remove a stream, simply leave
  /// it out of the request. To add a stream, simply add it to the request. To edit a stream, simply
  /// edit it in the request. The LTS edition has additional requests that allow per-stream changing
  /// of the configuration.
  void CheckStreams(JSON::Value &in, JSON::Value &out){
    // check for new streams and updates
    AddStreams(in, out);

    // check for deleted streams
    std::set<std::string> toDelete;
    jsonForEach(out, jit){
      if (!in.isMember(jit.key())){toDelete.insert(jit.key());}
    }
    // actually delete the streams
    while (toDelete.size() > 0){
      std::string deleting = *(toDelete.begin());
      deleteStream(deleting, out);
      toDelete.erase(deleting);
    }

    // update old-style configurations to new-style
    jsonForEach(in, jit){
      if (jit->isMember("channel")){
        if (!jit->isMember("source")){(*jit)["source"] = (*jit)["channel"]["URL"];}
        jit->removeMember("channel");
      }
      if (jit->isMember("preset")){jit->removeMember("preset");}
    }
  }

  /// Deletes the stream (name) from the config (out), optionally also deleting the VoD source file if sourceFileToo is true.
  int deleteStream(const std::string &name, JSON::Value &out, bool sourceFileToo){
    int ret = 0;
    if (sourceFileToo){
      std::string cleaned = name;
      Util::sanitizeName(cleaned);
      std::string strmSource;
      if (Util::getStreamStatus(cleaned) != STRMSTAT_OFF){
        DTSC::Meta M(cleaned, false, false);
        if (M && M.getSource().size()){strmSource = M.getSource();}
      }
      if (!strmSource.size()){
        std::string smp = cleaned.substr(0, cleaned.find_first_of("+ "));
        if (out.isMember(smp) && out[smp].isMember("source")){
          strmSource = out[smp]["source"].asStringRef();
        }
      }
      bool noFile = false;
      if (strmSource.size()){
        std::string prevInput;
        while (true){
          std::string oldSrc = strmSource;
          JSON::Value inputCapa = Util::getInputBySource(oldSrc, true);
          if (inputCapa["name"].asStringRef() == prevInput){break;}
          prevInput = inputCapa["name"].asStringRef();
          strmSource = inputCapa["source_file"].asStringRef();
          if (!strmSource.size()){
            noFile = true;
            break;
          }
          Util::streamVariables(strmSource, cleaned, oldSrc);
        }
      }
      if (noFile){
        WARN_MSG("Not deleting source for stream %s, since the stream does not have an unambiguous "
                 "source file.",
                 cleaned.c_str());
      }else{
        Util::streamVariables(strmSource, cleaned);
        if (!strmSource.size()){
          FAIL_MSG("Could not delete source for stream %s: unable to detect stream source URI "
                   "using any method",
                   cleaned.c_str());
        }else{
          if (unlink(strmSource.c_str())){
            FAIL_MSG("Could not delete source %s for %s: %s (%d)", strmSource.c_str(),
                     cleaned.c_str(), strerror(errno), errno);
          }else{
            ++ret;
            Log("STRM", "Deleting source file for stream " + cleaned + ": " + strmSource);
            // Delete dtsh, ignore failures
            if (!unlink((strmSource + ".dtsh").c_str())){++ret;}
          }
        }
      }
    }
    if (!out.isMember(name)){return ret;}
    /*LTS-START*/
    if (Triggers::shouldTrigger("STREAM_REMOVE")){
      if (!Triggers::doTrigger("STREAM_REMOVE", name, name)){return ret;}
    }
    /*LTS-END*/
    Log("STRM", "Deleted stream " + name);
    out.removeMember(name);
    Controller::writeStream(name, JSON::Value()); // Null JSON value = delete
    ++ret;
    ret *= -1;
    if (inputProcesses.count(name)){
      pid_t procId = inputProcesses[name];
      if (Util::Procs::isRunning(procId)){Util::Procs::Stop(procId);}
      inputProcesses.erase(name);
    }
    return ret;
  }

  void checkParameters(JSON::Value &streamObj){
    JSON::Value in = Util::getInputBySource(streamObj["source"].asStringRef(), true);
    if (in){
      jsonForEach(in["hardcoded"], it){streamObj[it.key()] = *it;}
    }
  }

}// namespace Controller
