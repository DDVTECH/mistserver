/// \file flv2dtsc.cpp
/// Contains the code that will transform any valid FLV input into valid DTSC.

#include <iostream>
#include <sstream>
#include <fstream>
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

///\brief Holds everything unique to converters.
namespace Converters {

  ///\brief Converts FLV from stdin to DTSC on stdout.
  ///\return The return code for the converter.
  int FLV2DTSC(std::ostream & output){
    FLV::Tag FLV_in; // Temporary storage for incoming FLV data.
    DTSC::Meta meta_out; // Storage for outgoing header data.
    JSON::Value pack_out; // Storage for outgoing data.
    std::stringstream prebuffer; // Temporary buffer before sending real data
    bool sending = false;
    unsigned int counter = 0;

    while ( !feof(stdin) && !FLV::Parse_Error){
      if (FLV_in.FileLoader(stdin)){
        pack_out = FLV_in.toJSON(meta_out);
        if (pack_out.isNull()){
          continue;
        }
        if ( !sending){
          counter++;
          if (counter > 8){
            sending = true;
            output << meta_out.toJSON().toNetPacked();
            output << prebuffer.rdbuf();
            prebuffer.str("");
            std::cerr << "Buffer done, starting real-time output..." << std::endl;
          }else{
            prebuffer << pack_out.toNetPacked();
            continue; //don't also write
          }
        }
        //simply write
        output << pack_out.toNetPacked();
      }
    }
    if (FLV::Parse_Error){
      std::cerr << "Conversion failed: " << FLV::Error_Str << std::endl;
      return 0;
    }

    // if the FLV input is very short, do output it correctly...
    if ( !sending){
      std::cerr << "EOF - outputting buffer..." << std::endl;
      output << meta_out.toJSON().toNetPacked();
      output << prebuffer.rdbuf();
    }
    std::cerr << "Done! If you output this data to a file, don't forget to run MistDTSCFix next." << std::endl;

    return 0;
  } //FLV2DTSC

}

///\brief Entry point for FLV2DTSC, simply calls Converters::FLV2DTSC().
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("output",
      JSON::fromString(
          "{\"long\":\"output\", \"value\":[\"stdout\"], \"short\":\"o\", \"arg\":\"string\", \"help\":\"Name of the outputfile or stdout for standard output.\"}"));
  conf.parseArgs(argc, argv);
  if (conf.getString("output") == "stdout"){
    return Converters::FLV2DTSC(std::cout);
  }
  std::ofstream oFile(conf.getString("output").c_str());
  return Converters::FLV2DTSC(oFile);
} //main
