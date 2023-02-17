#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mist/defines.h>
#include <mist/stream.h>
#include <mist/util.h>
#include <string>
#include <sys/stat.h>  //for stat
#include <sys/types.h> //for stat
#include <unistd.h>    //for stat

#include "input_flv.h"

namespace Mist{
  inputFLV::inputFLV(Util::Config *cfg) : Input(cfg){
    capa["name"] = "FLV";
    capa["desc"] = "Allows loading FLV files for Video on Demand.";
    capa["source_match"] = "/*.flv";
    capa["source_file"] = "$source";
    capa["priority"] = 9;
    capa["codecs"]["video"].append("H264");
    capa["codecs"]["video"].append("H263");
    capa["codecs"]["video"].append("VP6");
    capa["codecs"]["audio"].append("AAC");
    capa["codecs"]["audio"].append("MP3");
  }

  inputFLV::~inputFLV(){}

  bool inputFLV::checkArguments(){
    if (config->getString("input") == "-"){
      std::cerr << "Input from stdin not yet supported" << std::endl;
      return false;
    }
    if (!config->getString("streamname").size()){
      if (config->getString("output") == "-"){
        std::cerr << "Output to stdout not yet supported" << std::endl;
        return false;
      }
    }else{
      if (config->getString("output") != "-"){
        std::cerr << "File output in player mode not supported" << std::endl;
        return false;
      }
    }
    return true;
  }

  bool inputFLV::preRun(){
    // open File
    inFile = fopen(config->getString("input").c_str(), "r");
    if (!inFile){return false;}
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

  bool inputFLV::readHeader(){
    if (!inFile){return false;}
    meta.reInit(isSingular() ? streamName : "");
    // Create header file from FLV data
    Util::fseek(inFile, 13, SEEK_SET);
    AMF::Object amf_storage;
    uint64_t lastBytePos = 13;
    uint64_t bench = Util::getMicros();
    while (!feof(inFile) && !FLV::Parse_Error){
      if (tmpTag.FileLoader(inFile)){
        tmpTag.toMeta(meta, amf_storage);
        if (!tmpTag.getDataLen()){continue;}
        if (tmpTag.needsInitData() && tmpTag.isInitData()){continue;}
        size_t tNumber = meta.trackIDToIndex(tmpTag.getTrackID(), getpid());
        if (tNumber != INVALID_TRACK_ID){
          meta.update(tmpTag.tagTime(), tmpTag.offset(), tNumber, tmpTag.getDataLen(), lastBytePos,
                      tmpTag.isKeyframe);
        }
        lastBytePos = Util::ftell(inFile);
      }
    }
    bench = Util::getMicros(bench);
    INFO_MSG("Header generated in %" PRIu64 " ms: @%" PRIu64 ", %s, %s", bench / 1000, lastBytePos,
             M.getVod() ? "VoD" : "NOVoD", M.getLive() ? "Live" : "NOLive");
    if (FLV::Parse_Error){
      tmpTag = FLV::Tag();
      FLV::Parse_Error = false;
      ERROR_MSG("Stopping at FLV parse error @%" PRIu64 ": %s", lastBytePos, FLV::Error_Str.c_str());
    }
    Util::fseek(inFile, 13, SEEK_SET);
    return true;
  }

  void inputFLV::getNext(size_t idx){
    uint64_t lastBytePos = Util::ftell(inFile);
    if (idx != INVALID_TRACK_ID){
      uint8_t targetTag = 0x08;
      if (M.getType(idx) == "video"){targetTag = 0x09;}
      if (M.getType(idx) == "meta"){targetTag = 0x12;}
      FLV::seekToTagType(inFile, targetTag);
    }
    while (!feof(inFile) && !FLV::Parse_Error){
      if (tmpTag.FileLoader(inFile)){
        if (idx != INVALID_TRACK_ID && M.getID(idx) != tmpTag.getTrackID()){
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
      tmpTag = FLV::Tag();
      FAIL_MSG("FLV error @ %" PRIu64 ": %s", lastBytePos, FLV::Error_Str.c_str());
      thisPacket.null();
      return;
    }
    if (!tmpTag.getDataLen() || (tmpTag.needsInitData() && tmpTag.isInitData())){
      return getNext(idx);
    }
    thisIdx = meta.trackIDToIndex(tmpTag.getTrackID(), getpid());
    thisTime = tmpTag.tagTime();
    thisPacket.genericFill(thisTime, tmpTag.offset(), thisIdx, tmpTag.getData(),
                           tmpTag.getDataLen(), lastBytePos, tmpTag.isKeyframe);

    if (M.getCodec(idx) == "PCM" && M.getSize(idx) == 16){
      char *ptr = 0;
      size_t ptrSize = 0;
      thisPacket.getString("data", ptr, ptrSize);
      for (size_t i = 0; i < ptrSize; i += 2){
        char tmpchar = ptr[i];
        ptr[i] = ptr[i + 1];
        ptr[i + 1] = tmpchar;
      }
    }
  }

  void inputFLV::seek(uint64_t seekTime, size_t idx){
    // We will seek to the corresponding keyframe of the video track if selected, otherwise audio
    // keyframe. Flv files are never multi-track, so track 1 is video, track 2 is audio.
    size_t seekTrack = (idx == INVALID_TRACK_ID ? M.mainTrack() : idx);
    DTSC::Keys keys(M.keys(seekTrack));
    uint32_t keyNum = M.getKeyNumForTime(seekTrack, seekTime);
    Util::fseek(inFile, keys.getBpos(keyNum), SEEK_SET);
  }
}// namespace Mist
