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
#include <mist/timing.h>
#include <sys/sysinfo.h>

///Debugging tool for FLV data.
/// Expects FLV data through stdin, outputs human-readable information to stderr.
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0]);
  conf.addOption("filter", JSON::fromString("{\"arg\":\"num\", \"short\":\"f\", \"long\":\"filter\", \"default\":0, \"help\":\"Only print info about this tag type (8 = audio, 9 = video, 0 = all)\"}"));
  conf.addOption("mode", JSON::fromString("{\"long\":\"mode\", \"arg\":\"string\", \"short\":\"m\", \"default\":\"analyse\", \"help\":\"What to do with the stream. Valid modes are 'analyse', 'validate', 'output'.\"}"));
  conf.parseArgs(argc, argv);
  bool analyse = conf.getString("mode") == "analyse";
  bool validate = conf.getString("mode") == "validate";
  
  long long filter = conf.getInteger("filter");

  FLV::Tag flvData; // Temporary storage for incoming FLV data.
  long long int endTime = 0;
  long long int upTime = Util::bootSecs();
  while ( !feof(stdin)){
    if (flvData.FileLoader(stdin)){
      if (analyse){
        if (!filter || filter == flvData.data[0]){
          std::cout << "[" << flvData.tagTime() << "+" << flvData.offset() << "] " << flvData.tagType() << std::endl;
        }
      }
      endTime = flvData.tagTime();
    }
  }
  long long int finTime = Util::bootSecs();
  if (validate){
    std::cout << upTime << ", " << finTime << ", " << (finTime-upTime) << ", " << endTime << std::endl;
  }
  return 0;
}
