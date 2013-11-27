/// \file dtsc_analyser.cpp
/// Reads an DTSC file and prints all readable data about it

#include <string>
#include <iostream>

#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/config.h>

///\brief Holds everything unique to the analysers.  
namespace Analysers {
  ///\brief Debugging tool for DTSC data.
  ///
  /// Expects DTSC data in a file given on the command line, outputs human-readable information to stderr.
  ///\param conf The configuration parsed from the commandline.
  ///\return The return code of the analyser.
  int analyseDTSC(Util::Config conf){
    DTSC::File F(conf.getString("filename"));
    std::cout << F.getMeta().toJSON().toPrettyString() << std::endl;

    F.parseNext();
    while (F.getJSON()){
      std::cout << F.getJSON().toPrettyString() << std::endl;
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
