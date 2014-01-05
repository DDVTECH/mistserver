#include<iostream>
#include<vector>
#include <queue>
#include <stdlib.h>

#include <mist/timing.h>
#include <mist/dtsc.h>
#include <mist/ogg.h>
#include <mist/theora.h>
#include <mist/vorbis.h>
#include <mist/config.h>
#include <mist/json.h>

namespace Converters{
  int DTSC2OGG(Util::Config & conf){
    DTSC::File DTSCFile(conf.getString("filename"));
    //JSON::Value meta = DTSCFile.getMeta();
    srand (Util::getMS());//randomising with milliseconds from boot
    std::vector<unsigned int> curSegTable;
    OGG::headerPages oggMeta;
    //Creating ID headers for theora and vorbis
    DTSC::readOnlyMeta fileMeta = DTSCFile.getMeta();
    DTSC::Meta giveMeta;
    for ( std::map<int,DTSC::readOnlyTrack>::iterator it = fileMeta.tracks.begin(); it != fileMeta.tracks.end(); it ++) {
      std::cerr << "TrackID: " << it->first << std::endl;
      giveMeta.tracks[it->first].trackID = fileMeta.tracks[it->first].trackID;
      giveMeta.tracks[it->first].idHeader = fileMeta.tracks[it->first].idHeader;
      giveMeta.tracks[it->first].init = fileMeta.tracks[it->first].init;
      giveMeta.tracks[it->first].commentHeader = fileMeta.tracks[it->first].commentHeader;
    }
   
    oggMeta.readDTSCHeader(giveMeta);
    std::cout << oggMeta.parsedPages;//outputting header pages
   
    //create DTSC in OGG pages
    DTSCFile.parseNext();
    std::map< long long int, std::vector<JSON::Value> > DTSCBuffer;
    long long unsigned int prevGran;
    long long int currID;
    long long unsigned int currGran;
    OGG::Page curOggPage;
    

    while(DTSCFile.getJSON()){
      currID = DTSCFile.getJSON()["trackid"].asInt();
      currGran = DTSCFile.getJSON()["granule"].asInt();
      if (DTSCBuffer.count(currID) && !DTSCBuffer[currID].empty()){
        prevGran = DTSCBuffer[currID][0]["granule"].asInt();
      }else{
        prevGran = 0;
      }
      if (prevGran != 0 && (currGran != prevGran)){
        curOggPage.readDTSCVector(DTSCBuffer[currID], oggMeta.DTSCID2OGGSerial[currID], oggMeta.DTSCID2seqNum[currID]);
        std::cout << std::string((char*)curOggPage.getPage(), curOggPage.getPageSize());
        DTSCBuffer[currID].clear();
        oggMeta.DTSCID2seqNum[currID]++;
      }
      DTSCBuffer[currID].push_back(DTSCFile.getJSON());

      DTSCFile.parseNext();
    }
    //outputting end of stream pages
    for (
      std::map< long long int, std::vector<JSON::Value> >::iterator it = DTSCBuffer.begin();
      it != DTSCBuffer.end();
      it++
    ){
      if (!DTSCBuffer[it->first].empty() && DTSCBuffer[it->first][0]["data"].asString() != ""){
        curOggPage.readDTSCVector(DTSCBuffer[it->first], oggMeta.DTSCID2OGGSerial[it->first], oggMeta.DTSCID2seqNum[it->first]);
        std::cout << std::string((char*)curOggPage.getPage(), curOggPage.getPageSize());
      }
    }
    
    return 0;   
  }
}

int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("filename", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"Filename of the DTSC file to analyse.\"}"));
  conf.parseArgs(argc, argv);
  return Converters::DTSC2OGG(conf);
}
