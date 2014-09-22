/// \file mp4_analyser.cpp
/// Debugging tool for MP4 data.
/// Expects MP4 data through stdin, outputs human-readable information to stderr.

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <string.h>
#include <mist/mp4.h>
#include <mist/config.h>
#include <mist/defines.h>

///\brief Holds everything unique to the analysers.
namespace Analysers {
  ///\brief Debugging tool for MP4 data.
  ///
  /// Expects MP4 data through stdin, outputs human-readable information to stderr.
  ///\return The return code of the analyser.
  int analyseMP4(){
    std::string mp4Buffer;
    //Read all of std::cin to mp4Buffer
    while (std::cin.good()){
      mp4Buffer += std::cin.get();
    }
    mp4Buffer.erase(mp4Buffer.size() - 1, 1);

    MP4::Box mp4Data;
    int dataSize = mp4Buffer.size();
    int curPos = 0;
    while (mp4Data.read(mp4Buffer)){
      DEBUG_MSG(DLVL_DEVEL, "Read a box at position %d", curPos);
      std::cerr << mp4Data.toPrettyString(0) << std::endl;
      curPos += dataSize - mp4Buffer.size();
      dataSize = mp4Buffer.size();
    }
    DEBUG_MSG(DLVL_DEVEL, "Stopped parsing at position %d", curPos);
    return 0;
  }
}

/// Debugging tool for MP4 data.
/// Expects MP4 data through stdin, outputs human-readable information to stderr.
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.parseArgs(argc, argv);
  return Analysers::analyseMP4();
}

