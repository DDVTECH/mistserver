/// \file h264_analyser.cpp
/// Reads an H264 file and prints all readable data about it

#include <string>
#include <iostream>
#include <cstdio>

#include <mist/config.h>
#include <mist/defines.h>
#include <mist/h264.h>
#include <mist/bitfields.h>
#include <mist/bitstream.h>

int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0]);
  conf.addOption("filename", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"Full path of the file to analyse.\"}"));
  conf.parseArgs(argc, argv);
  FILE * F = fopen(conf.getString("filename").c_str(), "r+b");
  if (!F){
    FAIL_MSG("No such file");
  }

  h264::nalUnit * nalPtr = h264::nalFactory(F);
  while (nalPtr){
    nalPtr->toPrettyString(std::cout);
    nalPtr = h264::nalFactory(F);
  }
  return 0;
}

