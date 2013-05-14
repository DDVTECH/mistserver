#include <mist/procs.h>
#include <mist/config.h>
#include <mist/timing.h>
#include "controller_streams.h"
#include "controller_storage.h"
#include <sys/stat.h>

///\brief Holds everything unique to the controller.
namespace Controller {

  std::map<std::string, int> lastBuffer; ///< Last moment of contact with all buffers.
  
  ///\brief Checks whether two streams are equal.
  ///\param one The first stream for the comparison.
  ///\param two The second stream for the comparison.
  ///\return True if the streams are equal, false otherwise.
  bool streamsEqual(JSON::Value & one, JSON::Value & two){
    if ( !one.isMember("source") || !two.isMember("source") || one["source"] != two["source"]){
      return false;
    }
    return true;
  }

  ///\brief Starts a single stream
  ///\param name The name of the stream
  ///\param data The corresponding configuration values.
  void startStream(std::string name, JSON::Value & data){
    data["online"] = (std::string)"Checking...";
    data.removeMember("error");
    std::string URL;
    if (data.isMember("channel") && data["channel"].isMember("URL")){
      URL = data["channel"]["URL"].asString();
    }
    if (data.isMember("source")){
      URL = data["source"].asString();
    }
    std::string cmd1, cmd2, cmd3;
    if (URL == ""){
      Log("STRM", "Error for stream " + name + "! Source parameter missing.");
      data["error"] = "Missing source parameter!";
      return;
    }
    if (URL.substr(0, 4) == "push"){
      std::string pusher = URL.substr(7);
      if (data.isMember("DVR") && data["DVR"].asInt() > 0){
        data["DVR"] = data["DVR"].asInt();
        cmd2 = "MistBuffer -t " + data["DVR"].asString() + " -s " + name + " " + pusher;
      }else{
        cmd2 = "MistBuffer -s " + name + " " + pusher;
      }
      Util::Procs::Start(name, Util::getMyPath() + cmd2);
      Log("BUFF", "(re)starting stream buffer " + name + " for push data from " + pusher);
    }else{
      if (URL.substr(0, 1) == "/"){
        struct stat fileinfo;
        if (stat(URL.c_str(), &fileinfo) != 0 || S_ISDIR(fileinfo.st_mode)){
          Log("BUFF", "Warning for VoD stream " + name + "! File not found: " + URL);
          data["error"] = "Not found: " + URL;
          data["online"] = 0;
          return;
        }
        cmd1 = "cat " + URL;
        if (Util::epoch() - lastBuffer[name] > 5){
          data["error"] = "Available";
          data["online"] = 2;
        }else{
          data["online"] = 1;
          data.removeMember("error");
        }
        return; //MistPlayer handles VoD
      }else{
        cmd1 = "ffmpeg -re -async 2 -i " + URL + " -f flv -";
        cmd2 = "MistFLV2DTSC";
      }
      if (data.isMember("DVR") && data["DVR"].asInt() > 0){
        data["DVR"] = data["DVR"].asInt();
        cmd3 = "MistBuffer -t " + data["DVR"].asString() + " -s " + name;
      }else{
        cmd3 = "MistBuffer -s " + name;
      }
      if (cmd2 != ""){
        Util::Procs::Start(name, cmd1, Util::getMyPath() + cmd2, Util::getMyPath() + cmd3);
        Log("BUFF", "(re)starting stream buffer " + name + " for ffmpeg data: " + cmd1);
      }else{
        Util::Procs::Start(name, cmd1, Util::getMyPath() + cmd3);
        Log("BUFF", "(re)starting stream buffer " + name + " using input file " + URL);
      }
    }
  }

  ///\brief Checks all streams, restoring if needed.
  ///\param data The stream configuration for the server.
  void CheckAllStreams(JSON::Value & data){
    long long int currTime = Util::epoch();
    for (JSON::ObjIter jit = data.ObjBegin(); jit != data.ObjEnd(); jit++){
      if ( !Util::Procs::isActive(jit->first)){
        startStream(jit->first, jit->second);
      }
      if (currTime - lastBuffer[jit->first] > 5){
        if (jit->second.isMember("source") && jit->second["source"].asString().substr(0, 1) == "/" && jit->second.isMember("error")
            && jit->second["error"].asString() == "Available"){
          jit->second["online"] = 2;
        }else{
          if (jit->second.isMember("error") && jit->second["error"].asString() == "Available"){
            jit->second.removeMember("error");
          }
          jit->second["online"] = 0;
        }
      }else{
        // assume all is fine
        jit->second.removeMember("error");
        jit->second["online"] = 1;
        // check if source is valid
        if (jit->second.isMember("live") && !jit->second.isMember("meta") || !jit->second["meta"]){
          jit->second["online"] = 0;
          jit->second["error"] = "No (valid) source connected";
        }else{
          // for live streams, keep track of activity
          if (jit->second["meta"].isMember("live")){
            if (jit->second["meta"]["lastms"] != jit->second["lastms"]){
              jit->second["lastms"] = jit->second["meta"]["lastms"];
              jit->second["last_active"] = currTime;
            }
            // mark stream as offline if no activity for 5 seconds
            if (jit->second.isMember("last_active") && jit->second["last_active"].asInt() < currTime - 5){
              jit->second["online"] = 0;
              jit->second["error"] = "No (valid) source connected";
            }
          }
        }
      }
    }
    static JSON::Value strlist;
    bool changed = false;
    if (strlist["config"] != Storage["config"]){
      strlist["config"] = Storage["config"];
      changed = true;
    }
    if (strlist["streams"] != Storage["streams"]){
      strlist["streams"] = Storage["streams"];
      changed = true;
    }
    if (changed){
      WriteFile("/tmp/mist/streamlist", strlist.toString());
    }
  }

  ///\brief Parse a given stream configuration.
  ///\param in The requested configuration.
  ///\param out The new configuration after parsing.
  void CheckStreams(JSON::Value & in, JSON::Value & out){
    bool changed = false;

    //check for new streams and updates
    for (JSON::ObjIter jit = in.ObjBegin(); jit != in.ObjEnd(); jit++){
      if (out.isMember(jit->first)){
        if ( !streamsEqual(jit->second, out[jit->first])){
          Log("STRM", std::string("Updated stream ") + jit->first);
          Util::Procs::Stop(jit->first);
          startStream(jit->first, jit->second);
        }
      }else{
        Log("STRM", std::string("New stream ") + jit->first);
        startStream(jit->first, jit->second);
      }
    }

    //check for deleted streams
    for (JSON::ObjIter jit = out.ObjBegin(); jit != out.ObjEnd(); jit++){
      if ( !in.isMember(jit->first)){
        Log("STRM", std::string("Deleted stream ") + jit->first);
        Util::Procs::Stop(jit->first);
      }
    }

    //update old-style configurations to new-style
    for (JSON::ObjIter jit = in.ObjBegin(); jit != in.ObjEnd(); jit++){
      if (jit->second.isMember("channel")){
        if ( !jit->second.isMember("source")){
          jit->second["source"] = jit->second["channel"]["URL"];
        }
        jit->second.removeMember("channel");
      }
      if (jit->second.isMember("preset")){
        jit->second.removeMember("preset");
      }
    }

    out = in;
  }

} //Controller namespace
