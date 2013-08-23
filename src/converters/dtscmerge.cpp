/// \file dtscmerge.cpp
/// Contains the code that will attempt to merge multiple files into a single DTSC file.

#include <string>
#include <vector>
#include <mist/config.h>
#include <mist/dtsc.h>

namespace Converters {
  int getNextFree( std::map<std::string,std::map<int,int> > mapping ){
    int result = 1;
    std::map<std::string,std::map<int,int> >::iterator mapIt;
    std::map<int,int>::iterator subIt;
    if (mapping.size()){
      for (mapIt = mapping.begin(); mapIt != mapping.end(); mapIt++){
        if (mapIt->second.size()){
          for (subIt = mapIt->second.begin(); subIt != mapIt->second.end(); subIt++){
            if (subIt->second >= result){
              result = subIt->second + 1;
            }
          }
        }
      }
    }
    return result;
  }

  struct keyframeInfo{
    std::string fileName;
    int trackID;
    int keyTime;
    int keyBPos;
    int keyNum;
    int keyLen;
    int endBPos;
  };//keyframeInfo struct

  int DTSCMerge(int argc, char ** argv){
    Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
    conf.addOption("output", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"Filename of the output file.\"}"));
    conf.addOption("input", JSON::fromString("{\"arg_num\":2, \"arg\":\"string\", \"help\":\"Filename of the first input file.\"}"));
    conf.addOption("[additional_inputs ...]", JSON::fromString("{\"arg_num\":3, \"default\":\"\", \"arg\":\"string\", \"help\":\"Filenames of any number of aditional inputs.\"}"));
    conf.parseArgs(argc, argv);

    DTSC::File outFile;
    JSON::Value meta;
    JSON::Value newMeta;
    std::map<std::string,std::map<int, int> > trackMapping;

    bool fullSort = true;
    std::map<std::string, DTSC::File> inFiles;
    std::string outFileName = argv[1];
    std::string tmpFileName;
    for (int i = 2; i < argc; i++){
      tmpFileName = argv[i];
      if (tmpFileName == outFileName){
        fullSort = false;
      }else{
        inFiles.insert(std::pair<std::string,DTSC::File>(tmpFileName,DTSC::File(tmpFileName)));
      }
    }

    if (fullSort){
      outFile = DTSC::File(outFileName, true);
    }else{
      outFile = DTSC::File(outFileName);
      meta = outFile.getMeta();
      newMeta = meta;
      if (meta.isMember("tracks") && meta["tracks"].size() > 0){
        for (JSON::ObjIter trackIt = meta["tracks"].ObjBegin(); trackIt != meta["tracks"].ObjEnd(); trackIt++){
          trackMapping[argv[1]].insert(std::pair<int,int>(trackIt->second["trackid"].asInt(),getNextFree(trackMapping)));
          newMeta["tracks"][trackIt->first]["trackid"] = trackMapping[argv[1]][trackIt->second["trackid"].asInt()];
        }
      }
    }

    std::multimap<int,keyframeInfo> allSorted;

    for (std::map<std::string,DTSC::File>::iterator it = inFiles.begin(); it != inFiles.end(); it++){
      JSON::Value tmpMeta = it->second.getMeta();
      if (tmpMeta.isMember("tracks") && tmpMeta["tracks"].size() > 0){
        for (JSON::ObjIter trackIt = tmpMeta["tracks"].ObjBegin(); trackIt != tmpMeta["tracks"].ObjEnd(); trackIt++){
          long long int oldID = trackIt->second["trackid"].asInt();
          long long int mappedID = getNextFree(trackMapping);
          trackMapping[it->first].insert(std::pair<int,int>(oldID,mappedID));
          for (JSON::ArrIter keyIt = trackIt->second["keys"].ArrBegin(); keyIt != trackIt->second["keys"].ArrEnd(); keyIt++){
            keyframeInfo tmpInfo;
            tmpInfo.fileName = it->first;
            tmpInfo.trackID = oldID;
            tmpInfo.keyTime = (*keyIt)["time"].asInt();
            tmpInfo.keyBPos = (*keyIt)["bpos"].asInt();
            tmpInfo.keyNum = (*keyIt)["num"].asInt();
            tmpInfo.keyLen = (*keyIt)["len"].asInt();
            if ((keyIt + 1) != trackIt->second["keys"].ArrEnd()){
              tmpInfo.endBPos = (*(keyIt + 1))["bpos"].asInt();
            }else{
              tmpInfo.endBPos = it->second.getBytePosEOF();
            }
            allSorted.insert(std::pair<int,keyframeInfo>((*keyIt)["time"].asInt(),tmpInfo));
          }
          std::cerr << it->first << "::" << oldID << ":\n" << trackIt->second.toPrettyString() << std::endl << std::endl;
          newMeta["tracks"][it->first + JSON::Value(mappedID).asString()] = trackIt->second;
          newMeta["tracks"][it->first + JSON::Value(mappedID).asString()]["trackid"] = mappedID;
          newMeta["tracks"][it->first + JSON::Value(mappedID).asString()].removeMember("keys");
          newMeta["tracks"][it->first + JSON::Value(mappedID).asString()].removeMember("frags");
        }
      }
    }
    newMeta["allsortedsize"] = (long long int)allSorted.size();

    if (fullSort){
      meta.null();
      meta["moreheader"] = 0ll;
      std::string tmpWrite = meta.toPacked();
      outFile.writeHeader(tmpWrite,true);
    }

    std::set<int> trackSelector;
    for (std::multimap<int,keyframeInfo>::iterator sortIt = allSorted.begin(); sortIt != allSorted.end(); sortIt++){
      trackSelector.clear();
      trackSelector.insert(sortIt->second.trackID);
      inFiles[sortIt->second.fileName].selectTracks(trackSelector);
      inFiles[sortIt->second.fileName].seek_time(sortIt->second.keyTime);
      inFiles[sortIt->second.fileName].seekNext();
      while (inFiles[sortIt->second.fileName].getJSON() && inFiles[sortIt->second.fileName].getBytePos() <= sortIt->second.endBPos && !inFiles[sortIt->second.fileName].reachedEOF()){
        if (inFiles[sortIt->second.fileName].getJSON()["trackid"].asInt() == sortIt->second.trackID){
          inFiles[sortIt->second.fileName].getJSON()["trackid"] = trackMapping[sortIt->second.fileName][sortIt->second.trackID];
          outFile.writePacket(inFiles[sortIt->second.fileName].getJSON());
        }
        inFiles[sortIt->second.fileName].seekNext();
      }
    }

    if (fullSort || (meta.isMember("merged") && meta["merged"])){
      newMeta["merged"] = true;
    }else{
      newMeta.removeMember("merged");
    }
    std::string writeMeta = newMeta.toPacked();
    meta["moreheader"] = outFile.addHeader(writeMeta);
    writeMeta = meta.toPacked();
    outFile.writeHeader(writeMeta);

    return 0;
  }
}

int main(int argc, char ** argv){
  return Converters::DTSCMerge(argc, argv);
}
