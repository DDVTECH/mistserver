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
    JSON::Value meta = DTSCFile.getMeta();
    OGG::Page curOggPage;
    srand (Util::getMS());//randomising with milliseconds from boot
    std::vector<unsigned int> curSegTable;
    char* curNewPayload;
    std::map <long long unsigned int, unsigned int> DTSCID2OGGSerial;
    std::map <long long unsigned int, unsigned int> DTSCID2seqNum;
    //Creating ID headers for theora and vorbis
    for ( JSON::ObjIter it = meta["tracks"].ObjBegin(); it != meta["tracks"].ObjEnd(); it ++) {
      curOggPage.clear();
      curOggPage.setVersion();
      curOggPage.setHeaderType(2);//headertype 2 = Begin of Stream
      curOggPage.setGranulePosition(0);
      DTSCID2OGGSerial[it->second["trackid"].asInt()] = rand() % 0xFFFFFFFE +1; //initialising on a random not 0 number
      curOggPage.setBitstreamSerialNumber(DTSCID2OGGSerial[it->second["trackid"].asInt()]);
      DTSCID2seqNum[it->second["trackid"].asInt()] = 0;
      curOggPage.setPageSequenceNumber(DTSCID2seqNum[it->second["trackid"].asInt()]++);
      curSegTable.clear();
      curSegTable.push_back(it->second["IDHeader"].asString().size());
      curOggPage.setSegmentTable(curSegTable);
      curOggPage.setPayload((char*)it->second["IDHeader"].asString().c_str(), it->second["IDHeader"].asString().size());
      curOggPage.setCRCChecksum(curOggPage.calcChecksum());
      std::cout << std::string(curOggPage.getPage(), curOggPage.getPageSize());
    }
    //Creating remaining headers for theora and vorbis
    //for tracks in header
      //create standard page with comment (empty) en setup header(init)
    for ( JSON::ObjIter it = meta["tracks"].ObjBegin(); it != meta["tracks"].ObjEnd(); it ++) {
      curOggPage.clear();
      curOggPage.setVersion();
      curOggPage.setHeaderType(0);//headertype 0 = normal
      curOggPage.setGranulePosition(0);
      curOggPage.setBitstreamSerialNumber(DTSCID2OGGSerial[it->second["trackid"].asInt()]);
      curOggPage.setPageSequenceNumber(DTSCID2seqNum[it->second["trackid"].asInt()]++);
      curSegTable.clear();
      curSegTable.push_back(it->second["CommentHeader"].asString().size());
      curSegTable.push_back(it->second["init"].asString().size());
      curOggPage.setSegmentTable(curSegTable);
      std::string fullHeader = it->second["CommentHeader"].asString() + it->second["init"].asString();
      curOggPage.setPayload((char*)fullHeader.c_str(),fullHeader.size());
      //std::cerr << fullHeader.size() << std::endl;
      //std::cerr << "setPayload: " << curOggPage.setPayload((char*)fullHeader.c_str(), fullHeader.size()) << std::endl;
      curOggPage.setCRCChecksum(curOggPage.calcChecksum());
      std::cout << std::string(curOggPage.getPage(), curOggPage.getPageSize());
    }
    //create DTSC in OGG pages
    DTSCFile.parseNext();
    curSegTable.clear();
    long long int prevID = DTSCFile.getJSON()["trackid"].asInt();
    long long int prevGran = DTSCFile.getJSON()["granule"].asInt();
    bool OggEOS = false;
    //bool IDChange = false;
    //bool GranChange = false;
    std::string pageBuffer;
    
    while(DTSCFile.getJSON()){
      if(DTSCFile.getJSON()["trackid"].asInt()!=prevID || DTSCFile.getJSON()["granule"].asInt()!=prevGran){
        curOggPage.clear();
        curOggPage.setVersion();
        if (OggEOS){
          curOggPage.setHeaderType(4);//headertype 4 = end of stream
        }else{
          curOggPage.setHeaderType(0);//headertype 0 = normal
        }
        curOggPage.setGranulePosition(prevGran);
        curOggPage.setBitstreamSerialNumber(DTSCID2OGGSerial[prevID]);
        curOggPage.setPageSequenceNumber(DTSCID2seqNum[prevID]++);
        curOggPage.setSegmentTable(curSegTable);
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
      DTSCFile.parseNext();
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
