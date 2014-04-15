#include <mist/procs.h>
#include <mist/config.h>
#include <mist/timing.h>
#include <mist/stream.h>
#include <mist/dtsc.h>
#include <mist/defines.h>
#include <mist/shared_memory.h>
#include "controller_streams.h"
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
    if (one.isMember("DVR") != two.isMember("DVR") || (one.isMember("DVR") && one["DVR"] != two["DVR"])){
      return false;
    }
    if (one.isMember("cut") != two.isMember("cut") || (one.isMember("cut") && one["cut"] != two["cut"])){
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
    if (URL == ""){
      Log("STRM", "Error for stream " + name + "! Source parameter missing.");
      data["error"] = "Stream offline: Missing source parameter!";
      return;
    }
    if (URL.substr(0, 4) == "push"){
      if (hasViewers(name)){
        data["meta"].null();
        IPC::sharedPage streamIndex(name,0,false,false);
        if (!streamIndex.mapped){
          return;
        }
        unsigned int i = 0;
        JSON::fromDTMI((const unsigned char*)streamIndex.mapped + 8, streamIndex.len - 8, i, data["meta"]);
        if (data["meta"].isMember("tracks") && data["meta"]["tracks"].size()){
          for(JSON::ObjIter trackIt = data["meta"]["tracks"].ObjBegin(); trackIt != data["meta"]["tracks"].ObjEnd(); trackIt++){
            trackIt->second.removeMember("fragments");
            trackIt->second.removeMember("keys");
            trackIt->second.removeMember("parts");
          }
        }
      }
    }else{
      if (URL.substr(0, 1) == "/"){
        data.removeMember("error");
        struct stat fileinfo;
        if (stat(URL.c_str(), &fileinfo) != 0 || S_ISDIR(fileinfo.st_mode)){
          Log("BUFF", "Warning for VoD stream " + name + "! File not found: " + URL);
          data["error"] = "Stream offline: Not found: " + URL;
          data["online"] = 0;
          return;
        }
        bool getMeta = false;
        if ( !data.isMember("l_meta") || fileinfo.st_mtime != data["l_meta"].asInt()){
          getMeta = true;
          data["l_meta"] = (long long)fileinfo.st_mtime;
        }
        if (stat((URL+".dtsh").c_str(), &fileinfo) == 0 && !S_ISDIR(fileinfo.st_mode)){
          if ( !data.isMember("h_meta") || fileinfo.st_mtime != data["h_meta"].asInt()){
            getMeta = true;
            data["h_meta"] = (long long)fileinfo.st_mtime;
          }
        }
        if ( !getMeta && data.isMember("meta") && data["meta"].isMember("tracks")){
          for (JSON::ObjIter trIt = data["meta"]["tracks"].ObjBegin(); trIt != data["meta"]["tracks"].ObjEnd(); trIt++){
            if (trIt->second["codec"] == "H264"){
              if ( !trIt->second.isMember("init")){
                getMeta = true;
              }else{
                if (trIt->second["init"].asString().size() < 4){
                  Log("WARN", "Source file "+URL+" does not contain H264 init data that MistServer can interpret.");
                  data["error"] = "Stream offline: Invalid?";
                }else{
                  if (trIt->second["init"].asString().c_str()[1] != 0x42){
                    Log("WARN", "Source file "+URL+" is not H264 Baseline - convert to baseline profile for best compatibility.");
                    data["error"] = "Not optimal (details in log)";
                  }else{
                    if (trIt->second["init"].asString().c_str()[3] > 30){
                      Log("WARN", "Source file "+URL+" is higher than H264 level 3.0 - convert to a level <= 3.0 for best compatibility.");
                      data["error"] = "Not optimal (details in log)";
                    }
                  }
                }
              }
            }
          }
          if ( !data["meta"] || !data["meta"]["tracks"]){
            Log("WARN", "Source file " + URL + " seems to be corrupt.");
            data["error"] = "Stream offline: Corrupt file?";
            data["online"] = 0;
            return;
          }
        }else{
          getMeta = true;
        }
        if (getMeta){
          // if the file isn't dtsc and there's no dtsh file, run getStream on it
          // this guarantees that if the stream is playable, it now has a valid header.
          if ((URL.substr(URL.size() - 5) != ".dtsc") && (stat((URL+".dtsh").c_str(), &fileinfo) != 0)){
            Util::Stream::getVod(URL, name);
          }
          //now, run mistinfo on the source - or on the accompanying dtsh file, if it exists
          if (stat((URL+".dtsh").c_str(), &fileinfo) == 0){
            URL += ".dtsh";
          }
          char * tmp_cmd[3] = {0, 0, 0};
          std::string mistinfo = Util::getMyPath() + "MistInfo";
          tmp_cmd[0] = (char*)mistinfo.c_str();
          tmp_cmd[1] = (char*)URL.c_str();
          data["meta"] = JSON::fromString(Util::Procs::getOutputOf(tmp_cmd));
          if ( !data["meta"] || !data["meta"].isMember("tracks") || !data["meta"]["tracks"]){
            Log("WARN", "Source file " + URL + " seems to be corrupt.");
            data["error"] = "Stream offline: Corrupt file?";
            data["online"] = 0;
            return;
          }
        }
        if ( data["meta"] && data["meta"].isMember("tracks") && data["meta"]["tracks"] && !DTSC::isFixed(data["meta"])){
          char * tmp_cmd[3] = {0, 0, 0};
          std::string mistfix = Util::getMyPath() + "MistDTSCFix";
          tmp_cmd[0] = (char*)mistfix.c_str();
          tmp_cmd[1] = (char*)URL.c_str();
          Util::Procs::getOutputOf(tmp_cmd);
          data.removeMember("meta");
        }
        if (!hasViewers(name)){
          if ( !data.isMember("error")){
            data["error"] = "Available";
          }
          data["online"] = 2;
        }else{
          data["online"] = 1;
        }
        return; //MistPlayer handles VoD
      }else{
        /// \todo Implement ffmpeg pulling again?
        //Util::Procs::Start(name, "ffmpeg -re -async 2 -i " + URL + " -f flv -", Util::getMyPath() + "MistFLV2DTSC", Util::getMyPath() + buffcmd);
        //Log("BUFF", "(re)starting stream buffer " + name + " for ffmpeg data: ffmpeg -re -async 2 -i " + URL + " -f flv -");
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
      if (!jit->second.isMember("name")){
        jit->second["name"] = jit->first;
      }
      if (!hasViewers(jit->first)){
        if (jit->second.isMember("source") && jit->second["source"].asString().substr(0, 1) == "/" && jit->second.isMember("error")
            && jit->second["error"].asString().substr(0,15) != "Stream offline:"){
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
        //if (jit->second.isMember("live") && !jit->second.isMember("meta") || !jit->second["meta"]){
        if ( (jit->second.isMember("meta") && !jit->second["meta"].isMember("tracks"))){
          jit->second["online"] = 0;
          jit->second["error"] = "No (valid) source connected ";
        }else{
          // for live streams, keep track of activity
          if (jit->second["meta"].isMember("live")){
            static std::map<std::string, liveCheck> checker;
            //check activity by monitoring the lastms of track 0;
            JSON::ObjIter trackIt = jit->second["meta"]["tracks"].ObjBegin();
            if (trackIt->second["lastms"].asInt() != checker[jit->first].lastms){
              checker[jit->first].lastms = trackIt->second["lastms"].asInt();
              checker[jit->first].last_active = currTime;
            }
            //check H264 tracks for optimality
            if (jit->second.isMember("meta") && jit->second["meta"].isMember("tracks")){
              for (JSON::ObjIter trIt = jit->second["meta"]["tracks"].ObjBegin(); trIt != jit->second["meta"]["tracks"].ObjEnd(); trIt++){
                if (trIt->second["codec"] == "H264"){
                  if (trIt->second.isMember("init")){
                    if (trIt->second["init"].asString().size() < 4){
                      Log("WARN", "Live stream "+jit->first+" does not contain H264 init data that MistServer can interpret.");
                      jit->second["error"] = "Stream offline: Invalid?";
                    }else{
                      if (trIt->second["init"].asString().c_str()[1] != 0x42){
                        Log("WARN", "Live stream "+jit->first+" is not H264 Baseline - convert to baseline profile for best compatibility.");
                        jit->second["error"] = "Not optimal (details in log)";
                      }else{
                        if (trIt->second["init"].asString().c_str()[3] > 30){
                          Log("WARN", "Live stream "+jit->first+" is higher than H264 level 3.0 - convert to a level <= 3.0 for best compatibility.");
                          jit->second["error"] = "Not optimal (details in log)";
                        }
                      }
                    }
                  }
                }
              }
            }
            // mark stream as offline if no activity for 5 seconds
            //if (jit->second.isMember("last_active") && jit->second["last_active"].asInt() < currTime - 5){
            if (checker[jit->first].last_active < currTime - 5){
              jit->second["online"] = 2;
              jit->second["error"] = "Source not active";
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
      WriteFile(Util::getTmpFolder() + "streamlist", strlist.toString());
    }
  }
  
  void AddStreams(JSON::Value & in, JSON::Value & out){
    //check for new streams and updates
    for (JSON::ObjIter jit = in.ObjBegin(); jit != in.ObjEnd(); jit++){
      if (out.isMember(jit->first)){
        if ( !streamsEqual(jit->second, out[jit->first])){
          out[jit->first].null();
          out[jit->first]["name"] = jit->first;
          out[jit->first]["source"] = jit->second["source"];
          out[jit->first]["DVR"] = jit->second["DVR"].asInt();
          out[jit->first]["cut"] = jit->second["cut"].asInt();
          out[jit->first]["updated"] = 1ll;
          Log("STRM", std::string("Updated stream ") + jit->first);
          if (out[jit->first]["source"].asStringRef().substr(0, 7) != "push://"){
            Util::Procs::Stop(jit->first);
            startStream(jit->first, out[jit->first]);
          }else{
            if ( !Util::Procs::isActive(jit->first)){
              startStream(jit->first, out[jit->first]);
            }
          }
        }
      }else{
        out[jit->first]["name"] = jit->first;
        out[jit->first]["source"] = jit->second["source"];
        out[jit->first]["DVR"] = jit->second["DVR"].asInt();
        out[jit->first]["cut"] = jit->second["cut"].asInt();
        Log("STRM", std::string("New stream ") + jit->first);
        startStream(jit->first, out[jit->first]);
      }
    }
  }

  ///\brief Parse a given stream configuration.
  ///\param in The requested configuration.
  ///\param out The new configuration after parsing.
  void CheckStreams(JSON::Value & in, JSON::Value & out){
    //check for new streams and updates
    AddStreams(in, out);

    //check for deleted streams
    std::set<std::string> toDelete;
    for (JSON::ObjIter jit = out.ObjBegin(); jit != out.ObjEnd(); jit++){
      if ( !in.isMember(jit->first)){
        toDelete.insert(jit->first);
        Log("STRM", std::string("Deleted stream ") + jit->first);
        Util::Procs::Stop(jit->first);
      }
    }
    //actually delete the streams
    while (toDelete.size() > 0){
      std::string deleting = *(toDelete.begin());
      out.removeMember(deleting);
      toDelete.erase(deleting);
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

  }

} //Controller namespace
