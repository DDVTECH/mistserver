/// \file dtsc2mp4.cpp
/// Contains the code that will transform any valid DTSC input into valid MP4s.

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <mist/json.h>
#include <mist/dtsc.h> //DTSC support
#include <mist/mp4.h> //MP4 support
#include <mist/config.h>

///\brief Holds everything unique to converters.
namespace Converters {

  ///\brief Converts DTSC from file to MP4 on stdout.
  ///\return The return code for the converter.
  int DTSC2MP4(Util::Config & conf){
    DTSC::File input(conf.getString("filename"));
    std::cerr << input.getMeta()["tracks"]["video0"].size() << std::endl;
    //ftyp box
    MP4::FTYP ftypBox;
    std::cout << std::string(ftypBox.asBox(),ftypBox.boxedSize());
    //moov box
    MP4::MOOV moovBox;
      MP4::MVHD mvhdBox;
      moovBox.setContent(mvhdBox, 0);
      
      //start arbitrary
      int boxOffset = 1;
      for (JSON::ObjIter it = input.getMeta()["tracks"].ObjBegin(); it != input.getMeta()["tracks"].ObjEnd(); it++){
        MP4::TRAK trakBox;
          MP4::TKHD tkhdBox;
          std::cerr << it->second["trackid"].asInt() << std::endl;
          tkhdBox.setTrackID(it->second["trackid"].asInt());
          
          if (it->second.isMember("width")){
            tkhdBox.setWidth(it->second["width"].asInt() << 16);
          }
          if (it->second.isMember("height")){
            tkhdBox.setHeight(it->second["height"].asInt() << 16);
          }
          trakBox.setContent(tkhdBox, 0);
          
          MP4::MDIA mdiaBox;
            MP4::MDHD mdhdBox;
            mdiaBox.setContent(mdhdBox, 0);
            
            MP4::HDLR hdlrBox;
            mdiaBox.setContent(hdlrBox, 1);
            MP4::MINF minfBox;
              MP4::DINF dinfBox;
                MP4::DREF drefBox;
                dinfBox.setContent(drefBox,0);
              minfBox.setContent(dinfBox,0);
              
              MP4::STBL stblBox;
                MP4::STSD stsdBox;
                stblBox.setContent(stsdBox,0);

                MP4::STSS stssBox;
                stblBox.setContent(stssBox,1);

                MP4::STSC stscBox;
                stblBox.setContent(stscBox,2);

                MP4::STCO stcoBox;
                stblBox.setContent(stcoBox,3);
              minfBox.setContent(stblBox,1);
            mdiaBox.setContent(minfBox, 2);
          trakBox.setContent(mdiaBox, 1);
        moovBox.setContent(trakBox, boxOffset);
        boxOffset++;
      }
      //end arbitrary
    //std::cout << input.getMeta()["audio"].toPrettyString() << std::endl;
    std::cout << std::string(moovBox.asBox(),moovBox.boxedSize());
    //mdat box alot
    return 0;
  } //DTSC2MP4

} //Converter namespace

/// Entry point for DTSC2FLV, simply calls Converters::DTSC2FLV().
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("filename", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"Filename of the input file to convert.\"}"));
  conf.parseArgs(argc, argv);
  return Converters::DTSC2MP4(conf);
} //main
