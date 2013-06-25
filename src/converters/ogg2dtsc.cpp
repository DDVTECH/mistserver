#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <map>
#include <mist/dtsc.h>
#include <mist/ogg.h>
#include <mist/theora.h>
#include <mist/config.h>
#include <mist/json.h>

namespace Converters{
  enum codecType {THEORA, VORBIS};

  class oggTrack{
    public:
      oggTrack() : lastTime() { }
      codecType codec;
      std::string name;
      long long unsigned int dtscID;
      long long unsigned int lastTime;
      //Codec specific elements
      theora::header idHeader;
  };

  int OGG2DTSC(){
    std::string oggBuffer;
    OGG::Page oggPage;
    //netpacked
    //Read all of std::cin to oggBuffer
    
    //while stream busy
    JSON::Value DTSCOut;
    JSON::Value DTSCHeader;
    DTSCHeader.null();
    DTSCHeader["moreheader"] = 0ll;
    std::map<long unsigned int, oggTrack> trackData;
    long long int lastTrackID = 1;
    while (std::cin.good()){
      for (unsigned int i = 0; (i < 1024) && (std::cin.good()); i++){//buffering
        oggBuffer += std::cin.get();
      }
      while (oggPage.read(oggBuffer)){//reading ogg to ogg::page
        //on succes, we handle one page
        long unsigned int sNum = oggPage.getBitstreamSerialNumber();
        if (oggPage.typeBOS()){//defines a new track
          std::cerr << "Begin "<< sNum << std::endl;
          if (memcmp(oggPage.getFullPayload()+1, "theora", 6) == 0){
            trackData[sNum].codec = THEORA;
            std::cerr << "Snr " << sNum << "=theora" << std::endl;
          }else if(memcmp(oggPage.getFullPayload()+1, "vorbis", 6) == 0){
            std::cerr << "Snr " << sNum << "=vorbis" << std::endl;
            trackData[sNum].codec = VORBIS;
          }else{
            std::cerr << "Unknown Codec, skipping" << std::endl;
            continue;
          }
          std::stringstream tID;
          tID << "track" << trackData[sNum].dtscID;
          trackData[sNum].name = tID.str();
          trackData[sNum].dtscID = lastTrackID++;
        }
        //if Serial number is available in mapping
        if(trackData.find(sNum)!=trackData.end()){
          //switch on codec
          switch(trackData[sNum].codec){
            case THEORA:{
              int offset = 0;
              theora::header tHead;
              fprintf(stderr, "Parsing %d elements\n", oggPage.getSegmentTableDeque().size());
              for (std::deque<unsigned int>::iterator it = oggPage.getSegmentTableDeque().begin(); it != oggPage.getSegmentTableDeque().end(); it++){
                fprintf(stderr, "Parsing Snr %u: element of length %d\n", sNum, (*it));
                if(tHead.read(oggPage.getFullPayload()+offset, (*it))){//if the current segment is a header part
                  std::cerr << "Theora Header Segment " << tHead.getHeaderType() << std::endl;
                  //fillDTSC header
                  switch(tHead.getHeaderType()){
                    case 0:{ //identification header
                      trackData[sNum].idHeader = tHead;
                      break;
                    }
                    case 1: //comment header
                      break;
                    case 2:{ //setup header, also the point to start writing the header
                      std::cout << DTSCHeader.toNetPacked();
                      break;
                    }
                  }
                }else{//if the current segment is a movie part
                  //output DTSC packet
                  DTSCOut.null();//clearing DTSC buffer
                  DTSCOut["trackid"] = (long long)trackData[sNum].dtscID;
                  long long unsigned int temp = oggPage.getGranulePosition();
                  DTSCOut["time"] = (long long)trackData[sNum].lastTime ++;
                  DTSCOut["data"] = std::string(oggPage.getFullPayload()+offset, (*it)); //segment content put in JSON
                  if (trackData[sNum].idHeader.parseGranuleLower(temp) == 0){ //granule mask equals zero when on keyframe
                    DTSCOut["keyframe"] = 1;
                  }else{
                    DTSCOut["interframe"] = 1;
                  }
                  fprintf(stderr,"Outputting a packet of %d bytes\n", (*it));
                  std::cout << DTSCOut.toNetPacked();
                }
                offset += (*it);
              }
              if (trackData[sNum].lastTime != (trackData[sNum].idHeader.parseGranuleUpper(oggPage.getGranulePosition()) + trackData[sNum].idHeader.parseGranuleLower(oggPage.getGranulePosition()))){

              }
              break;
            }
            case VORBIS:
              break;
            default:
              std::cerr << "Can not handle this codec" << std::endl;
              break;
          }
        }else{
          std::cerr <<"Error! Unknown bitstream number " << oggPage.getBitstreamSerialNumber() << std::endl;
        }
        if (oggPage.typeEOS()){//ending page
          std::cerr << oggPage.getBitstreamSerialNumber() << "  ending" << std::endl;
          trackData.erase(sNum);
          //remove from trackdata
        }
      }
    }
    return 0;
  }
}

int main(int argc, char ** argv){
  return Converters::OGG2DTSC();
}
