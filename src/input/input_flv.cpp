#include <iostream>
#include <fstream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <mist/stream.h>
#include <mist/defines.h>

#include "input_flv.h"

namespace Mist {
  inputFLV::inputFLV(Util::Config * cfg) : Input(cfg) {
    capa["name"] = "FLV";
    capa["desc"] = "Enables FLV Input";
    capa["source_match"] = "/*.flv";
    capa["priority"] = 9ll;
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("H263");
    capa["codecs"][0u][0u].append("VP6");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
  }

  bool inputFLV::setup() {
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
    
    //open File
    inFile = fopen(config->getString("input").c_str(), "r");
    if (!inFile) {
      return false;
    }
    return true;
  }

  bool inputFLV::readHeader() {
    if (!inFile){return false;}
    //See whether a separate header file exists.
    if (readExistingHeader()){return true;}
    //Create header file from FLV data
    fseek(inFile, 13, SEEK_SET);
    AMF::Object amf_storage;
    JSON::Value lastPack;
    long long int lastBytePos = 13;
    while (!feof(inFile) && !FLV::Parse_Error){
      if (tmpTag.FileLoader(inFile)){
        lastPack.null();
        lastPack = tmpTag.toJSON(myMeta, amf_storage);
        lastPack["bpos"] = lastBytePos;
        myMeta.update(lastPack);
        lastBytePos = ftell(inFile);
      }
    }
    if (FLV::Parse_Error){
      std::cerr << FLV::Error_Str << std::endl;
      return false;
    }
    myMeta.toFile(config->getString("input") + ".dtsh");
    return true;
  }
  
  void inputFLV::getNext(bool smart) {
    long long int lastBytePos = ftell(inFile);
    while (!feof(inFile) && !FLV::Parse_Error){
      if (tmpTag.FileLoader(inFile)){
        if ( !selectedTracks.count(tmpTag.getTrackID())){
          lastBytePos = ftell(inFile);
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
      FAIL_MSG("FLV error: %s", FLV::Error_Str.c_str());
      thisPacket.null();
      return;
    }
    if (!tmpTag.getDataLen()){
      return getNext();
    }
    thisPacket.genericFill(tmpTag.tagTime(), tmpTag.offset(), tmpTag.getTrackID(), tmpTag.getData(), tmpTag.getDataLen(), lastBytePos, tmpTag.isKeyframe); //init packet from tmpTags data
  }

  void inputFLV::seek(int seekTime) {
    //We will seek to the corresponding keyframe of the video track if selected, otherwise audio keyframe.
    //Flv files are never multi-track, so track 1 is video, track 2 is audio.
    int trackSeek = (selectedTracks.count(1) ? 1 : 2);
    size_t seekPos = myMeta.tracks[trackSeek].keys[0].getBpos();
    for (unsigned int i = 0; i < myMeta.tracks[trackSeek].keys.size(); i++){
      if (myMeta.tracks[trackSeek].keys[i].getTime() > seekTime){
        break;
      }
      seekPos = myMeta.tracks[trackSeek].keys[i].getBpos();
    }
    fseek(inFile, seekPos, SEEK_SET);
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

