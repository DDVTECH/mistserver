#include <iostream>
#include <fstream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <sys/types.h>//for stat
#include <sys/stat.h>//for stat
#include <unistd.h>//for stat
#include <mist/util.h>
#include <mist/stream.h>
#include <mist/defines.h>

#include "input_flv.h"

namespace Mist {
  inputFLV::inputFLV(Util::Config * cfg) : Input(cfg) {
    capa["name"] = "FLV";
    capa["desc"] = "Allows loading FLV files for Video on Demand.";
    capa["source_match"] = "/*.flv";
    capa["priority"] = 9ll;
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("H263");
    capa["codecs"][0u][0u].append("VP6");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
  }

  bool inputFLV::checkArguments() {
    if (config->getString("input") == "-") {
      std::cerr << "Input from stdin not yet supported" << std::endl;
      return false;
    }
    if (!config->getString("streamname").size()){
      if (config->getString("output") == "-") {
        std::cerr << "Output to stdout not yet supported" << std::endl;
        return false;
      }
    }else{
      if (config->getString("output") != "-") {
        std::cerr << "File output in player mode not supported" << std::endl;
        return false;
      }
    }
    return true;
  }
    
  bool inputFLV::preRun() {
    //open File
    inFile = fopen(config->getString("input").c_str(), "r");
    if (!inFile) {
      return false;
    }
    struct stat statData;
    lastModTime = 0;
    if (stat(config->getString("input").c_str(), &statData) != -1){
      lastModTime = statData.st_mtime;
    }
    return true;
  }

  /// Overrides the default keepRunning function to shut down
  /// if the file disappears or changes, by polling the file's mtime.
  /// If neither applies, calls the original function.
  bool inputFLV::keepRunning(){
    struct stat statData;
    if (stat(config->getString("input").c_str(), &statData) == -1){
      INFO_MSG("Shutting down because input file disappeared");
      return false;
    }
    if (lastModTime != statData.st_mtime){
      INFO_MSG("Shutting down because input file changed");
      return false;
    }
    return Input::keepRunning();
  }

  bool inputFLV::readHeader() {
    if (!inFile){return false;}
    //Create header file from FLV data
    Util::fseek(inFile, 13, SEEK_SET);
    AMF::Object amf_storage;
    long long int lastBytePos = 13;
    uint64_t bench = Util::getMicros();
    while (!feof(inFile) && !FLV::Parse_Error){
      if (tmpTag.FileLoader(inFile)){
        tmpTag.toMeta(myMeta, amf_storage);
        if (!tmpTag.getDataLen()){continue;}
        if (tmpTag.needsInitData() && tmpTag.isInitData()){continue;}
        myMeta.update(tmpTag.tagTime(), tmpTag.offset(), tmpTag.getTrackID(), tmpTag.getDataLen(), lastBytePos, tmpTag.isKeyframe);
        lastBytePos = Util::ftell(inFile);
      }
    }
    bench = Util::getMicros(bench);
    INFO_MSG("Header generated in %llu ms: @%lld, %s, %s", bench/1000, lastBytePos, myMeta.vod?"VoD":"NOVoD", myMeta.live?"Live":"NOLive");
    if (FLV::Parse_Error){
      FLV::Parse_Error = false;
      ERROR_MSG("Stopping at FLV parse error @%lld: %s", lastBytePos, FLV::Error_Str.c_str());
    }
    myMeta.toFile(config->getString("input") + ".dtsh");
    return true;
  }
  
  void inputFLV::getNext(bool smart) {
    long long int lastBytePos = Util::ftell(inFile);
    if (selectedTracks.size() == 1){
      uint8_t targetTag = 0x08;
      if (selectedTracks.count(1)){targetTag = 0x09;}
      if (selectedTracks.count(3)){targetTag = 0x12;}
      FLV::seekToTagType(inFile, targetTag);
    }
    while (!feof(inFile) && !FLV::Parse_Error){
      if (tmpTag.FileLoader(inFile)){
        if ( !selectedTracks.count(tmpTag.getTrackID())){
          lastBytePos = Util::ftell(inFile);
          continue;
        }
        break;
      }
    }
    if (feof(inFile)){
      thisPacket.null();
      return;
    }
    if (FLV::Parse_Error){
      FLV::Parse_Error = false;
      FAIL_MSG("FLV error @ %lld: %s", lastBytePos, FLV::Error_Str.c_str());
      thisPacket.null();
      return;
    }
    if (!tmpTag.getDataLen() || (tmpTag.needsInitData() && tmpTag.isInitData())){
      return getNext();
    }
    thisPacket.genericFill(tmpTag.tagTime(), tmpTag.offset(), tmpTag.getTrackID(), tmpTag.getData(), tmpTag.getDataLen(), lastBytePos, tmpTag.isKeyframe); //init packet from tmpTags data

    DTSC::Track & trk = myMeta.tracks[tmpTag.getTrackID()];
    if (trk.codec == "PCM" && trk.size == 16){
      char * ptr = 0;
      uint32_t ptrSize = 0;
      thisPacket.getString("data", ptr, ptrSize);
      for (uint32_t i = 0; i < ptrSize; i+=2){
        char tmpchar = ptr[i];
        ptr[i] = ptr[i+1];
        ptr[i+1] = tmpchar;
      }
    }
  }

  void inputFLV::seek(int seekTime) {
    //We will seek to the corresponding keyframe of the video track if selected, otherwise audio keyframe.
    //Flv files are never multi-track, so track 1 is video, track 2 is audio.
    int trackSeek = (selectedTracks.count(1) ? 1 : 2);
    uint64_t seekPos = myMeta.tracks[trackSeek].keys[0].getBpos();
    for (unsigned int i = 0; i < myMeta.tracks[trackSeek].keys.size(); i++){
      if (myMeta.tracks[trackSeek].keys[i].getTime() > seekTime){
        break;
      }
      seekPos = myMeta.tracks[trackSeek].keys[i].getBpos();
    }
    Util::fseek(inFile, seekPos, SEEK_SET);
  }

  void inputFLV::trackSelect(std::string trackSpec) {
    selectedTracks.clear();
    size_t index;
    while (trackSpec != "") {
      index = trackSpec.find(' ');
      selectedTracks.insert(atoi(trackSpec.substr(0, index).c_str()));
      if (index != std::string::npos) {
        trackSpec.erase(0, index + 1);
      } else {
        trackSpec = "";
      }
    }
  }
}

