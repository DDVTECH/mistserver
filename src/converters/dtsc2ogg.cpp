#include<iostream>
#include<vector>
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
    OGG::Page curOggPage;
    srand (Util::getMS());//randomising with milliseconds from boot
    std::vector<unsigned int> curSegTable;
    char* curNewPayload;
    //std::map <long long unsigned int, unsigned int> DTSCID2OGGSerial;
    //std::map <long long unsigned int, unsigned int> DTSCID2seqNum;
    OGG::headerPages oggMeta;
    //Creating ID headers for theora and vorbis
    oggMeta.readDTSCHeader(DTSCFile.getMeta());
    std::cout << oggMeta.parsedPages;
   
    //create DTSC in OGG pages
    DTSCFile.parseNext();
    curSegTable.clear();
    long long int prevID = DTSCFile.getJSON()["trackid"].asInt();
    long long int prevGran = DTSCFile.getJSON()["granule"].asInt();
    bool OggEOS = false;
    bool OggCont = false;
    bool IDChange = false;
    bool GranChange = false;
    std::string pageBuffer;
    
    while(DTSCFile.getJSON()){
      if(DTSCFile.getJSON()["trackid"].asInt()!=prevID || DTSCFile.getJSON()["granule"].asInt()!=prevGran || DTSCFile.getJSON()["granule"].asInt() == -1){
        curOggPage.clear();
        curOggPage.setVersion();
        if (OggCont){
          curOggPage.setHeaderType(1);//headertype 1 = Continue Page
        }else if (OggEOS){
          curOggPage.setHeaderType(4);//headertype 4 = end of stream
        }else{
          curOggPage.setHeaderType(0);//headertype 0 = normal
        }
        curOggPage.setGranulePosition(prevGran);
        curOggPage.setBitstreamSerialNumber(oggMeta.DTSCID2OGGSerial[prevID]);
        curOggPage.setPageSequenceNumber(oggMeta.DTSCID2seqNum[prevID]++);
        if(!curOggPage.setSegmentTable(curSegTable)){
          std::cerr << "Troubling segTable:";
          for (unsigned int i = 0; i<curSegTable.size(); i++){
            std::cerr << " " << curSegTable[i];
          }
          std::cerr << std::endl;
        }
        curOggPage.setPayload((char*)pageBuffer.c_str(), pageBuffer.size());
        curOggPage.setCRCChecksum(curOggPage.calcChecksum());
        std::cout << std::string(curOggPage.getPage(), curOggPage.getPageSize());
        pageBuffer = "";
        curSegTable.clear();
        //write one pagebuffer as Ogg page
      }
      
      pageBuffer += DTSCFile.getJSON()["data"].asString();
      curSegTable.push_back(DTSCFile.getJSON()["data"].asString().size());
      prevID = DTSCFile.getJSON()["trackid"].asInt();
      prevGran = DTSCFile.getJSON()["granule"].asInt();
      if (DTSCFile.getJSON()["OggEOS"]){
        OggEOS=true;
      }else{
        OggEOS=false;
      }
      if (DTSCFile.getJSON()["OggCont"]){
        OggCont=true;
      }else{
        OggCont=false;
      }
      DTSCFile.parseNext();
    }
    //quick copy-paste fix to output the last ogg page
      curOggPage.clear();
      curOggPage.setVersion();
      curOggPage.setHeaderType(4);//headertype 4 = end of stream
      curOggPage.setGranulePosition(prevGran);
      curOggPage.setBitstreamSerialNumber(oggMeta.DTSCID2OGGSerial[prevID]);
      curOggPage.setPageSequenceNumber(oggMeta.DTSCID2seqNum[prevID]++);
      curOggPage.setSegmentTable(curSegTable);
      curOggPage.setPayload((char*)pageBuffer.c_str(), pageBuffer.size());
      curOggPage.setCRCChecksum(curOggPage.calcChecksum());
      std::cout << std::string(curOggPage.getPage(), curOggPage.getPageSize());
      pageBuffer = "";
      curSegTable.clear();
      //write one pagebuffer as Ogg page
    //end quick fix

    return 0;   
  }
}

int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("filename", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"Filename of the DTSC file to analyse.\"}"));
  conf.parseArgs(argc, argv);
  return Converters::DTSC2OGG(conf);
}
