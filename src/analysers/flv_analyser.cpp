/// \file flv_analyser.cpp
/// Contains the code for the FLV Analysing tool.

#include <fcntl.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <string.h>
#include <fstream>
#include <unistd.h>
#include <signal.h>
#include <mist/flv_tag.h> //FLV support
#include <mist/config.h>

///\brief Holds everything unique to the analysers.
namespace Analysers {
  ///\brief Debugging tool for FLV data.
  ///
  /// Expects FLV data through stdin, outputs human-readable information to stderr.
  ///\return The return code of the analyser.
  int analyseFLV(){
    FLV::Tag flvData; // Temporary storage for incoming FLV data.
    while ( !feof(stdin)){
      if (flvData.FileLoader(stdin)){
        std::cout << "Tag: " << flvData.tagType() << "\n\tTime: " << flvData.tagTime() << std::endl;
      }
    }
    return 0;
  }
}

int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.parseArgs(argc, argv);
  return Analysers::analyseFLV();
}
