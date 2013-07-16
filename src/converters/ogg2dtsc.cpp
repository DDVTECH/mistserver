#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <map>
#include <mist/dtsc.h>
#include <mist/ogg.h>
#include <mist/theora.h>
#include <mist/vorbis.h>
#include <mist/config.h>
#include <mist/json.h>

namespace Converters{
  enum codecType {THEORA, VORBIS};

  class oggTrack{
    public:
      oggTrack() : lastTime(), parsedHeaders(false) { }
      codecType codec;
      std::string name;
      long long unsigned int dtscID;
      long long unsigned int lastTime;
      bool parsedHeaders;
      //Codec specific elements
      theora::header idHeader;//needed to determine keyframe
  };

  int OGG2DTSC(){
    std::string oggBuffer;
    OGG::Page oggPage;
    //netpacked
    //Read all of std::cin to oggBuffer
    
    JSON::Value DTSCOut;
    JSON::Value DTSCHeader;
    DTSCHeader.null();
    DTSCHeader["moreheader"] = 0ll;
    std::map<long unsigned int, oggTrack> trackData;
    long long int lastTrackID = 1;
    int headerSeen = 0; 
    bool headerWritten = false;//important bool, used for outputting the simple DTSC header.
    bool allStreamsSeen = false; //other important bool used for error checking the EOS.
    //while stream busy
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
            headerSeen += 1;
            headerWritten = false;
            trackData[sNum].codec = THEORA;
            std::cerr << "Snr " << sNum << "=theora" << std::endl;
          }else if(memcmp(oggPage.getFullPayload()+1, "vorbis", 6) == 0){
            headerSeen += 1;
            headerWritten = false;
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
          int offset = 0;
          for (std::deque<unsigned int>::iterator it = oggPage.getSegmentTableDeque().begin(); it != oggPage.getSegmentTableDeque().end(); it++){
            if (trackData[sNum].parsedHeaders){
              //todo output segment
              //output DTSC packet
              DTSCOut.null();//clearing DTSC buffer
              DTSCOut["trackid"] = (long long)trackData[sNum].dtscID;
              long long unsigned int temp = oggPage.getGranulePosition();
              DTSCOut["time"] = (long long)trackData[sNum].lastTime ++;
              DTSCOut["data"] = std::string(oggPage.getFullPayload()+offset, (*it)); //segment content put in JSON
              if (trackData[sNum].codec == THEORA){
                if (trackData[sNum].idHeader.parseGranuleLower(temp) == 0){ //granule mask equals zero when on keyframe
                  DTSCOut["keyframe"] = 1;
                }else{
                  DTSCOut["interframe"] = 1;
                }
              }
              std::cout << DTSCOut.toNetPacked();
            }else{
              //switch on codec
              switch(trackData[sNum].codec){
                case THEORA:{
                  theora::header tHead;
                  if(tHead.read(oggPage.getFullPayload()+offset, (*it))){//if the current segment is a Theora header part
                    std::cerr << "Theora Header Segment " << tHead.getHeaderType() << std::endl;
                    //fillDTSC header
                    switch(tHead.getHeaderType()){
                      case 0:{ //identification header
                        std::cerr << "Theora ID header found" << std::endl;
                        trackData[sNum].idHeader = tHead;
                        DTSCHeader["tracks"][trackData[sNum].name]["height"] = (long long)tHead.getPICH();
                        DTSCHeader["tracks"][trackData[sNum].name]["width"] = (long long)tHead.getPICW();
                        DTSCHeader["tracks"][trackData[sNum].name]["theoraID"] = std::string(oggPage.getFullPayload()+offset, (*it));
                        break;
                      }
                      case 1: //comment header
                        std::cerr << "Theora comment header found" << std::endl;
                        break;
                      case 2:{ //setup header, also the point to start writing the header
                        DTSCHeader["tracks"][trackData[sNum].name]["codec"] = "theora";
                        DTSCHeader["tracks"][trackData[sNum].name]["trackid"] = (long long)trackData[sNum].dtscID;
                        DTSCHeader["tracks"][trackData[sNum].name]["type"] = "video";
                        DTSCHeader["tracks"][trackData[sNum].name]["init"] = std::string(oggPage.getFullPayload()+offset, (*it));
                        headerSeen --;
                        trackData[sNum].parsedHeaders = true;
                        break;
                      }
                    }
                  }else{//if the current segment is a movie part
                  }
                  break;
                }
                case VORBIS:{
                  vorbis::header vHead;
                    if(vHead.read(oggPage.getFullPayload()+offset, (*it))){//if the current segment is a Theora header part
                      switch(vHead.getHeaderType()){
                        case 1:{
                          std::cerr << "Vorbis ID header" << std::endl;
                          DTSCHeader["tracks"][trackData[sNum].name]["channels"] = (long long)vHead.getAudioChannels();
                          break;
                        }
                        case 5:{
                          std::cerr << "Vorbis init header" << std::endl;
                          DTSCHeader["tracks"][trackData[sNum].name]["codec"] = "vorbis";
                          DTSCHeader["tracks"][trackData[sNum].name]["trackid"] = (long long)trackData[sNum].dtscID;
                          DTSCHeader["tracks"][trackData[sNum].name]["type"] = "audio";
                          DTSCHeader["tracks"][trackData[sNum].name]["init"] = std::string(oggPage.getFullPayload()+offset, (*it));
                          headerSeen --;
                          trackData[sNum].parsedHeaders = true;
                          break;
                        }
                      }
                    }else{
                      //buffer vorbis
                    }
                  break;
                }
                default:
                  std::cerr << "Can not handle this codec" << std::endl;
                  break;
              }
            }
            offset += (*it);
          }

        }else{
          std::cerr <<"Error! Unknown bitstream number " << oggPage.getBitstreamSerialNumber() << std::endl;
        }
        //write header here
        if (headerSeen == 0 && headerWritten == false){
          std::cout << DTSCHeader.toNetPacked();
          headerWritten = true;
        }
        //write section buffer
        //write section
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
