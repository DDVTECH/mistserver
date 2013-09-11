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
    char* curNewPayload;
    OGG::headerPages oggMeta;
    //Creating ID headers for theora and vorbis
    oggMeta.readDTSCHeader(DTSCFile.getMeta());
    std::cout << oggMeta.parsedPages;//outputting header pages
   
    //create DTSC in OGG pages
    DTSCFile.parseNext();
    std::map< long long int, std::vector<JSON::Value> > DTSCBuffer;
    long long unsigned int prevGran;
    long long int currID;
    long long int currGran;
    OGG::Page curOggPage;
    

    while(DTSCFile.getJSON()){
      currID = DTSCFile.getJSON()["trackid"].asInt();
      currGran = DTSCFile.getJSON()["granule"].asInt();
      if (DTSCBuffer.count(currID) && !DTSCBuffer[currID].empty()){
        prevGran = DTSCBuffer[currID][0]["granule"].asInt();
      }else{
        prevGran = 0;
      }
      if (prevGran != 0 && (prevGran == -1 || currGran != prevGran)){
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
