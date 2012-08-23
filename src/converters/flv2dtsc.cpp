/// \file flv2dtsc.cpp
/// Contains the code that will transform any valid FLV input into valid DTSC.

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <mist/flv_tag.h>
#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/amf.h>
#include <mist/config.h>

/// Holds all code that converts filetypes to/from to DTSC.
namespace Converters{

  /// Reads FLV from STDIN, outputs DTSC to STDOUT.
  int FLV2DTSC() {
    FLV::Tag FLV_in; // Temporary storage for incoming FLV data.
    JSON::Value meta_out; // Storage for outgoing header data.
    JSON::Value pack_out; // Storage for outgoing data.
    std::stringstream prebuffer; // Temporary buffer before sending real data
    bool sending = false;
    unsigned int counter = 0;
    
    while (!feof(stdin)){
      if (FLV_in.FileLoader(stdin)){
        pack_out = FLV_in.toJSON(meta_out);
        if (pack_out.isNull()){continue;}
        if (!sending){
          counter++;
          if (counter > 8){
            sending = true;
            std::string packed_header = meta_out.toPacked();
            unsigned int size = htonl(packed_header.size());
            std::cout << std::string(DTSC::Magic_Header, 4) << std::string((char*)&size, 4) << packed_header;
            std::cout << prebuffer.rdbuf();
            prebuffer.str("");
            std::cerr << "Buffer done, starting real-time output..." << std::endl;
          }else{
            std::string packed_out = pack_out.toPacked();
            unsigned int size = htonl(packed_out.size());
            prebuffer << std::string(DTSC::Magic_Packet, 4) << std::string((char*)&size, 4) << packed_out;
            continue;//don't also write
          }
        }
        //simply write
        std::string packed_out = pack_out.toPacked();
        unsigned int size = htonl(packed_out.size());
        std::cout << std::string(DTSC::Magic_Packet, 4) << std::string((char*)&size, 4) << packed_out;
      }
    }

    // if the FLV input is very short, do output it correctly...
    if (!sending){
      std::cerr << "EOF - outputting buffer..." << std::endl;
      std::string packed_header = meta_out.toPacked();
      unsigned int size = htonl(packed_header.size());
      std::cout << std::string(DTSC::Magic_Header, 4) << std::string((char*)&size, 4) << packed_header;
      std::cout << prebuffer.rdbuf();
    }
    std::cerr << "Done!" << std::endl;
    
    return 0;
  }//FLV2DTSC

};//Buffer namespace

/// Entry point for FLV2DTSC, simply calls Converters::FLV2DTSC().
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.parseArgs(argc, argv);
  return Converters::FLV2DTSC();
}//main
