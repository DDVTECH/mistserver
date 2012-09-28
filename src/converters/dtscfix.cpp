/// \file dtscfix.cpp
/// Contains the code that will attempt to fix the metadata contained in an DTSC file.

#include <string>
#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/config.h>

/// Holds all code that converts filetypes to/from to DTSC.
namespace Converters{

  /// Reads an DTSC file and attempts to fix the metadata in it.
  int DTSCFix(Util::Config & conf) {
    DTSC::File F(conf.getString("filename"));
    JSON::Value oriheader = F.getMeta();
    JSON::Value meta = oriheader;
    JSON::Value pack;

    if (!oriheader.isMember("moreheader")){
      std::cerr << "This file is not DTSCFix'able. Please re-convert and try again." << std::endl;
      return 1;
    }
    if (oriheader["moreheader"].asInt() > 0){
      std::cerr << "Warning: This file has already been DTSCFix'ed. Doing this multiple times makes the file larger for no reason." << std::endl;
    }
    meta.removeMember("keytime");
    meta.removeMember("keybpos");
    meta.removeMember("moreheader");

    long long unsigned int firstpack = 0;
    long long unsigned int nowpack = 0;
    long long unsigned int lastaudio = 0;
    long long unsigned int lastvideo = 0;
    long long unsigned int lastkey = 0;
    long long unsigned int totalvideo = 0;
    long long unsigned int totalaudio = 0;
    long long unsigned int keyframes = 0;
    long long unsigned int key_min = 0xffffffff;
    long long unsigned int key_max = 0;
    long long unsigned int vid_min = 0xffffffff;
    long long unsigned int vid_max = 0;
    long long unsigned int aud_min = 0xffffffff;
    long long unsigned int aud_max = 0;
    long long unsigned int bfrm_min = 0xffffffff;
    long long unsigned int bfrm_max = 0;
    long long unsigned int bps = 0;

    F.seekNext();
    while (!F.getJSON().isNull()){
      nowpack = F.getJSON()["time"].asInt();
      if (firstpack == 0){firstpack = nowpack;}
      if (F.getJSON()["datatype"].asString() == "audio"){
        if (lastaudio != 0 && (nowpack - lastaudio) != 0){
          bps = F.getJSON()["data"].asString().size() / (nowpack - lastaudio);
          if (bps < aud_min){aud_min = bps;}
          if (bps > aud_max){aud_max = bps;}
        }
        totalaudio += F.getJSON()["data"].asString().size();
        lastaudio = nowpack;
      }
      if (F.getJSON()["datatype"].asString() == "video"){
        if (lastvideo != 0 && (nowpack - lastvideo) != 0){
          bps = F.getJSON()["data"].asString().size() / (nowpack - lastvideo);
          if (bps < vid_min){vid_min = bps;}
          if (bps > vid_max){vid_max = bps;}
        }
        if (F.getJSON()["keyframe"].asInt() != 0){
          meta["keytime"].append(F.getJSON()["time"]);
          meta["keybpos"].append(F.getLastReadPos());
          if (lastkey != 0){
            bps = nowpack - lastkey;
            if (bps < key_min){key_min = bps;}
            if (bps > key_max){key_max = bps;}
          }
          keyframes++;
          lastkey = nowpack;
        }
        if (F.getJSON()["offset"].asInt() != 0){
          bps = F.getJSON()["offset"].asInt();
          if (bps < bfrm_min){bfrm_min = bps;}
          if (bps > bfrm_max){bfrm_max = bps;}
        }
        totalvideo += F.getJSON()["data"].asString().size();
        lastvideo = nowpack;
      }
      F.seekNext();
    }

    meta["length"] = (long long int)((nowpack - firstpack)/1000);
    meta["lastms"] = (long long int)nowpack;
    if (meta.isMember("audio")){
      meta["audio"]["bps"] = (long long int)(totalaudio / ((lastaudio - firstpack) / 1000));
    }
    if (meta.isMember("video")){
      meta["video"]["bps"] = (long long int)(totalvideo / ((lastvideo - firstpack) / 1000));
      meta["video"]["keyms"] = (long long int)((lastvideo - firstpack) / keyframes);
      if (meta["video"]["keyms"].asInt() - key_min > key_max - meta["video"]["keyms"].asInt()){
        meta["video"]["keyvar"] = (long long int)(meta["video"]["keyms"].asInt() - key_min);
      }else{
        meta["video"]["keyvar"] = (long long int)(key_max - meta["video"]["keyms"].asInt());
      }
    }

    std::cerr << "Appending new header..." << std::endl;
    std::string loader = meta.toPacked();
    long long int newHPos = F.addHeader(loader);
    if (!newHPos){
      std::cerr << "Failure appending new header. Cancelling." << std::endl;
      return 1;
    }
    std::cerr << "Re-writing header..." << std::endl;
    oriheader["moreheader"] = newHPos;
    loader = oriheader.toPacked();
    if (F.writeHeader(loader)){
      std::cerr << "Metadata is now: " << meta.toPrettyString(0) << std::endl;
      return 0;
    }else{
      return -1;
    }
  }//DTSCFix

};

/// Entry point for FLV2DTSC, simply calls Converters::FLV2DTSC().
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("filename", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"Filename of the file to attempt to fix.\"}"));
  conf.parseArgs(argc, argv);
  return Converters::DTSCFix(conf);
}//main
