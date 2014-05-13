/// \file dtsc_analyser.cpp
/// Reads an DTSC file and prints all readable data about it

#include <string>
#include <iostream>
#include <sstream>

#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/config.h>
#include <mist/defines.h>

///\brief Holds everything unique to the analysers.  
namespace Analysers {
  ///\brief Debugging tool for DTSC data.
  ///
  /// Expects DTSC data in a file given on the command line, outputs human-readable information to stderr.
  ///\param conf The configuration parsed from the commandline.
  ///\return The return code of the analyser.
  int analyseDTSC(Util::Config conf){
    DTSC::File F(conf.getString("filename"));
    F.getMeta().toPrettyString(std::cout,0, 0x03);

    int bPos = 0;
    F.seek_bpos(0);
    F.parseNext();
    JSON::Value tmp;
    std::string tmpStr;
    while (F.getPacket()){
      switch (F.getPacket().getVersion()){
        case DTSC::DTSC_V1: {
          std::cout << "DTSCv1 packet: " << F.getPacket().getScan().toPrettyString() << std::endl;
          break;
        }
        case DTSC::DTSC_V2: {
          std::cout << "DTSCv2 packet: " << F.getPacket().getScan().toPrettyString() << std::endl;
          break;
        }
        default:
          DEBUG_MSG(DLVL_WARN,"Invalid dtsc packet @ bpos %d", bPos);
          break;
      }
      std::cout << tmp.toPrettyString() << std::endl;
      bPos = F.getBytePos();
      F.parseNext();
    }
    return 0;
  }
}

/// Reads an DTSC file and prints all readable data about it
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("filename", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"Filename of the DTSC file to analyse.\"}"));
  conf.parseArgs(argc, argv);
  return Analysers::analyseDTSC(conf);
} //main
