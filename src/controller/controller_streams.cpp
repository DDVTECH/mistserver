#include <mist/procs.h>
#include <mist/config.h>
#include <mist/timing.h>
#include <mist/stream.h>
#include <mist/dtsc.h>
#include <mist/defines.h>
#include <mist/shared_memory.h>
#include "controller_streams.h"
#include "controller_capabilities.h"
#include "controller_storage.h"
#include "controller_statistics.h"
#include <sys/stat.h>
#include <map>

///\brief Holds everything unique to the controller.
namespace Controller {

  ///\brief Checks whether two streams are equal.
  ///\param one The first stream for the comparison.
  ///\param two The second stream for the comparison.
  ///\return True if the streams are equal, false otherwise.
  bool streamsEqual(JSON::Value & one, JSON::Value & two){
    if (one.isMember("source") != two.isMember("source") || one["source"] != two["source"]){
      return false;
    }
    
    /// \todo Change this to use capabilities["inputs"] and only compare required/optional parameters.
    /// \todo Maybe change this to check for correct source and/or required parameters.
    
    //temporary: compare the two JSON::Value objects.
    return one==two;
    
    //nothing different? return true by default
    //return true;
  }

  ///\brief Checks the validity of a stream, updates internal stream status.
  ///\param name The name of the stream
  ///\param data The corresponding configuration values.
  void checkStream(std::string name, JSON::Value & data){
    std::string prevState = data["error"].asStringRef();
    data["online"] = (std::string)"Checking...";
    data.removeMember("error");
    std::string URL;
    if (data.isMember("channel") && data["channel"].isMember("URL")){
      URL = data["channel"]["URL"].asString();
    }
    if (data.isMember("source")){
      URL = data["source"].asString();
    }
    if (URL == ""){
      data["error"] = "Stream offline: Missing source parameter!";
      if (data["error"].asStringRef() != prevState){
        Log("STRM", "Error for stream " + name + "! Source parameter missing.");
      }
      return;
    }
    if (URL.substr(0, 1) != "/"){
      //push-style stream
      return;
    }
    if (URL.substr(0, 1) == "/"){
      //vod-style stream
      data.removeMember("error");
      struct stat fileinfo;
      if (stat(URL.c_str(), &fileinfo) != 0 || S_ISDIR(fileinfo.st_mode)){
        data["error"] = "Stream offline: Not found: " + URL;
        if (data["error"].asStringRef() != prevState){
          Log("BUFF", "Warning for VoD stream " + name + "! File not found: " + URL);
        }
        data["online"] = 0;
        return;
      }
      if (!hasViewers(name)){
        if ( !data.isMember("error")){
          data["error"] = "Available";
        }
        data["online"] = 2;
      }else{
        data["online"] = 1;
      }
      return;
    }
    //not recognized
    data["error"] = "Invalid source format";
    if (data["error"].asStringRef() != prevState){
      Log("STRM", "Invalid source format for stream " + name + "!");
    }
    return;
  }

  ///\brief Checks all streams, restoring if needed.
  ///\param data The stream configuration for the server.
  ///\returns True if the server status changed
  bool CheckAllStreams(JSON::Value & data){
    long long int currTime = Util::epoch();
    jsonForEach(data, jit) {
      checkStream(jit.key(), (*jit));
      if (!jit->isMember("name")){
        (*jit)["name"] = jit.key();
      }
      if (!hasViewers(jit.key())){
        if (jit->isMember("source") && (*jit)["source"].asString().substr(0, 1) == "/" && jit->isMember("error")
            && (*jit)["error"].asString().substr(0,15) != "Stream offline:"){
          (*jit)["online"] = 2;
        }else{
          if (jit->isMember("error") && (*jit)["error"].asString() == "Available"){
            jit->removeMember("error");
          }
          (*jit)["online"] = 0;
        }
      }else{
        // assume all is fine
        jit->removeMember("error");
        (*jit)["online"] = 1;
      }
    }

    //check for changes in config or streams
    static JSON::Value strlist;
    if (strlist["config"] != Storage["config"] || strlist["streams"] != Storage["streams"]){
      strlist["config"] = Storage["config"];
      strlist["streams"] = Storage["streams"];
      return true;
    }
    return false;
  }
  
  void AddStreams(JSON::Value & in, JSON::Value & out){
    //check for new streams and updates
    jsonForEach(in, jit) {
      if (out.isMember(jit.key())){
        if ( !streamsEqual((*jit), out[jit.key()])){
          out[jit.key()] = (*jit);
          out[jit.key()]["name"] = jit.key();
          Log("STRM", std::string("Updated stream ") + jit.key());
        }
      }else{
        out[jit.key()] = (*jit);
        out[jit.key()]["name"] = jit.key();
        Log("STRM", std::string("New stream ") + jit.key());
      }
    }
  }

  ///\brief Parse a given stream configuration.
  ///\param in The requested configuration.
  ///\param out The new configuration after parsing.
  ///
  /// \api
  /// `"streams"` requests take the form of:
  /// ~~~~~~~~~~~~~~~{.js}
  /// {
  ///   "streamname_here": { //name of the stream
  ///     "source": "/mnt/media/a.dtsc" //full path to a VoD file, or "push://" followed by the IP or hostname of the machine allowed to push live data. Empty means everyone is allowed to push live data.
  ///     "DVR": 30000 //optional. For live streams, indicates the requested minimum size of the available DVR buffer in milliseconds.
  ///   },
  ///   //the above structure repeated for all configured streams
  /// }
  /// ~~~~~~~~~~~~~~~
  /// and are responded to as:
  /// ~~~~~~~~~~~~~~~{.js}
  /// {
  ///   "streamname_here": { //name of the configured stream
  ///     "error": "Available", //error state, if any. "Available" is a special value for VoD streams, indicating it has no current viewers (is not active), but is available for activation.
  ///     "h_meta": 1398113185, //unix time the stream header (if any) was last processed for metadata
  ///     "l_meta": 1398115447, //unix time the stream itself was last processed for metadata
  ///     "meta": { //available metadata for this stream, if any
  ///       "format": "dtsc", //detected media source format
  ///       "tracks": { //list of tracks in this stream
  ///         "audio_AAC_2ch_48000hz_2": {//human-readable track name
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
  ///         },
  ///         //the above structure repeated for all tracks
  ///       },
  ///       "vod": 1 //indicates VoD stream, or "live" to indicated live stream.
  ///     },
  ///     "name": "a", //the stream name, guaranteed to be equal to the object name.
  ///     "online": 2, //online state. 0 = error, 1 = active, 2 = inactive.
  ///     "source": "/home/thulinma/a.dtsc" //source for this stream, as configured.
  ///   },
  ///   //the above structure repeated for all configured streams
  /// }
  /// ~~~~~~~~~~~~~~~
  /// Through this request, ALL streams must always be configured. To remove a stream, simply leave it out of the request. To add a stream, simply add it to the request. To edit a stream, simply edit it in the request. The LTS edition has additional requests that allow per-stream changing of the configuration.
  void CheckStreams(JSON::Value & in, JSON::Value & out){
    //check for new streams and updates
    AddStreams(in, out);

    //check for deleted streams
    std::set<std::string> toDelete;
    jsonForEach(out, jit) {
      if ( !in.isMember(jit.key())){
        toDelete.insert(jit.key());
        Log("STRM", std::string("Deleted stream ") + jit.key());
      }
    }
    //actually delete the streams
    while (toDelete.size() > 0){
      std::string deleting = *(toDelete.begin());
      out.removeMember(deleting);
      toDelete.erase(deleting);
    }

    //update old-style configurations to new-style
    jsonForEach(in, jit) {
      if (jit->isMember("channel")){
        if ( !jit->isMember("source")){
          (*jit)["source"] = (*jit)["channel"]["URL"];
        }
        jit->removeMember("channel");
      }
      if (jit->isMember("preset")){
        jit->removeMember("preset");
      }
    }

  }

} //Controller namespace
