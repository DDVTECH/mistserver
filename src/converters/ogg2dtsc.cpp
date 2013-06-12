#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <string.h>
#include <vector>
#include <mist/dtsc.h>
#include <mist/ogg.h>
#include <mist/config.h>
#include <mist/json.h>

namespace Converters{
  struct oggTrack{
    long unsigned int serialNumber; //serial number in read OGG file
    long unsigned int lastSequenceNumber;//error checking for lost pages in OGG
    long long int dtscID; //track ID for in the written DTSC file
    DTSC::datatype type; //type of stream in DTSC
  };

  int OGG2DTSC(){
    std::string oggBuffer;
    OGG::Page oggPage;
    //netpacked
    //Read all of std::cin to oggBuffer
    
    //while stream busy
    JSON::Value DTSCOut;
    std::vector<oggTrack> trackData;
    long long int lastTrackID = 1;
    while (std::cin.good()){
      for (unsigned int i = 0; (i < 1024) && (std::cin.good()); i++){
        oggBuffer += std::cin.get();
      }
      //while OGG::page check functie{ read
      while (oggPage.read(oggBuffer)){//reading ogg to ogg::page
        //on succes, we handle one page
        if (oggPage.typeBOS()){//defines a new track
          //std::cout << oggPage.getFullPayload() << std::endl;
          oggTrack temp;
          temp.serialNumber = oggPage.getBitstreamSerialNumber();
          std::cerr << "Begin "<< temp.serialNumber << std::endl;
          temp.lastSequenceNumber = oggPage.getPageSequenceNumber();
          temp.dtscID = lastTrackID;
          lastTrackID++;
          if (memcmp(oggPage.getFullPayload()+1, "theora", 6)){
            temp.type = DTSC::VIDEO; 
            std::cerr << "Snr " << temp.serialNumber << "=theora" << std::endl;
          }else if(memcmp(oggPage.getFullPayload()+2, "vorbis", 6)){
            std::cerr << "Snr " << temp.serialNumber << "=vorbis" << std::endl;
            temp.type = DTSC::AUDIO;
          }else{
            std::cerr << "Unknown Codec!" << std::endl;
          }
          trackData.insert(trackData.end(), temp);
        }else if (oggPage.typeEOS()){//ending page
          std::cerr << std::hex << oggPage.getGranulePosition() <<std::dec << "ending" << std::endl;
        }else{//normal page
          std::cerr << std::hex << oggPage.getGranulePosition() <<std::dec<< std::endl;
          
          /*
          //ogg page 2 DTSC packet
          //for elk segment
            DTSCOut.null();//clearing DTSC buffer
            DTSCOut["trackid"] = 1; //video
            DTSCOut["trackid"] = 2; //audio
            DTSCOut["time"] = 0; //timestamp
            DTSCOut["data"] = 0; //segment inhoud
            DTSCOut["keyframe"] = "x"; //Aanmaken als eerste segment = keyframe
          //std::cout << "inner" << std::endl;
          */
        }
      }
      //std::cout << "outer" << std::endl;
    }
    return 0;
  }
}

int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.parseArgs(argc, argv);
  return Converters::OGG2DTSC();
}
