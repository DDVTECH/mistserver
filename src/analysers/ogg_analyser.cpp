#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <string.h>
#include <mist/ogg.h>
#include <mist/config.h>
#include <mist/theora.h>

namespace Analysers{
  int analyseOGG(){
    std::map<int,std::string> sn2Codec;
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
        oggPage.setInternalCodec("");
        if (oggPage.getHeaderType() & 0x02){
          if (memcmp("theora",oggPage.getFullPayload() + 1,6) == 0){
            sn2Codec[oggPage.getBitstreamSerialNumber()] = "theora";
          }
        }
        oggPage.setInternalCodec(sn2Codec[oggPage.getBitstreamSerialNumber()]);
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

