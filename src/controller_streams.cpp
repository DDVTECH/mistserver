#include <mist/procs.h>
#include <mist/config.h>
#include <mist/timing.h>
#include "controller_streams.h"
#include "controller_storage.h"
#include <sys/stat.h>

namespace Controller {

  std::map<std::string, int> lastBuffer; ///< Last moment of contact with all buffers.

  bool streamsEqual(JSON::Value & one, JSON::Value & two){
    if (one["channel"]["URL"] != two["channel"]["URL"]){
      return false;
    }
    if (one["preset"]["cmd"] != two["preset"]["cmd"]){
      return false;
    }
    return true;
  }

  void startStream(std::string name, JSON::Value & data){
    std::string URL = data["channel"]["URL"];
    std::string preset = data["preset"]["cmd"];
    std::string cmd1, cmd2, cmd3;
    if (URL.substr(0, 4) == "push"){
      std::string pusher = URL.substr(7);
      cmd2 = "MistBuffer -s " + name + " " + pusher;
      Util::Procs::Start(name, Util::getMyPath() + cmd2);
      Log("BUFF", "(re)starting stream buffer " + name + " for push data from " + pusher);
    }else{
      if (URL.substr(0, 1) == "/"){
        struct stat fileinfo;
        if (stat(URL.c_str(), &fileinfo) != 0 || S_ISDIR(fileinfo.st_mode)){
          Log("BUFF", "Warning for VoD stream " + name + "! File not found: " + URL);
          data["error"] = "Not found: " + URL;
          return;
        }
        cmd1 = "cat " + URL;
        data["error"] = "Available";
        return; //MistPlayer handles VoD
      }else{
        cmd1 = "ffmpeg -re -async 2 -i " + URL + " " + preset + " -f flv -";
        cmd2 = "MistFLV2DTSC";
      }
      cmd3 = "MistBuffer -s " + name;
      if (cmd2 != ""){
        Util::Procs::Start(name, cmd1, Util::getMyPath() + cmd2, Util::getMyPath() + cmd3);
        Log("BUFF", "(re)starting stream buffer " + name + " for ffmpeg data: " + cmd1);
      }else{
        Util::Procs::Start(name, cmd1, Util::getMyPath() + cmd3);
        Log("BUFF", "(re)starting stream buffer " + name + " using input file " + URL);
      }
    }
  }

  void CheckAllStreams(JSON::Value & data){
    long long int currTime = Util::epoch();
    for (JSON::ObjIter jit = data.ObjBegin(); jit != data.ObjEnd(); jit++){
      if ( !Util::Procs::isActive(jit->first)){
        startStream(jit->first, jit->second);
      }
      if (currTime - lastBuffer[jit->first] > 5){
        if (jit->second.isMember("error") && jit->second["error"].asString() != ""){
          jit->second["online"] = jit->second["error"];
        }else{
          jit->second["online"] = 0;
        }
      }else{
        jit->second["online"] = 1;
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

  void CheckStreams(JSON::Value & in, JSON::Value & out){
    bool changed = false;
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
    for (JSON::ObjIter jit = out.ObjBegin(); jit != out.ObjEnd(); jit++){
      if ( !in.isMember(jit->first)){
        Log("STRM", std::string("Deleted stream ") + jit->first);
        Util::Procs::Stop(jit->first);
      }
    }
    out = in;
  }

} //Controller namespace
