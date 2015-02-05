#include <iostream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <mist/stream.h>
#include <mist/defines.h>

#include "input_dtsc.h"

namespace Mist {
  inputDTSC::inputDTSC(Util::Config * cfg) : Input(cfg) {
    capa["name"] = "DTSC";
    capa["desc"] = "Enables DTSC Input";
    capa["priority"] = 9ll;
    capa["source_match"] = "/*.dtsc";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("H263");
    capa["codecs"][0u][0u].append("VP6");
    capa["codecs"][0u][0u].append("theora");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("vorbis");
  }

  bool inputDTSC::setup() {
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
    inFile = DTSC::File(config->getString("input"));
    if (!inFile) {
      return false;
    }
    return true;
  }

  bool inputDTSC::readHeader() {
    if (!inFile) {
      return false;
    }
    DTSC::File tmp(config->getString("input") + ".dtsh");
    if (tmp) {
      myMeta = tmp.getMeta();
      DEBUG_MSG(DLVL_HIGH,"Meta read in with %lu tracks", myMeta.tracks.size());
      return true;
    }
    if (inFile.getMeta().moreheader < 0 || inFile.getMeta().tracks.size() == 0) {
      DEBUG_MSG(DLVL_FAIL,"Missing external header file");
      return false;
    }
    myMeta = DTSC::Meta(inFile.getMeta());
    DEBUG_MSG(DLVL_DEVEL,"Meta read in with %lu tracks", myMeta.tracks.size());
    return true;
  }
  
  void inputDTSC::getNext(bool smart) {
    if (smart){
      inFile.seekNext();
    }else{
      inFile.parseNext();
    }
    lastPack = inFile.getPacket();
  }

  void inputDTSC::seek(int seekTime) {
    inFile.seek_time(seekTime);
    initialTime = 0;
    playUntil = 0;
  }

  void inputDTSC::trackSelect(std::string trackSpec) {
    selectedTracks.clear();
    long long unsigned int index;
    while (trackSpec != "") {
      index = trackSpec.find(' ');
      selectedTracks.insert(atoi(trackSpec.substr(0, index).c_str()));
      if (index != std::string::npos) {
        trackSpec.erase(0, index + 1);
      } else {
        trackSpec = "";
      }
    }
    inFile.selectTracks(selectedTracks);
  }
}

