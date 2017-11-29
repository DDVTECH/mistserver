#include "input_srt.h"

namespace Mist{

  InputSrt::InputSrt(Util::Config *cfg) : Input(cfg){
    vtt = false;
    capa["name"] = "SRT";
    capa["decs"] = "Enables SRT Input";
    capa["source_match"].append("/*.srt");
    capa["source_match"].append("/*.vtt");
    capa["priority"] = 9ll;
    capa["codecs"][0u][0u].append("subtitle");
  }

  bool InputSrt::preRun(){
    fileSource.close();
    fileSource.open(config->getString("input").c_str());
    if (!fileSource.is_open()){
      FAIL_MSG("Could not open file %s: %s", config->getString("input").c_str(), strerror(errno));
      return false;
    }
    return true;
  }

  bool InputSrt::checkArguments(){
    if (config->getString("input") == "-"){
      FAIL_MSG("Reading from standard input not yet supported");
      return false;
    }else{
      preRun();
    }

    if (!config->getString("streamname").size()){
      if (config->getString("output") == "-"){
        FAIL_MSG("Writing to standard output not yet supported");
        return false;
      }
    }else{
      if (config->getString("output") != "-"){
        FAIL_MSG("File output in player mode not supported");
        return false;
      }
    }
    return true;
  }

  bool InputSrt::readHeader(){
    if (!fileSource.good()){return false;}

    myMeta.tracks[1].trackID = 1;
    myMeta.tracks[1].type = "meta";
    myMeta.tracks[1].codec = "subtitle";

    getNext();
    while (thisPacket){
      myMeta.update(thisPacket);
      getNext();
    }

    // outputting dtsh file
    myMeta.toFile(config->getString("input") + ".dtsh");
    return true;
  }

  void InputSrt::getNext(bool smart){
    bool hasPacket = false;

    thisPacket.null();
    std::string line;

    uint32_t index;
    uint32_t timestamp;
    uint32_t duration;
    int lineNr = 0;
    std::string data;

    while (std::getline(fileSource, line)){// && !line.empty()){

      INFO_MSG("");
      INFO_MSG("reading line: %s", line.c_str());

      if (line.size() >= 7 && line.substr(0, 7) == "WEBVTT"){
        vtt = true;
        std::getline(fileSource, line);
        lineNr++;
      }

      lineNr++;

      INFO_MSG("linenr: %d", lineNr);
      if (line.empty() || (line.size() == 1 && line.at(0) == '\r')){
        static JSON::Value thisPack;
        thisPack.null();
        thisPack["trackid"] = 1;
        thisPack["bpos"] = (long long)fileSource.tellg();
        thisPack["data"] = data;
        thisPack["index"] = index;
        thisPack["time"] = timestamp;
        thisPack["duration"] = duration;

        // Write the json value to lastpack
        std::string tmpStr = thisPack.toNetPacked();
        thisPacket.reInit(tmpStr.data(), tmpStr.size());
        lineNr = 0;
        if (vtt){lineNr++;}
        return;
      }else{
        if (lineNr == 1){
          index = atoi(line.c_str());
        }else if (lineNr == 2){
          // timestamp
          int from_hour = 0;
          int from_min = 0;
          int from_sec = 0;
          int from_ms = 0;

          int to_hour = 0;
          int to_min = 0;
          int to_sec = 0;
          int to_ms = 0;
          sscanf(line.c_str(), "%d:%d:%d,%d --> %d:%d:%d,%d", &from_hour, &from_min, &from_sec,
                 &from_ms, &to_hour, &to_min, &to_sec, &to_ms);
          timestamp =
              (from_hour * 60 * 60 * 1000) + (from_min * 60 * 1000) + (from_sec * 1000) + from_ms;
          duration = ((to_hour * 60 * 60 * 1000) + (to_min * 60 * 1000) + (to_sec * 1000) + to_ms) -
                     timestamp;
        }else{
          // subtitle
          if (data.size() > 1){data.append("\n");}
          data.append(line);
        }
      }
    }
    thisPacket.null();
  }

  void InputSrt::seek(int seekTime){fileSource.seekg(0, fileSource.beg);}

  void InputSrt::trackSelect(std::string trackSpec){
    // we only have one track..
    selectedTracks.clear();
    selectedTracks.insert(1);
  }

}// namespace Mist

