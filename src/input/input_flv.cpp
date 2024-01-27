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
    capa["desc"] = "Allows loading FLV files and URLs for Video on Demand and/or live streaming";
    capa["source_match"].append("/*.flv");
    capa["source_match"].append("http://*.flv");
    capa["source_match"].append("https://*.flv");
    capa["source_match"].append("s3+http://*.flv");
    capa["source_match"].append("s3+https://*.flv");
    capa["source_file"] = "$source";
    capa["priority"] = 9;
    capa["codecs"]["video"].append("H264");
    capa["codecs"]["video"].append("H263");
    capa["codecs"]["video"].append("VP6");
    capa["codecs"]["audio"].append("AAC");
    capa["codecs"]["audio"].append("MP3");
    readBufferOffset = 0;
    readPos = 0;
    totalBytes = 0;
  }

  void inputFLV::dataCallback(const char *ptr, size_t size){
    readBuffer.append(ptr, size);
    totalBytes += size;
  }
  size_t inputFLV::getDataCallbackPos() const{return readPos + readBuffer.size();}

  inputFLV::~inputFLV(){}

  bool inputFLV::checkArguments(){
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

  bool inputFLV::needsLock(){
    // Streamed input requires no lock, non-streamed does
    if (!standAlone){return false;}
    if (config->getString("input") == "-"){return false;}
    return Input::needsLock();
  }

  bool inputFLV::preRun(){
    if (config->getString("input") == "-"){
      standAlone = false;
      inFile.open(0);
    }else{
      // open File
      inFile.open(config->getString("input"));
      if (!inFile){
        Util::logExitReason(ER_READ_START_FAILURE, "Opening input '%s' failed", config->getString("input").c_str());
        return false;
      }
      standAlone = inFile.isSeekable();
    }
    return true;
  }

  bool inputFLV::slideWindowTo(size_t seekPos, size_t seekLen){
    if (readPos > seekPos || seekPos > readPos + readBuffer.size() + 4*1024*1024){
      readBuffer.truncate(0);
      readBufferOffset = 0;
      if (!inFile.seek(seekPos)){
        FAIL_MSG("Seek to %" PRIu64 " failed! Aborting load", seekPos);
        return false;
      }
      readPos = inFile.getPos();
    }
    while (seekPos+seekLen > readPos + readBuffer.size() && config->is_active && inFile){
      size_t preSize = readBuffer.size();
      inFile.readSome(seekPos+seekLen - (readPos + readBuffer.size()), *this);
      if (readBuffer.size() == preSize){Util::sleep(5);}
    }
    if (seekPos+seekLen > readPos + readBuffer.size()){
      Util::logExitReason(ER_READ_START_FAILURE, "Input file seek abort");
      config->is_active = false;
      readBufferOffset = 0;
      return false;
    }
    readBufferOffset = seekPos - readPos;

    if (standAlone){
      //If we have more than 10MiB buffered and are more than 10MiB into the buffer, shift the first 4MiB off the buffer.
      //This prevents infinite growth of the read buffer for large files, but allows for some re-use of data.
      if (readBuffer.size() >= 10*1024*1024 && readBufferOffset > 10*1024*1024){
        readBuffer.shift(4*1024*1024);
        readBufferOffset -= 4*1024*1024;
        readPos += 4*1024*1024;
      }
    }else{
      //For non-standalone mode, we know we're always live streaming, and can always cut off what we've shifted
      if (readBufferOffset){
        readBuffer.shift(readBufferOffset);
        readPos += readBufferOffset;
        readBufferOffset = 0;
      }
    }

    return true;
  }

  bool inputFLV::readHeader(){
    if (!inFile){Util::logExitReason(
      ER_READ_START_FAILURE, "Reading header for '%s' failed: Could not open input stream", config->getString("input").c_str());
      return false;
    }
    if (!meta || (needsLock() && isSingular())){
      meta.reInit(isSingular() ? streamName : "");
    }
    // Create header file from FLV data
    AMF::Object amf_storage;
    uint64_t lastBytePos = 0;
    size_t needed = 0;
    while (inFile || readBufferOffset < readBuffer.size()){
      if (!slideWindowTo(readPos + readBufferOffset, needed)){
        Util::logExitReason(ER_READ_START_FAILURE, "input read/seek error");
        return false;
      }
      needed = tmpTag.MemLoader(readBuffer, readBuffer.size(), readBufferOffset);
      if (needed == std::string::npos){
        // Tag read error
        Util::logExitReason(ER_FORMAT_SPECIFIC, "FLV parser error: %s", FLV::Error_Str.c_str());
        return false;
      }
      if (!needed){
        // Successful tag read
        tmpTag.toMeta(meta, amf_storage);
        if (!tmpTag.getDataLen()){continue;}
        if (tmpTag.needsInitData() && tmpTag.isInitData()){continue;}
        size_t tNumber = meta.trackIDToIndex(tmpTag.getTrackID(), getpid());
        if (tNumber != INVALID_TRACK_ID){
          meta.update(tmpTag.tagTime(), tmpTag.offset(), tNumber, tmpTag.getDataLen(), lastBytePos, tmpTag.isKeyframe);
        }
        lastBytePos = readPos + readBufferOffset;
      }
      // If not successful, we now know how many bytes we need
    }
    return true;
  }

  void inputFLV::getNext(size_t idx){
    uint64_t lastBytePos = readPos + readBufferOffset;
    size_t needed = 0;
    while (inFile || readBufferOffset < readBuffer.size()){
      if (!slideWindowTo(readPos + readBufferOffset, needed)){
        Util::logExitReason(ER_READ_START_FAILURE, "input read/seek error");
        thisPacket.null();
        return;
      }
      needed = tmpTag.MemLoader(readBuffer, readBuffer.size(), readBufferOffset);
      if (needed == std::string::npos){
        // Tag read error
        Util::logExitReason(ER_FORMAT_SPECIFIC, "FLV parser error: %s", FLV::Error_Str.c_str());
        thisPacket.null();
        return;
      }
      if (!needed){
        // Successful tag read
        AMF::Object amf_storage;
        tmpTag.toMeta(meta, amf_storage);
        if (!tmpTag.getDataLen()){continue;}
        if (tmpTag.needsInitData() && tmpTag.isInitData()){continue;}
        thisIdx = meta.trackIDToIndex(tmpTag.getTrackID(), getpid());
        if (idx != INVALID_TRACK_ID && idx != thisIdx){continue;}
        break;
      }
      // If not successful, we now know how many bytes we need
    }

    // Reached EOF - not an error
    if (needed){
      thisPacket.null();
      return;
    }

    thisTime = tmpTag.tagTime();
    if (!standAlone){lastBytePos = 0;}
    thisPacket.genericFill(thisTime, tmpTag.offset(), thisIdx, tmpTag.getData(), tmpTag.getDataLen(), lastBytePos, tmpTag.isKeyframe);

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
    uint32_t keyNum = M.getKeyNumForTime(seekTrack, seekTime);
    if (!slideWindowTo(DTSC::Keys(M.keys(seekTrack)).getBpos(keyNum), 4)){
      WARN_MSG("Failed to seek to %" PRIu64, seekTime);
    }
  }
}// namespace Mist
