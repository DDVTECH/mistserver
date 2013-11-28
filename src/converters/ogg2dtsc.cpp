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
      oggTrack() : lastTime(0), parsedHeaders(false) { }
      codecType codec;
      std::string name;
      long long unsigned int dtscID;
      double lastTime;
      bool parsedHeaders;
      //Codec specific elements
      theora::header idHeader;//needed to determine keyframe
  };

  int OGG2DTSC(){
    std::string oggBuffer;
    OGG::Page oggPage;
    //Read all of std::cin to oggBuffer
    double mspft;//microseconds per frame
    double mspfv;//microseconds per frame vorbis
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
          if (memcmp(oggPage.getFullPayload()+1, "theora", 6) == 0){
            headerSeen += 1;
            headerWritten = false;
            trackData[sNum].codec = THEORA;
            //fix timerate here
            //frn/frd = fps
            theora::header tempHead;
            tempHead.read(oggPage.getFullPayload(), oggPage.getPayloadSize());
            mspft = (double)(tempHead.getFRD() * 1000) / tempHead.getFRN();
          }else if(memcmp(oggPage.getFullPayload()+1, "vorbis", 6) == 0){
            headerSeen += 1;
            headerWritten = false;
            trackData[sNum].codec = VORBIS;
            vorbis::header tempHead;
            tempHead.read(oggPage.getFullPayload(), oggPage.getPayloadSize());
            mspfv = ntohl(tempHead.getAudioSampleRate()) / 1000;
          }else{
            std::cerr << "Unknown Codec, " << std::string(oggPage.getFullPayload()+1, 6)<<" skipping" << std::endl;
            continue;
          }
          trackData[sNum].dtscID = lastTrackID++;
          std::stringstream tID;
          tID << "track" << trackData[sNum].dtscID;
          trackData[sNum].name = tID.str();
        }
        //if Serial number is available in mapping
        if(trackData.find(sNum)!=trackData.end()){//create DTSC from OGG page
          int offset = 0;
          for (std::deque<unsigned int>::iterator it = oggPage.getSegmentTableDeque().begin(); it != oggPage.getSegmentTableDeque().end(); it++){
            if (trackData[sNum].parsedHeaders){
              //output DTSC packet
              DTSCOut.null();//clearing DTSC buffer
              DTSCOut["trackid"] = (long long)trackData[sNum].dtscID;
              long long unsigned int temp = oggPage.getGranulePosition();
              DTSCOut["granule"] = (long long)temp;
              DTSCOut["time"] = (long long)trackData[sNum].lastTime;
              if (trackData[sNum].codec == THEORA){
                trackData[sNum].lastTime += (mspft / 4);
              }else{
                trackData[sNum].lastTime += (mspfv / 16);
              }
              DTSCOut["data"] = std::string(oggPage.getFullPayload()+offset, (*it)); //segment content put in JSON
              if (trackData[sNum].codec == THEORA){
                if (trackData[sNum].idHeader.parseGranuleLower(temp) == 0){ //granule mask equals zero when on keyframe
                  DTSCOut["keyframe"] = 1;
                }else{
                  DTSCOut["interframe"] = 1;
                }
              }
              // Ending packet
              if (oggPage.typeContinue()){//Continuing page
                DTSCOut["OggCont"] = 1;
              }
              if (oggPage.typeEOS()){//ending page
                DTSCOut["OggEOS"] = 1;
              }

              std::cout << DTSCOut.toNetPacked();
            }else{//if we ouput a header:
              //switch on codec
              switch(trackData[sNum].codec){
                case THEORA:{
                  theora::header tHead;
                  if(tHead.read(oggPage.getFullPayload()+offset, (*it))){//if the current segment is a Theora header part
                    //fillDTSC header
                    switch(tHead.getHeaderType()){
                      case 0:{ //identification header
                        trackData[sNum].idHeader = tHead;
                        DTSCHeader["tracks"][trackData[sNum].name]["height"] = (long long)tHead.getPICH();
                        DTSCHeader["tracks"][trackData[sNum].name]["width"] = (long long)tHead.getPICW();
                        DTSCHeader["tracks"][trackData[sNum].name]["idheader"] = std::string(oggPage.getFullPayload()+offset, (*it));
                        break;
                      }
                      case 1: //comment header
                        DTSCHeader["tracks"][trackData[sNum].name]["commentheader"] = std::string(oggPage.getFullPayload()+offset, (*it));
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
                  }
                  break;
                }
                case VORBIS:{
                  vorbis::header vHead;
                    if(vHead.read(oggPage.getFullPayload()+offset, (*it))){//if the current segment is a Vorbis header part
                      switch(vHead.getHeaderType()){
                        case 1:{
                          DTSCHeader["tracks"][trackData[sNum].name]["channels"] = (long long)vHead.getAudioChannels();
                          DTSCHeader["tracks"][trackData[sNum].name]["idheader"] = std::string(oggPage.getFullPayload()+offset, (*it));
                          break;
                        }
                        case 3:{
                          DTSCHeader["tracks"][trackData[sNum].name]["commentheader"] = std::string(oggPage.getFullPayload()+offset, (*it));
                          break;
                        }
                        case 5:{
                          DTSCHeader["tracks"][trackData[sNum].name]["codec"] = "vorbis";
                          DTSCHeader["tracks"][trackData[sNum].name]["trackid"] = (long long)trackData[sNum].dtscID;
                          DTSCHeader["tracks"][trackData[sNum].name]["type"] = "audio";
                          DTSCHeader["tracks"][trackData[sNum].name]["init"] = std::string(oggPage.getFullPayload()+offset, (*it));
                          headerSeen --;
                          trackData[sNum].parsedHeaders = true;
                          break;
                        }
                        default:{
                          std::cerr << "Unsupported header type for vorbis" << std::endl;
                        }
                      }
                    }else{
                      std::cerr << "Unknown Header" << std::endl;
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
        //write section
        if (oggPage.typeEOS()){//ending page
          //remove from trackdata
          trackData.erase(sNum);
        }
      }
    }
    std::cerr << "DTSC file created succesfully" << std::endl;
    return 0;
  }
}

int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.parseArgs(argc,argv);
  return Converters::OGG2DTSC();
}
