/// \file flv2dtsc.cpp
/// Contains the code that will transform any valid FLV input into valid DTSC.

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <mist/flv_tag.h>
#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/amf.h>
#include <mist/config.h>

///\brief Holds everything unique to converters.
namespace Converters {

  ///\brief Converts FLV from stdin to DTSC on stdout.
  ///\return The return code for the converter.
  int SRT2DTSC(Util::Config & conf){
    int lineNum;
    int beginH, beginM, beginS, beginMs;
    int endH, endM, endS, endMs;
    char lineBuf[1024];
    std::string myData;
    JSON::Value meta;
    meta["moreheader"] = 0ll;
    meta["tracks"]["track3"]["trackid"] = 3ll;
    meta["tracks"]["track3"]["type"] = "meta";
    meta["tracks"]["track3"]["codec"] = "srt";
    meta["tracks"]["track3"]["language"] = conf.getString("language");
    std::cout << meta.toNetPacked();
    JSON::Value newPack;
    while (std::cin.good()){
      if (scanf( "%d\n%d:%d:%d,%d --> %d:%d:%d,%d\n", &lineNum, &beginH, &beginM, &beginS, &beginMs, &endH, &endM, &endS, &endMs) != 9){
        break;
      }
      while (std::cin.good() && myData.find("\r\n\r\n") == std::string::npos && myData.find("\n\n") == std::string::npos){
        std::cin.getline(lineBuf, 1024);
        myData += std::string(lineBuf) + "\n";
      }
      myData.erase( myData.end() - 1 );
      newPack.null();
      newPack["trackid"] = 3;
      newPack["time"] = (((((beginH * 60) + beginM) * 60) + beginS) * 1000) + beginMs;
      newPack["duration"] = (((((endH - beginH) * 60) + (endM - beginM)) * 60) + (endS - beginS)) * 1000 + (endMs - beginMs);
      newPack["data"] = myData;
      std::cout << newPack.toNetPacked();
      myData = "";
    }
    return 0;
  } //SRT2DTSC

}

///\brief Entry point for SRT2DTSC, simply calls Converters::SRT2DTSC().
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("language",
      JSON::fromString("{\"arg_num\":1,\"value\":[\"?\"], \"help\": \"The language of these subtitles.\"}"));
  conf.parseArgs(argc, argv);
  return Converters::SRT2DTSC(conf);
} //main
