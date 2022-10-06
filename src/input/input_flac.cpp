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

#include "input_flac.h"
#include <mist/flac.h>

namespace Mist{
  inputFLAC::inputFLAC(Util::Config *cfg) : Input(cfg){
    capa["name"] = "FLAC";
    capa["desc"] = "Allows loading FLAC files for Audio on Demand.";
    capa["source_match"] = "/*.flac";
    capa["source_file"] = "$source";
    capa["priority"] = 9;
    capa["codecs"]["audio"].append("FLAC");
    stopProcessing = false;
    stopFilling = false;
    pos = 2;
    curPos = 0;
    sampleNr = 0;
    frameNr = 0;
    sampleRate = 0;
    blockSize = 0;
    bitRate = 0;
    channels = 0;
    tNum = INVALID_TRACK_ID;
  }

  inputFLAC::~inputFLAC(){}

  bool inputFLAC::checkArguments(){
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

  bool inputFLAC::preRun(){
    inFile = fopen(config->getString("input").c_str(), "r");
    if (!inFile){return false;}
    return true;
  }

  void inputFLAC::stripID3tag(){
    char header[10];
    fread(header, 10, 1, inFile); // Read a 10 byte header
    if (header[0] == 'I' || header[1] == 'D' || header[2] == '3'){
      uint64_t id3size = (((int)header[6] & 0x7F) << 21) | (((int)header[7] & 0x7F) << 14) |
                         (((int)header[8] & 0x7F) << 7) |
                         ((header[9] & 0x7F) + 10 + ((header[5] & 0x10) ? 10 : 0));
      INFO_MSG("strip ID3 Tag, size: %" PRIu64 " bytes", id3size);
      curPos += id3size;
      fseek(inFile, id3size, SEEK_SET);
    }else{
      fseek(inFile, 0, SEEK_SET);
    }
  }

  bool inputFLAC::readMagicPacket(){
    char magic[4];
    if (fread(magic, 4, 1, inFile) != 1){
      FAIL_MSG("Could not read magic word - aborting!");
      return false;
    }

    if (FLAC::is_header(magic)){
      curPos += 4;
      return true;
    }

    FAIL_MSG("Not a FLAC file - aborting!");
    return false;
  }

  bool inputFLAC::readHeader(){
    if (!inFile){return false;}

    if (readExistingHeader()){
      WARN_MSG("header exists, read old one");

      if (M.inputLocalVars.isMember("blockSize")){
        return true;
      }else{
        INFO_MSG("Header needs update as it contains no blockSize, regenerating");
      }
    }

    meta.reInit(config->getString("streamname"));
    Util::ResizeablePointer tmpInit;
    tmpInit.append("fLaC", 4);

    stripID3tag();
    if (!readMagicPacket()){return false;}

    bool lastMeta = false;
    char metahead[4];
    while (!feof(inFile) && !lastMeta){
      if (fread(metahead, 4, 1, inFile) != 1){
        FAIL_MSG("Could not read metadata block header - aborting!");
        return false;
      }

      uint32_t bytes = Bit::btoh24(metahead + 1);
      lastMeta = (metahead[0] & 0x80); // check for last metadata block flag

      std::string mType;
      switch (metahead[0] & 0x7F){
      case 0:{
        mType = "STREAMINFO";

        char metaTmp[bytes];
        if (fread(metaTmp, bytes, 1, inFile) != 1){
          FAIL_MSG("Could not read streaminfo metadata - aborting!");
          return false;
        }

        // Store this block in init data
        metahead[0] |= 0x80; //Set last metadata block flag
        tmpInit.append(metahead, 4);
        tmpInit.append(metaTmp, bytes);


        HIGH_MSG("min blocksize: %zu, max: %zu", (size_t)(metaTmp[0] << 8 | metaTmp[1]),
                 (size_t)(metaTmp[2] << 8) | metaTmp[3]);

        blockSize = (metaTmp[0] << 8 | metaTmp[1]);
        if ((metaTmp[2] << 8 | metaTmp[3]) != blockSize){
          FAIL_MSG("variable block size not supported!");
          return 1;
        }

        sampleRate = ((metaTmp[10] << 12) | (metaTmp[11] << 4) | ((metaTmp[12] & 0xf0) >> 4));
        WARN_MSG("setting samplerate: %zu", sampleRate);

        channels = ((metaTmp[12] & 0xe) >> 1) + 1;
        HIGH_MSG("setting channels:  %zu", channels);

        break;
      }
      case 1: mType = "PADDING"; break;
      case 2: mType = "APPLICATION"; break;
      case 3: mType = "SEEKTABLE"; break;
      case 4: mType = "VORBIS_COMMENT"; break;
      case 5: mType = "CUESHEET"; break;
      case 6: mType = "PICTURE"; break;
      case 127: mType = "INVALID"; break;
      default: mType = "UNKNOWN"; break;
      }
      curPos += 4 + bytes;
      if (mType != "STREAMINFO"){fseek(inFile, bytes, SEEK_CUR);}
      INFO_MSG("Found %" PRIu32 "b metadata block of type %s", bytes, mType.c_str());
    }

    if (!sampleRate){
      FAIL_MSG("Could not get sample rate from file header");
      return false;
    }
    if (!channels){
      FAIL_MSG("no channel information found!");
      return false;
    }


    tNum = meta.addTrack();
    meta.setID(tNum, tNum);
    meta.setType(tNum, "audio");
    meta.setCodec(tNum, "FLAC");
    meta.setRate(tNum, sampleRate);
    meta.setChannels(tNum, channels);
    meta.setInit(tNum, tmpInit, tmpInit.size());
    meta.inputLocalVars["blockSize"] = blockSize;

    getNext();
    while (thisPacket){
      meta.update(thisPacket);
      getNext();
    }
    return true;
  }

  bool inputFLAC::fillBuffer(size_t size){
    if (feof(inFile)){
      INFO_MSG("EOF");
      return flacBuffer.size();
    }

    for (int i = 0; i < size; i++){
      char tmp = fgetc(inFile);
      if (!feof(inFile)){
        flacBuffer += tmp;
      }else{
        WARN_MSG("End, process remaining buffer data: %zu bytes", flacBuffer.size());
        stopFilling = true;
        break;
      }
    }

    start = (char *)flacBuffer.data();
    end = start + flacBuffer.size();

    return flacBuffer.size();
  }

  void inputFLAC::getNext(size_t idx){
    while (!stopProcessing){
      blockSize = M.inputLocalVars["blockSize"].asInt();

      // fill buffer if needed
      if (pos + 16 >= flacBuffer.size()){
        if (!stopFilling){
          if (!fillBuffer(1000)){
            thisPacket.null();
            return;
          }
        }else{
          if (!flacBuffer.size()){
            thisPacket.null();
            return;
          }
        }
      }

      if (stopFilling && ((flacBuffer.size() - pos) < 1)){
        uint64_t timestamp = (sampleNr * 1000) / sampleRate;
        HIGH_MSG("process last frame size: %zu", flacBuffer.size());
        thisTime = timestamp;
        thisIdx = tNum;
        thisPacket.genericFill(timestamp, 0, 0, start, flacBuffer.size(), curPos, false);
        flacBuffer.clear();
        stopProcessing = true;
        return;
      }

      uint16_t tmp = Bit::btohs(start + pos);
      if (tmp == 0xfff8){// check sync code
        char *a = start + pos;

        int utfv = FLAC::utfVal(start + 4); // read framenumber
        int skip = FLAC::utfBytes(*(a + 4));
        prev_header_size = skip + 4;

        if (checksum::crc8(0, start + pos, prev_header_size) == *(a + prev_header_size)){
          // checksum pass, valid frame found
          if (((utfv + 1 != FLAC::utfVal(a + 4))) &&
              (utfv != 0)){// TODO: this check works only if framenumbers are used in the header
            HIGH_MSG("error frame, found: %d, expected: %d, ignore.. ", FLAC::utfVal(a + 4), utfv + 1);
            // checksum pass, but incorrect framenr... happens sometimes
          }else{
            //            FLAC_Frame f(start);
            FLAC::Frame f(start);
            if (sampleRate != 0 && f.rate() != sampleRate){
              FAIL_MSG("samplerate from frame header: %d, sampleRate from file header: %zu", f.rate(), sampleRate);
              thisPacket.null();
              return;
            }

            frameNr++;
            uint64_t timestamp = (sampleNr * 1000) / sampleRate;
            if (blockSize != f.samples()){
              FAIL_MSG("blockSize differs, %zu %u", blockSize, f.samples());
            }

            if (blockSize == 0){
              FAIL_MSG("Cannot detect block size");
              return;
            }
            if (blockSize == 1 || blockSize == 2){
              // get blockSize from end of header
              // TODO: need to test
              FAIL_MSG("weird blocksize");
              blockSize = *(start + prev_header_size); // 8 or 16 bits value
            }

            sampleNr += blockSize;

            thisTime = timestamp;
            thisIdx = tNum;
            thisPacket.genericFill(timestamp, 0, 0, start, pos, curPos, false);

            curPos += pos;
            flacBuffer.erase(0, pos);

            start = (char *)flacBuffer.data();
            end = start + flacBuffer.size();
            pos = 2;

            return;
          }
        }
      }
      pos++;
    }

    thisPacket.null();
    return;
  }

  void inputFLAC::seek(uint64_t seekTime, size_t idx){
    uint64_t mainTrack = M.mainTrack();
    blockSize = M.inputLocalVars["blockSize"].asInt();
    sampleRate = meta.getRate(mainTrack);

    pos = 2;
    clearerr(inFile);
    flacBuffer.clear();
    stopProcessing = false;
    stopFilling = false;
    DTSC::Keys keys(M.keys(mainTrack));
    DTSC::Parts parts(M.parts(mainTrack));
    uint64_t seekPos = keys.getBpos(0);
    uint64_t seekKeyTime = keys.getTime(0);
    // Replay the parts of the previous keyframe, so the timestaps match up
    for (size_t i = 0; i < keys.getEndValid(); i++){
      if (keys.getTime(i) > seekTime){break;}
      DONTEVEN_MSG("Seeking to %" PRIu64 ", found %" PRIu64 "...", seekTime, keys.getTime(i));
      seekPos = keys.getBpos(i);
      seekKeyTime = keys.getTime(i);
    }
    Util::fseek(inFile, seekPos, SEEK_SET);
    sampleNr = (uint64_t)((seekKeyTime * sampleRate / 1000 + (blockSize / 2)) / blockSize) * blockSize;
    HIGH_MSG("seek: %" PRIu64 ", sampleNr: %" PRIu64 ", time: %" PRIu64, seekPos, sampleNr, seekKeyTime);
    curPos = seekPos;
    return;
  }
}// namespace Mist
