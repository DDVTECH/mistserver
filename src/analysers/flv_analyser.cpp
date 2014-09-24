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

///Debugging tool for FLV data.
/// Expects FLV data through stdin, outputs human-readable information to stderr.
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("filter", JSON::fromString("{\"arg\":\"num\", \"short\":\"f\", \"long\":\"filter\", \"default\":0, \"help\":\"Only print info about this tag type (8 = audio, 9 = video, 0 = all)\"}"));
  conf.parseArgs(argc, argv);
  
  long long filter = conf.getInteger("filter");

  FLV::Tag flvData; // Temporary storage for incoming FLV data.
  while ( !feof(stdin)){
    if (flvData.FileLoader(stdin)){
      if (!filter || filter == flvData.data[0]){
        std::cout << "[" << flvData.tagTime() << "+" << flvData.offset() << "] " << flvData.tagType() << std::endl;
      }
    }
  }
  return 0;
}
