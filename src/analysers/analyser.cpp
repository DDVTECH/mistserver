#include "analyser.h"
#include <iostream>
#include <mist/timing.h>

/// Reads configuration and opens a passed filename replacing standard input if needed.
Analyser::Analyser(Util::Config &conf){
  detail = conf.getInteger("detail");
  mediaTime = 0;
  upTime = Util::bootSecs();
  isActive = &conf.is_active;
}

///Opens the filename. Supports stdin and plain files.
bool Analyser::open(const std::string & filename){
  if (filename.size() && filename != "-"){
    int fp = ::open(filename.c_str(), O_RDONLY);
    if (fp <= 0){
      FAIL_MSG("Cannot open '%s': %s", filename.c_str(), strerror(errno));
      return false;
    }
    dup2(fp, STDIN_FILENO);
    close(fp);
    INFO_MSG("Parsing %s...", filename.c_str());
  }else{
    INFO_MSG("Parsing standard input...");
  }
  return true;
}

/// Stops analysis by closing the standard input
void Analyser::stop(){
  close(STDIN_FILENO);
  std::cin.setstate(std::ios_base::eofbit);
}

///Checks if standard input is still valid.
bool Analyser::isOpen(){
  return (*isActive) && std::cin.good();
}

/// Main loop for all analysers. Reads packets while not interrupted, parsing and/or printing them.
int Analyser::run(Util::Config &conf){
  isActive = &conf.is_active;
  if (!open(conf.getString("filename"))){
    return 1;
  }
  while (conf.is_active && isOpen()){
    if (!parsePacket()){
      if (isOpen()){
        FAIL_MSG("Could not parse packet!");
        return 1;
      }
      INFO_MSG("Reached end of file");
      return 0;
    }
  }
  return 0;
}

/// Sets options common to all analysers.
/// Should generally be called by the init function of each analyser.
void Analyser::init(Util::Config &conf){
  JSON::Value opt;

  opt["arg_num"] = 1ll;
  opt["arg"] = "string";
  opt["default"] = "-";
  opt["help"] = "Filename to analyse, or - for standard input (default)";
  conf.addOption("filename", opt);
  opt.null();

  opt["long"] = "detail";
  opt["short"] = "D";
  opt["arg"] = "num";
  opt["default"] = 2ll;
  opt["help"] = "Detail level for analysis (0 = none, 2 = default, 10 = max)";
  conf.addOption("detail", opt);
  opt.null();
}

