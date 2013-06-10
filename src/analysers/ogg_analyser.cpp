#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <string.h>
#include <mist/ogg.h>
#include <mist/config.h>

namespace Analysers{
  int analyseOGG(){
    std::string oggBuffer;
    //Read all of std::cin to oggBuffer
    while (std::cin.good()){
      oggBuffer += std::cin.get();
    }
    oggBuffer.erase(oggBuffer.size() - 1, 1);

    OGG::Page oggData;
    while (oggData.read(oggBuffer)){
      std::cerr << oggData.toPrettyString() << std::endl;
    }
    return 0;
  }
}

int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.parseArgs(argc, argv);
  return Analysers::analyseOGG();
}

