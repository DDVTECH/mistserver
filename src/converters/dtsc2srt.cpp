/// \file dtscfix.cpp
/// Contains the code that will attempt to fix the metadata contained in an DTSC file.

#include <string>
#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/config.h>
#include <iomanip>
#include <iostream>

///\brief Holds everything unique to converters.
namespace Converters {
  ///\brief Reads a DTSC file and attempts to fix the metadata in it.
  ///\param conf The current configuration of the program.
  ///\return The return code for the fixed program.
  int DTSCFix(Util::Config & conf){
    DTSC::File F(conf.getString("filename"));

    int curIndex = 1;

    F.parseNext();
    while ( !F.getJSON().isNull()){
      std::cout << curIndex++ << std::endl;
      long long unsigned int time = F.getJSON()["time"].asInt();
      std::cout << std::setfill('0') << std::setw(2) << (time / 3600000) << ":";
      std::cout << std::setfill('0') << std::setw(2) <<  ((time % 3600000) / 60000) << ":";
      std::cout << std::setfill('0') << std::setw(2) << (((time % 3600000) % 60000) / 1000) << ",";
      std::cout << std::setfill('0') << std::setw(3) << time % 1000 << " --> ";
      time += F.getJSON()["duration"].asInt();
      std::cout << std::setfill('0') << std::setw(2) << (time / 3600000) << ":";
      std::cout << std::setfill('0') << std::setw(2) <<  ((time % 3600000) / 60000) << ":";
      std::cout << std::setfill('0') << std::setw(2) << (((time % 3600000) % 60000) / 1000) << ",";
      std::cout << std::setfill('0') << std::setw(3) << time % 1000 << std::endl;
      std::cout << F.getJSON()["data"].asString() << std::endl;
      F.parseNext();
    }
    return 0;

  } //DTSCFix

}

/// Entry point for DTSCFix, simply calls Converters::DTSCFix().
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("filename", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"Filename of the file to attempt to fix.\"}"));
  conf.parseArgs(argc, argv);
  return Converters::DTSCFix(conf);
} //main
