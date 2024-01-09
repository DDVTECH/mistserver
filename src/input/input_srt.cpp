#include "input_srt.h"

namespace Mist{

  InputSrt::InputSrt(Util::Config *cfg) : Input(cfg){
    vtt = false;
    capa["name"] = "SubRip";
    capa["decs"] =
        "This input allows streaming of SubRip (SRT and WebVTT) subtitle files as Video on Demand.";
    capa["source_match"].append("/*.srt");
    capa["source_match"].append("/*.vtt");
    capa["priority"] = 9;
    capa["codecs"]["subtitle"].append("subtitle");
  }

  bool InputSrt::preRun(){
    fileSource.close();
    fileSource.open(config->getString("input").c_str());
    if (!fileSource.is_open()){
      Util::logExitReason(ER_READ_START_FAILURE, "Could not open file %s: %s", config->getString("input").c_str(), strerror(errno));
      return false;
    }
    return true;
  }

  bool InputSrt::checkArguments(){
    if (config->getString("input") == "-"){
      Util::logExitReason(ER_FORMAT_SPECIFIC, "Input from stdin not yet supported");
      return false;
    }else{
      preRun();
    }

    if (!config->getString("streamname").size()){
      if (config->getString("output") == "-"){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "Output to stdout not yet supported");
        return false;
      }
    }else{
      if (config->getString("output") != "-"){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "File output in player mode not supported");
        return false;
      }
    }
    return true;
  }

  bool InputSrt::readHeader(){
    if (!fileSource.good()){
      Util::logExitReason(ER_READ_START_FAILURE, "Reading header for '%s' failed: Could not open input stream", config->getString("input").c_str());
      return false;
    }
    size_t idx = meta.addTrack();
    meta.setID(idx, 1);
    meta.setType(idx, "meta");
    meta.setCodec(idx, "subtitle");

    getNext();
    while (thisPacket){
      meta.update(thisPacket);
      getNext();
    }
    return true;
  }

  void InputSrt::getNext(size_t idx){
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
        thisPack["bpos"] = fileSource.tellg();
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
          timestamp = (from_hour * 60 * 60 * 1000) + (from_min * 60 * 1000) + (from_sec * 1000) + from_ms;
          duration = ((to_hour * 60 * 60 * 1000) + (to_min * 60 * 1000) + (to_sec * 1000) + to_ms) - timestamp;
        }else{
          // subtitle
          if (data.size() > 1){data.append("\n");}
          data.append(line);
        }
      }
    }
    thisPacket.null();
  }

  void InputSrt::seek(uint64_t seekTime, size_t idx){fileSource.seekg(0, fileSource.beg);}

}// namespace Mist
