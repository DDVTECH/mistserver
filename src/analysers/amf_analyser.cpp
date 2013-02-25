/// \file amf_analyser.cpp
/// Debugging tool for AMF data.
/// Expects AMF data through stdin, outputs human-readable information to stderr.

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <mist/amf.h>
#include <mist/config.h>

/// Debugging tool for AMF data.
/// Expects AMF data through stdin, outputs human-readable information to stderr.
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.parseArgs(argc, argv);
  std::string temp;
  while (std::cin.good()){
    temp += std::cin.get();
  } //read all of std::cin to temp
  temp.erase(temp.size() - 1, 1); //strip the invalid last character
  AMF::Object amfdata = AMF::parse(temp); //parse temp into an AMF::Object
  std::cerr << amfdata.Print() << std::endl; //pretty-print the object
  return 0;
}

