#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <string.h>
#include <mist/dtsc.h>
#include <mist/ogg.h>
#include <mist/config.h>
#include <mist/json.h>

namespace Converters{
  int OGG2DTSC(){
    std::string oggBuffer;
    OGG::Page oggPage;
    //netpacked
    //Read all of std::cin to oggBuffer
    
    //while stream busy
    while (std::cin.good()){
      for (unsigned int i; (i < 1024) && (std::cin.good()); i++){
        oggBuffer += std::cin.get();
      }
      //while OGG::page check functie{ read
      while (oggPage.read(oggBuffer)){//reading ogg to string
        //ogg page 2 DTSC packet
        std::cout << oggPage.typeBOS();
        std::cout << "inner" << std::endl;
      }
      //std::cout << "outer" << std::endl;
    }
    return 0;
  }
}

int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.parseArgs(argc, argv);
  return Converters::OGG2DTSC();
}
