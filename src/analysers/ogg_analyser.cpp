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
    OGG::Page oggPage;
    //Read all of std::cin to oggBuffer
    //while stream busy
    while (std::cin.good()){
      for (unsigned int i = 0; (i < 1024) && (std::cin.good()); i++){
        oggBuffer += std::cin.get();
      }
      //while OGG::page check function read
      while (oggPage.read(oggBuffer)){//reading ogg to string
        std::cout << oggPage.toPrettyString() << std::endl;
      }
    }
    return 0;
  }
}

int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.parseArgs(argc, argv);
  return Analysers::analyseOGG();
}

