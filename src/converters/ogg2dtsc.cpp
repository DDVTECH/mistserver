#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <string.h>
#include <map>
#include <mist/dtsc.h>
#include <mist/ogg.h>
#include <mist/theora.h>
#include <mist/config.h>
#include <mist/json.h>

namespace Converters{
  enum codecType {THEORA, VORBIS};

  struct oggTrack{
    //long unsigned int serialNumber; //serial number in read OGG file
    long unsigned int lastSequenceNumber;//error checking for lost pages in OGG
    codecType codec;
    long long unsigned int dtscID;
    long long signed int fpks; //frames per kilo second
    DTSC::datatype type; //type of stream in DTSC
  };

  int OGG2DTSC(){
    std::string oggBuffer;
    OGG::Page oggPage;
    //netpacked
    //Read all of std::cin to oggBuffer
    
    //while stream busy
    JSON::Value DTSCOut;
    std::map<long unsigned int, oggTrack> trackData;
    long long int lastTrackID = 1;
    while (std::cin.good()){
      for (unsigned int i = 0; (i < 1024) && (std::cin.good()); i++){//buffering
        oggBuffer += std::cin.get();
      }
      //while OGG::page check functie{ read
      while (oggPage.read(oggBuffer)){//reading ogg to ogg::page
        //on succes, we handle one page
        long unsigned int sNum = oggPage.getBitstreamSerialNumber();
        if (oggPage.typeBOS()){//defines a new track
          std::cerr << "Begin "<< sNum << std::endl;
          trackData[sNum].lastSequenceNumber = oggPage.getPageSequenceNumber();
          trackData[sNum].dtscID = lastTrackID++;
          if (memcmp(oggPage.getFullPayload()+1, "theora", 6) == 0){
            trackData[sNum].type = DTSC::VIDEO; 
            trackData[sNum].codec = THEORA;
            std::cerr << "Snr " << sNum << "=theora" << std::endl;
          }else if(memcmp(oggPage.getFullPayload()+1, "vorbis", 6) == 0){
            std::cerr << "Snr " << sNum << "=vorbis" << std::endl;
            trackData[sNum].codec = VORBIS;
            trackData[sNum].type = DTSC::AUDIO;
          }else{
            std::cerr << "Unknown Codec!" << std::endl;
          }
        }
        //if Serial number is available in mapping
        if(trackData.find(sNum)!=trackData.end()){
          //switch on codec
          //oggTrack curTrack = trackData[oggPage.getBitstreamSerialNumber()];
          switch(trackData[sNum].codec){
            case THEORA:{
              //std::cerr << "Theora" << std::endl;
              char* curSeg;//current segment
              curSeg = oggPage.getFullPayload();//setting pointer to first segment
              std::deque<unsigned int> segTable;//
              //oggPage.getSegmentTableDeque().swap(segTable);
              segTable = oggPage.getSegmentTableDeque();
              unsigned int curPlace = 0;
              unsigned int curLength;
              theora::header tHead;
              while (!segTable.empty()) {
                curLength = segTable.front();
                segTable.pop_front();
                
                //std::cerr << curLength << ", " << std::hex << curSeg << std::dec;
                if(tHead.read(curSeg+curPlace, curLength)){//if the current segment is a header part
                  std::cerr << "Theora Header Segment " << tHead.getHeaderType() << std::endl;
                  //fillDTSC header
                  switch(tHead.getHeaderType()){
                    case 0: //identification header
                      trackData[sNum].fpks = ((long long int)tHead.getFRN() * 1000) / tHead.getFRD();
                      std::cerr << trackData[sNum].fpks << std::endl;
                    break;
                    case 1: //comment header
                    break;
                    case 2: //setup header
                    break;
                  }
                  
                }else{//if the current segment is a movie part
                  //std::cerr << "Theora Movie" << std::endl;
                  //output DTSC packet
                  DTSCOut.null();//clearing DTSC buffer
                  DTSCOut["trackid"] = (long long)trackData[sNum].dtscID;
                  DTSCOut["time"] = 0; //timestamp
                  DTSCOut["data"] = std::string(curSeg + curPlace, curLength); //segment content put in JSON
                  //DTSCOut["keyframe"] = "x"; //Aanmaken als eerste segment = keyframe
                  //else DTSCOut["interframe"] = "x";                  
                  std::cout << DTSCOut.toString();
                }
                curPlace += curLength;
              }
            break;
            }
            case VORBIS:
              //std::cerr << oggPage.getFullPayload() << " ";
              //std::cerr << "Vorbis Page" << std::endl;
            break;
            default:
              std::cerr << "Can not handle this codec" << std::endl;
            break;
          }
          /*
          //ogg page 2 DTSC packet
          //for every segment
            DTSCOut.null();//clearing DTSC buffer
            DTSCOut["trackid"] = 1; //video
            DTSCOut["trackid"] = 2; //audio
            DTSCOut["time"] = 0; //timestamp
            DTSCOut["data"] = 0; //segment inhoud
            DTSCOut["keyframe"] = "x"; //Aanmaken als eerste segment = keyframe
            else DTSCOut["interframe"] = "x";
          //std::cout << "inner" << std::endl;
          */
        }else{
          std::cerr <<"Error! " << oggPage.getBitstreamSerialNumber() << "unknown bitstream serial number" << std::endl;
        }
        if (oggPage.typeEOS()){//ending page
          std::cerr << oggPage.getBitstreamSerialNumber() << "  ending" << std::endl;
          //remove from trackdata
        }
      }
    }
    return 0;
  }
}

int main(int argc, char ** argv){
  //Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  //conf.parseArgs(argc, argv);
  return Converters::OGG2DTSC();
}
