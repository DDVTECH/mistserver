#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mist/bitfields.h>
#include <mist/defines.h>
#include <mist/encode.h>
#include <mist/encryption.h>
#include <mist/stream.h>
#include <string>

#include "input_dtsccrypt.h"
#include <ctime>

namespace Mist{
  inputDTSC::inputDTSC(Util::Config *cfg) : Input(cfg){
    capa["name"] = "DTSC";
    capa["desc"] = "Enables DTSC Input";
    capa["priority"] = 9;
    capa["source_match"] = "/*.dtsc";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("H263");
    capa["codecs"][0u][0u].append("VP6");
    capa["codecs"][0u][0u].append("theora");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("vorbis");

    JSON::Value option;
    option["long"] = "key";
    option["short"] = "k";
    option["arg"] = "string";
    option["help"] = "The key to en/decrypt the current file with";
    config->addOption("key", option);
    option.null();

    option["long"] = "keyseed";
    option["short"] = "s";
    option["arg"] = "string";
    option["help"] = "The keyseed to en/decrypt the current file with";
    config->addOption("keyseed", option);
    option.null();

    option["long"] = "keyid";
    option["short"] = "i";
    option["arg"] = "string";
    option["help"] = "The keyid to en/decrypt the current file with";
    config->addOption("keyid", option);
    option.null();

    srand(time(NULL));
  }

  bool inputDTSC::checkArguments(){
    key = Encodings::Base64::decode(config->getString("key"));
    if (key == ""){
      if (config->getString("keyseed") == "" || config->getString("keyid") == ""){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "No key given, and no keyseed/keyid given");
        return false;
      }
      std::string tmpSeed = Encodings::Base64::decode(config->getString("keyseed"));
      std::string tmpID = Encodings::Base64::decode(config->getString("keyid"));
      std::string guid = Encryption::PR_GuidToByteArray(tmpID);
      key = Encryption::PR_GenerateContentKey(tmpSeed, guid);
    }

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

    // open File
    inFile = DTSC::File(config->getString("input"));
    if (!inFile){
      Util::logExitReason(ER_READ_START_FAILURE, "Opening input '%s' failed", config->getString("input").c_str());
      return false;
    }
    return true;
  }

  bool inputDTSC::readHeader(){
    if (!inFile){
      Util::logExitReason(ER_READ_START_FAILURE, "Reading header for '%s' failed: Could not open input stream", config->getString("input").c_str());
      return false;
    }
    DTSC::File tmp(config->getString("input") + ".dtsh");
    if (tmp){
      myMeta = tmp.getMeta();
      DEBUG_MSG(DLVL_HIGH, "Meta read in with %lu tracks", myMeta.tracks.size());
      return true;
    }
    if (inFile.getMeta().moreheader < 0 || inFile.getMeta().tracks.size() == 0){
      Util::logExitReason(ER_READ_START_FAILURE, "Missing external header file");
      return false;
    }
    myMeta = DTSC::Meta(inFile.getMeta());
    DEBUG_MSG(DLVL_DEVEL, "Meta read in with %lu tracks", myMeta.tracks.size());
    return true;
  }

  void inputDTSC::getNext(bool smart){
    if (smart){
      inFile.seekNext();
    }else{
      inFile.parseNext();
    }
    thisPacket = inFile.getPacket();
    // Do encryption/decryption here
    int tid = thisPacket.getTrackId();
    char *ivec;
    size_t ivecLen;
    thisPacket.getString("ivec", ivec, ivecLen);
    char iVec[16];
    if (ivecLen){
      memcpy(iVec, ivec, 8);
    }else{
      if (iVecs.find(tid) == iVecs.end()){
        iVecs[tid] = ((long long unsigned int)rand() << 32) + rand();
      }
      Bit::htobll(iVec, iVecs[tid]);
      iVecs[tid]++;
    }
    Encryption::encryptPlayReady(thisPacket, myMeta.tracks[tid].codec, iVec, key.data());
  }

  void inputDTSC::seek(int seekTime){
    inFile.seek_time(seekTime);
    initialTime = 0;
    playUntil = 0;
  }

  void inputDTSC::trackSelect(std::string trackSpec){
    selectedTracks.clear();
    long long unsigned int index;
    while (trackSpec != ""){
      index = trackSpec.find(' ');
      selectedTracks.insert(atoi(trackSpec.substr(0, index).c_str()));
      if (index != std::string::npos){
        trackSpec.erase(0, index + 1);
      }else{
        trackSpec = "";
      }
    }
    inFile.selectTracks(selectedTracks);
  }
}// namespace Mist
