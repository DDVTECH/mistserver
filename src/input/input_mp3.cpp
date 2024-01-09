#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mist/defines.h>
#include <mist/flv_tag.h>
#include <mist/mpeg.h>
#include <mist/stream.h>
#include <string>

#include "input_mp3.h"

namespace Mist{
  inputMP3::inputMP3(Util::Config *cfg) : Input(cfg){
    capa["name"] = "MP3";
    capa["desc"] = "This input allows you to stream MP3 Video on Demand files.";
    capa["source_match"] = "/*.mp3";
    capa["source_file"] = "$source";
    capa["priority"] = 9;
    capa["codecs"]["audio"].append("MP3");
    timestamp = 0;
  }

  bool inputMP3::checkArguments(){
    if (config->getString("input") == "-"){
      Util::logExitReason(ER_FORMAT_SPECIFIC, "Input from stdin not yet supported");
      return false;
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

  bool inputMP3::preRun(){
    // open File
    inFile = fopen(config->getString("input").c_str(), "r");
    if (!inFile){
      Util::logExitReason(ER_READ_START_FAILURE, "Opening input '%s' failed", config->getString("input").c_str());
      return false;
    }
    return true;
  }

  bool inputMP3::readHeader(){
    if (!inFile){
      Util::logExitReason(ER_READ_START_FAILURE, "Reading header for '%s' failed: Could not open input stream", config->getString("input").c_str());
      return false;
    }
    meta.reInit(isSingular() ? streamName : "");
    size_t tNum = meta.addTrack();
    meta.setID(tNum, tNum);
    meta.setType(tNum, "audio");
    meta.setCodec(tNum, "MP3");
    // Create header file from MP3 data
    char header[10];
    fread(header, 10, 1, inFile); // Read a 10 byte header
    if (header[0] == 'I' || header[1] == 'D' || header[2] == '3'){
      size_t id3size = (((int)header[6] & 0x7F) << 21) | (((int)header[7] & 0x7F) << 14) |
                       (((int)header[8] & 0x7F) << 7) |
                       ((header[9] & 0x7F) + 10 + ((header[5] & 0x10) ? 10 : 0));
      INFO_MSG("id3 size: %zu bytes", id3size);
      fseek(inFile, id3size, SEEK_SET);
    }else{
      fseek(inFile, 0, SEEK_SET);
    }
    // Read the first mp3 header for bitrate and such
    uint64_t filePos = ftell(inFile);
    fread(header, 4, 1, inFile);
    fseek(inFile, filePos, SEEK_SET);

    Mpeg::MP2Info mp2Info = Mpeg::parseMP2Header(header);
    meta.setRate(tNum, mp2Info.sampleRate);
    meta.setChannels(tNum, mp2Info.channels);

    getNext();
    while (thisPacket){
      meta.update(thisPacket);
      getNext();
    }

    fseek(inFile, 0, SEEK_SET);
    timestamp = 0;
    return true;
  }

  void inputMP3::getNext(size_t idx){
    thisPacket.null();
    static char packHeader[3000];
    size_t filePos = ftell(inFile);
    size_t read = fread(packHeader, 1, 3000, inFile);
    if (!read){
      Util::logExitReason(ER_CLEAN_EOF, "Reached EOF");
      return;
    }
    if (packHeader[0] != 0xFF || (packHeader[1] & 0xE0) != 0xE0){
      // Find the first occurence of sync byte
      char *i = (char *)memchr(packHeader, (char)0xFF, read);
      if (!i){return;}
      size_t offset = i - packHeader;
      while (offset && (i[1] & 0xE0) != 0xE0){
        i = (char *)memchr(i + 1, (char)0xFF, read - (offset + 1));
        if (!i){
          offset = 0;
          break;
        }
      }
      if (!offset){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "Sync byte not found from offset %zu", filePos);
        return;
      }
      filePos += offset;
      fseek(inFile, filePos, SEEK_SET);
      read = fread(packHeader, 1, 3000, inFile);
    }
    // We now have a sync byte for sure

    // mpeg version is on the bits 0x18 of packHeader[1], but only 0x08 is important --> 0 is version 2, 1 is version 1
    // leads to 2 - value == version, -1 to get the right index for the array
    int mpegVersion = 1 - ((packHeader[1] >> 3) & 0x01);
    // mpeg layer is on the bits 0x06 of packHeader[1] --> 1 is layer 3, 2 is layer 2, 3 is layer 1
    // leads to 4 - value == layer, -1 to get the right index for the array
    int mpegLayer = 3 - ((packHeader[1] >> 1) & 0x03);
    int sampleCount = sampleCounts[mpegVersion][mpegLayer];
    // samplerate is encoded in bits 0x0C of packHeader[2];
    int sampleRate = sampleRates[mpegVersion][((packHeader[2] >> 2) & 0x03)] * 1000;

    int bitRate = bitRates[mpegVersion][mpegLayer][((packHeader[2] >> 4) & 0x0F)] * 1000;

    size_t dataSize = 0;
    if (mpegLayer == 0){// layer 1
      // Layer 1: dataSize = (12 * BitRate / SampleRate + Padding) * 4
      dataSize = (12 * ((double)bitRate / sampleRate) + ((packHeader[2] >> 1) & 0x01)) * 4;
    }else{// Layer 2 or 3
      // Layer 2, 3: dataSize = 144 * BitRate / SampleRate + Padding
      dataSize = 144 * ((double)bitRate / sampleRate) + ((packHeader[2] >> 1) & 0x01);
    }

    if (!dataSize){return;}
    fseek(inFile, filePos + dataSize, SEEK_SET);

    // Create a json value with the right data
    thisPacket.genericFill(timestamp, 0, 0, packHeader, dataSize, filePos, false);
    thisTime = timestamp;
    thisIdx = 0;

    // Update the internal timestamp
    timestamp += (sampleCount / (sampleRate / 1000));
  }

  void inputMP3::seek(uint64_t seekTime, size_t idx){
    idx = 0;
    DTSC::Keys keys(M.keys(idx));
    uint32_t keyNum = M.getKeyNumForTime(idx, seekTime);
    fseek(inFile, keys.getBpos(keyNum), SEEK_SET);
    timestamp = keys.getTime(keyNum);
  }
}// namespace Mist
