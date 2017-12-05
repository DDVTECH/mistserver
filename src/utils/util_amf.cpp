/// \file util_amf.cpp
/// Debugging tool for AMF data.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mist/amf.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/util.h>
#include <string>

int main(int argc, char **argv){
  Util::redirectLogsIfNeeded();
  Util::Config conf(argv[0]);
  JSON::Value opt;
  opt["arg_num"] = 1ll;
  opt["arg"] = "string";
  opt["default"] = "-";
  opt["help"] = "Filename to analyse, or - for standard input (default)";
  conf.addOption("filename", opt);
  conf.parseArgs(argc, argv);

  std::string filename = conf.getString("filename");
  if (filename.size() && filename != "-"){
    int fp = open(filename.c_str(), O_RDONLY);
    if (fp <= 0){
      FAIL_MSG("Cannot open '%s': %s", filename.c_str(), strerror(errno));
      return 1;
    }
    dup2(fp, STDIN_FILENO);
    close(fp);
    INFO_MSG("Parsing %s...", filename.c_str());
  }else{
    INFO_MSG("Parsing standard input...");
  }

  std::string amfBuffer;
  // Read all of std::cin to amfBuffer
  while (std::cin.good()){amfBuffer += std::cin.get();}
  // Strip the invalid last character
  amfBuffer.erase((amfBuffer.end() - 1));
  // Parse into an AMF::Object
  AMF::Object amfData = AMF::parse(amfBuffer);
  // Print the output.
  std::cout << amfData.Print() << std::endl;
  return 0;
}

