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

/// Debugging tool for MP4 data.
/// Expects MP4 data through stdin, outputs human-readable information to stderr.
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.parseArgs(argc, argv);

  std::string temp;
  while (std::cin.good()){
    temp += std::cin.get();
  } //read all of std::cin to temp
  temp.erase(temp.size() - 1, 1); //strip the invalid last character

  MP4::Box mp4data;
  while (mp4data.read(temp)){
    std::cerr << mp4data.toPrettyString(0) << std::endl;
    if (mp4data.isType("mdat")){
      std::ofstream oFile;
      oFile.open("mdat");
      oFile << std::string(mp4data.payload(), mp4data.payloadSize());
      oFile.close();
    }
  }
  return 0;
}

