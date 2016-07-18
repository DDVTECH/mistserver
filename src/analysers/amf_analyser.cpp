/// \file amf_analyser.cpp
/// Debugging tool for AMF data.
/// Expects AMF data through stdin, outputs human-readable information to stderr.

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <mist/amf.h>
#include <mist/config.h>
#include <mist/defines.h>

///\brief Holds everything unique to the analysers.
namespace Analysers {
  ///\brief Debugging tool for AMF data.
  ///
  /// Expects AMF data through stdin, outputs human-readable information to stderr.
  ///\return The return code of the analyser.
  int analyseAMF(Util::Config conf){
  
    std::string filename = conf.getString("filename");
    
    if(filename.length() > 0){
      int fp = open(filename.c_str(), O_RDONLY);
    
      if(fp <= 0){
        FAIL_MSG("Cannot open file: %s",filename.c_str());
        return false;
      }

      dup2(fp, STDIN_FILENO);
      close(fp);
    }
    
    std::string amfBuffer;
    //Read all of std::cin to amfBuffer
    while (std::cin.good()){
      amfBuffer += std::cin.get();
    }
    //Strip the invalid last character
    amfBuffer.erase((amfBuffer.end() - 1));
    //Parse into an AMF::Object
    AMF::Object amfData = AMF::parse(amfBuffer);
    //Print the output.
    std::cerr << amfData.Print() << std::endl;
    return 0;
  }
}

int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0]);
  conf.addOption("filename", JSON::fromString( "{\"arg_num\":1, \"arg\":\"string\", \"default\":\"\", \"help\":\"Filename of the AMF file to analyse.\"}"));
  conf.parseArgs(argc, argv);
  
  return Analysers::analyseAMF(conf);
}

