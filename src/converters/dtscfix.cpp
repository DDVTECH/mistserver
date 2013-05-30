/// \file dtscfix.cpp
/// Contains the code that will attempt to fix the metadata contained in an DTSC file.

#include <string>
#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/config.h>

///\brief Holds everything unique to converters.
namespace Converters {
  class HeaderEntryDTSC {
    public:
      HeaderEntryDTSC() : totalSize(0), parts(0), lastKeyTime(-5000), trackID(0), firstms(-1), lastms(0), keynum(0) {}
      long long int totalSize;
      long long int parts;
      long long int lastKeyTime;
      long long int trackID;
      long long int firstms;
      long long int lastms;
      long long int keynum;
      std::string type;
  };

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
    if (oriheader["moreheader"].asInt() > 0 && !conf.getBool("force")){
      if (meta.isMember("tracks") && meta["tracks"].size() > 0){
        bool isFixed = true;
        for ( JSON::ObjIter trackIt = meta["tracks"].ObjBegin(); trackIt != meta["tracks"].ObjEnd(); trackIt ++) {
          if ( !trackIt->second.isMember("frags") || !trackIt->second.isMember("keynum")){
            isFixed = false;
          }
        }
        if (isFixed){
          std::cerr << "This file was already fixed or doesn't need fixing - cancelling." << std::endl;
          return 0;
        }
      }
    }
    meta.removeMember("isFixed");
    meta.removeMember("keytime");
    meta.removeMember("keybpos");
    meta.removeMember("moreheader");

    std::map<std::string,int> trackIDs;
    std::map<std::string,HeaderEntryDTSC> trackData;


    long long int nowpack = 0;
    
    std::string currentID;
    int nextFreeID = 0;

    std::set<int> tmp;

    for (JSON::ObjIter it = meta["tracks"].ObjBegin(); it != meta["tracks"].ObjEnd(); it++){
      tmp.insert(it->second["trackid"].asInt());
      trackIDs.insert(std::pair<std::string,int>(it->first,it->second["trackid"].asInt()));
      trackData[it->first].type = it->second["type"].asString();
      trackData[it->first].trackID = it->second["trackid"].asInt();
      trackData[it->first].type = it->second["type"].asString();
      if (it->second["trackid"].asInt() >= nextFreeID){
        nextFreeID = it->second["trackid"].asInt() + 1;
      }
      it->second.removeMember("keylen");
      it->second.removeMember("keybpos");
      it->second.removeMember("frags");
      it->second.removeMember("keytime");
      it->second.removeMember("keynum");
      it->second.removeMember("keydata");
      it->second.removeMember("keyparts");
    }

    F.selectTracks(tmp);
    F.seek_time(0);

    F.seekNext();
    while ( !F.getJSON().isNull()){
      currentID = "";
      if (F.getJSON()["trackid"].asInt() == 0){
        if (F.getJSON()["datatype"].asString() == "video"){
          currentID = "video0";
          if (trackData[currentID].trackID == 0){
            trackData[currentID].trackID = nextFreeID++;
          }
          if (meta.isMember("video")){
            meta["tracks"][currentID] = meta["video"];
            meta.removeMember("video");
          }
          trackData[currentID].type = F.getJSON()["datatype"].asString();
        }else{
          if (F.getJSON()["datatype"].asString() == "audio"){
            currentID = "audio0";
            if (trackData[currentID].trackID == 0){
              trackData[currentID].trackID = nextFreeID++;
            }
            if (meta.isMember("audio")){
              meta["tracks"][currentID] = meta["audio"];
              meta.removeMember("audio");
            }
            trackData[currentID].type = F.getJSON()["datatype"].asString();
          }else{
            fprintf(stderr, "Found an unknown package with packetid 0 and datatype %s\n",F.getJSON()["datatype"].asString().c_str());
            F.seekNext();
            continue;
          }
        }
      }else{
        for( std::map<std::string,int>::iterator it = trackIDs.begin(); it != trackIDs.end(); it++ ) {
          if( it->second == F.getJSON()["trackid"].asInt() ) {
            currentID = it->first;
            break;
          }
        }
        if( currentID == "" ) {
          fprintf(stderr, "Found an unknown v2 packet with id %d\n", F.getJSON()["trackid"].asInt());
          F.seekNext();
          continue;
          //should create new track but this shouldnt be needed...
        }
      }
      if (F.getJSON()["time"].asInt() >= nowpack){
        nowpack = F.getJSON()["time"].asInt();
      }
      if (trackData[currentID].firstms == -1){
        trackData[currentID].firstms = nowpack;
      }
      if (trackData[currentID].type == "video"){
        if (F.getJSON().isMember("keyframe")){
          if (trackData[currentID].totalSize){
            meta["tracks"][currentID]["keydata"].append(trackData[currentID].totalSize);
            trackData[currentID].totalSize = 0;
            meta["tracks"][currentID]["keyparts"].append(trackData[currentID].parts);
            trackData[currentID].parts = 0;
          }
          meta["tracks"][currentID]["keytime"].append(F.getJSON()["time"]);
          meta["tracks"][currentID]["keybpos"].append(F.getLastReadPos());
          meta["tracks"][currentID]["keynum"].append( ++trackData[currentID].keynum);
          if (meta["tracks"][currentID]["keytime"].size() > 1){
            meta["tracks"][currentID]["keylen"].append(F.getJSON()["time"].asInt() - meta["tracks"][currentID]["keytime"][meta["tracks"][currentID]["keytime"].size() - 2].asInt());
          }
        }
      }else{
        if ((F.getJSON()["time"].asInt() - trackData[currentID].lastKeyTime) > 5000){
          trackData[currentID].lastKeyTime = F.getJSON()["time"].asInt();
          if (trackData[currentID].totalSize){
            meta["tracks"][currentID]["keydata"].append(trackData[currentID].totalSize);
            trackData[currentID].totalSize = 0;
            meta["tracks"][currentID]["keyparts"].append(trackData[currentID].parts);
            trackData[currentID].parts = 0;
          }
          meta["tracks"][currentID]["keytime"].append(F.getJSON()["time"]);
          meta["tracks"][currentID]["keybpos"].append(F.getLastReadPos());
          meta["tracks"][currentID]["keynum"].append( ++trackData[currentID].keynum);
          if (meta["tracks"][currentID]["keytime"].size() > 1){
            meta["tracks"][currentID]["keylen"].append(F.getJSON()["time"].asInt() - meta["tracks"][currentID]["keytime"][meta["tracks"][currentID]["keytime"].size() - 2].asInt());
          }
        }
      }
      trackData[currentID].totalSize += F.getJSON()["data"].asString().size();
      trackData[currentID].lastms = nowpack;
      trackData[currentID].parts ++;
      F.seekNext();
    }

    long long int firstms = 9999999999;
    long long int lastms = -1;

    for (std::map<std::string,HeaderEntryDTSC>::iterator it = trackData.begin(); it != trackData.end(); it++){
      meta["tracks"][it->first]["keydata"].append(it->second.totalSize);
      it->second.totalSize = 0;
      meta["tracks"][it->first]["keyparts"].append(it->second.parts);
      it->second.parts = 0;
      if (it->second.firstms < firstms){
        firstms = it->second.firstms;
      }
      if (it->second.lastms > lastms){
        lastms = it->second.lastms;
      }
      meta["tracks"][it->first]["firstms"] = it->second.firstms;
      meta["tracks"][it->first]["lastms"] = it->second.lastms;
      meta["tracks"][it->first]["length"] = (it->second.lastms - it->second.firstms) / 1000;
      if ( !meta["tracks"][it->first].isMember("bps")){
        meta["tracks"][it->first]["bps"] = (long long int)(it->second.lastms / ((it->second.lastms - it->second.firstms) / 1000));
      }
      if (it->second.trackID != 0){
        meta["tracks"][it->first]["trackid"] = trackIDs[it->first];
      }else{
        meta["tracks"][it->first]["trackid"] = nextFreeID ++;
      }
      meta["tracks"][it->first]["type"] = it->second.type;
      int tmp = meta["tracks"][it->first]["keytime"].size();
      if (tmp > 0){
        meta["tracks"][it->first]["keylen"].append(it->second.lastms - meta["tracks"][it->first]["keytime"][tmp - 1].asInt());
      }else{
        meta["tracks"][it->first]["keylen"].append(it->second.lastms);
      }
      //calculate fragments
      meta["tracks"][it->first]["frags"].null();
      long long int currFrag = -1;
      for (unsigned int i = 0; i < meta["tracks"][it->first]["keytime"].size(); i++){
        if (meta["tracks"][it->first]["keytime"][i].asInt() / 10000 > currFrag){
          currFrag = meta["tracks"][it->first]["keytime"][i].asInt() / 10000;
          long long int fragLen = 1;
          long long int fragDur = meta["tracks"][it->first]["keylen"][i].asInt();
          for (unsigned int j = i + 1; j < meta["tracks"][it->first]["keytime"].size(); j++){
            if (meta["tracks"][it->first]["keytime"][j].asInt() / 10000 > currFrag || j == meta["tracks"][it->first]["keytime"].size() - 1){
              JSON::Value thisFrag;
              thisFrag["num"] = meta["tracks"][it->first]["keynum"][i];
              thisFrag["len"] = fragLen;
              thisFrag["dur"] = fragDur;
              meta["tracks"][it->first]["frags"].append(thisFrag);
              break;
            }
            fragLen++;
            fragDur += meta["tracks"][it->first]["keylen"][j].asInt();
          }
        }
      }
    }

    meta["firstms"] = firstms;
    meta["lastms"] = lastms;
    meta["length"] = (lastms - firstms) / 1000;

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
  conf.addOption("force", JSON::fromString("{\"short\":\"f\", \"long\":\"force\", \"default\":0, \"help\":\"Force fixing.\"}"));
  conf.parseArgs(argc, argv);
  return Converters::DTSCFix(conf);
} //main
