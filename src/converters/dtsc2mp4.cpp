/// \file dtsc2mp4.cpp
/// Contains the code that will transform any valid DTSC input into valid MP4s.

#include <iostream>
#include <algorithm>
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
    DTSC::File input(conf.getString("filename"));//DTSC input
    
    //DTSC::readOnlyMeta fileMeta = input.getMeta();
    DTSC::Meta giveMeta(input.getMeta());
    
    MP4::DTSC2MP4Converter Conv;//DTSC to MP4 converter class will handle header creation and media parsing
    std::cout << Conv.DTSCMeta2MP4Header(giveMeta);//Creating and outputting MP4 header from DTSC file
    
    //initialising JSON input
    std::set<int> selector;
    JSON::Value tmp = input.getMeta().toJSON();
    for (JSON::ObjIter trackIt = tmp["tracks"].ObjBegin(); trackIt != tmp["tracks"].ObjEnd(); trackIt++){
      selector.insert(trackIt->second["trackid"].asInt());
    }
    input.selectTracks(selector);
    input.seek_time(0);
    input.seekNext();
    
    //Parsing rest of file
    while (input.getJSON()){//as long as the file goes
      Conv.parseDTSC(input.getJSON());//parse 1 file DTSC packet
      if(Conv.sendReady()){//if the converter has a part to send out
        std::cout << Conv.sendString();//send out and clear Converter buffer
      }
      input.seekNext();//get next DTSC packet
    }
    //output remaining buffer
    std::cout << Conv.purgeBuffer();
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
