/// \file dtscfix.cpp
/// Contains the code that will attempt to fix the metadata contained in an DTSC file.

#include <string>
#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/config.h>

///\brief Holds everything unique to converters.
namespace Converters {

  ///\brief Reads a DTSC file and attempts to fix the metadata in it.
  ///\param conf The current configuration of the program.
  ///\return The return code for the fixed program.
  int DTSCFix(Util::Config & conf){
    DTSC::File F(conf.getString("filename"));
    JSON::Value oriheader = F.getFirstMeta();
    JSON::Value meta = F.getMeta();
    JSON::Value pack;

    if ( !oriheader.isMember("moreheader")){
      std::cerr << "This file is too old to fix - please reconvert." << std::endl;
      return 1;
    }
    if (oriheader["moreheader"].asInt() > 0){
      if ((meta.isMember("keytime") && meta.isMember("keybpos") && meta.isMember("keynum") && meta.isMember("keylen") && meta.isMember("frags"))
          || !meta.isMember("video")){
        std::cerr << "This file was already fixed or doesn't need fixing - cancelling." << std::endl;
        return 0;
      }
    }
    meta.removeMember("keytime");
    meta.removeMember("keybpos");
    meta.removeMember("moreheader");

    long long int nowpack = 0;
    long long int lastaudio = 0;
    long long int lastvideo = 0;
    long long unsigned int totalvideo = 0;
    long long unsigned int totalaudio = 0;
    long long int keynum = 0;

    F.seekNext();
    while ( !F.getJSON().isNull()){
      if (F.getJSON()["time"].asInt() >= nowpack){
        nowpack = F.getJSON()["time"].asInt();
      }
      if ( !meta.isMember("firstms")){
        meta["firstms"] = nowpack;
      }
      if (F.getJSON()["datatype"].asString() == "audio"){
        totalaudio += F.getJSON()["data"].asString().size();
        lastaudio = nowpack;
      }
      if (F.getJSON()["datatype"].asString() == "video"){
        if (F.getJSON()["keyframe"].asInt() != 0){
          meta["keytime"].append(F.getJSON()["time"]);
          meta["keybpos"].append(F.getLastReadPos());
          meta["keynum"].append( ++keynum);
          if (meta["keytime"].size() > 1){
            meta["keylen"].append(F.getJSON()["time"].asInt() - meta["keytime"][meta["keytime"].size() - 2].asInt());
          }
        }
        totalvideo += F.getJSON()["data"].asString().size();
        lastvideo = nowpack;
      }
      F.seekNext();
    }

    meta["length"] = ((nowpack - meta["firstms"].asInt()) / 1000);
    meta["lastms"] = nowpack;
    if (meta.isMember("audio")){
      meta["audio"]["bps"] = (long long int)(totalaudio / ((lastaudio - meta["firstms"].asInt()) / 1000));
    }
    if (meta.isMember("video")){
      meta["video"]["bps"] = (long long int)(totalvideo / ((lastvideo - meta["firstms"].asInt()) / 1000));
      meta["video"]["keyms"] = ((lastvideo - meta["firstms"].asInt()) / meta["keytime"].size());
      //append last keylen element - keytime, keybpos, keynum and keylen are now complete
      if (meta["keytime"].size() > 0){
        meta["keylen"].append(nowpack - meta["keytime"][meta["keytime"].size() - 1].asInt());
      }else{
        meta["keylen"].append(nowpack);
      }
      //calculate fragments
      meta["frags"].null();
      long long int currFrag = -1;
      for (unsigned int i = 0; i < meta["keytime"].size(); i++){
        if (meta["keytime"][i].asInt() / 10000 > currFrag){
          currFrag = meta["keytime"][i].asInt() / 10000;
          long long int fragLen = 1;
          long long int fragDur = meta["keylen"][i].asInt();
          for (unsigned int j = i; j < meta["keytime"].size(); j++){
            if (meta["keytime"][j].asInt() / 10000 > currFrag || j == meta["keytime"].size() - 1){
              JSON::Value thisFrag;
              thisFrag["num"] = meta["keynum"][i];
              thisFrag["len"] = fragLen;
              thisFrag["dur"] = fragDur;
              meta["frags"].append(thisFrag);
              break;
            }
            fragLen++;
            fragDur += meta["keylen"][j].asInt();
          }
        }
      }
    }

    //append the revised header
    std::string loader = meta.toPacked();
    long long int newHPos = F.addHeader(loader);
    if ( !newHPos){
      std::cerr << "Failure appending new header." << std::endl;
      return -1;
    }
    //re-write the original header with information about the location of the new one
    oriheader["moreheader"] = newHPos;
    loader = oriheader.toPacked();
    if (F.writeHeader(loader)){
      std::cerr << "Metadata is now: " << meta.toPrettyString(0) << std::endl;
      return 0;
    }else{
      std::cerr << "Failure rewriting header." << std::endl;
      return -1;
    }
  } //DTSCFix

}

/// Entry point for DTSCFix, simply calls Converters::DTSCFix().
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("filename", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"Filename of the file to attempt to fix.\"}"));
  conf.parseArgs(argc, argv);
  return Converters::DTSCFix(conf);
} //main
