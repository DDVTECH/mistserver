#include "analyser.h"
#include <iostream>
#include <mist/timing.h>
#include <mist/http_parser.h>
#include <mist/urireader.h>


/// Reads configuration and opens a passed filename replacing standard input if needed.
Analyser::Analyser(Util::Config &conf){
  validate = conf.getBool("validate");
  detail = conf.getInteger("detail");
  timeOut = conf.getInteger("timeout") *1000;
  mediaTime = 0;
  upTime = Util::bootMS();
  isActive = &conf.is_active;
  firstMediaTime=0;
  firstMediaBootTime=0;
}

///Opens the filename. Supports stdin and plain files.
bool Analyser::open(const std::string & filename){
  uri.open(filename);
  uriSource = filename;
  return true;
}

/// Stops analysis by closing the standard input
void Analyser::stop(){
  close(STDIN_FILENO);
  std::cin.setstate(std::ios_base::eofbit);
}

/// Prints validation message if needed
Analyser::~Analyser(){
  if (validate){
    finTime = Util::bootMS();
    std::cout << upTime << ", " << finTime << ", " << (finTime - upTime) << ", " << mediaTime << ", " << firstMediaTime << ", " << firstMediaBootTime  << std::endl;
  }
}

/// Checks if standard input is still valid.
bool Analyser::isOpen(){
  return (*isActive) && std::cin.good();
}

/// Main loop for all analysers. Reads packets while not interrupted, parsing and/or printing them.
int Analyser::run(Util::Config &conf){
  isActive = &conf.is_active;
  if (!open(conf.getString("filename"))){return 1;}

  while (conf.is_active && isOpen()){
    if (!parsePacket()){
      if (isOpen()){
        FAIL_MSG("Could not parse packet!");
        return 1;
      }
      INFO_MSG("Reached end of file");
      return 0;
    }
    if (validate){
      finTime = Util::bootMS();
      if(firstMediaBootTime == 0){
        firstMediaBootTime = finTime;
        firstMediaTime = mediaTime;
      }else{
        // slow down to realtime + 10s
        if (validate && ((finTime - firstMediaBootTime + 10000) < mediaTime - firstMediaTime)){
          uint32_t sleepMs = (mediaTime - firstMediaTime) - (finTime - firstMediaBootTime + 10000);
          if ((finTime - firstMediaBootTime  + sleepMs / 1000) >= timeOut){
            WARN_MSG("Reached timeout of %" PRIu64 " seconds, stopping", timeOut/1000);
            return 3;
          }
          DONTEVEN_MSG("Sleeping for %" PRIu32 "ms", sleepMs);
          Util::sleep(sleepMs);
          finTime = Util::bootMS();
        }

        if ((finTime - firstMediaBootTime) > (mediaTime - firstMediaTime) + 2000){
          FAIL_MSG("Media time more than 2 seconds behind!");
          FAIL_MSG("fintime: %" PRIu64 " , upTime: %" PRIu64 ", mediatime: %" PRIu64 ", fin-up: %" PRIu64 ", mediatime/1000: %" PRIu64 ", mediatime: %" PRIu64, finTime, upTime, mediaTime, (finTime - upTime), (mediaTime /1000) +2, mediaTime);
          return 4;
        }
        // slow down to realtime + 10s
        if (validate && ((finTime - upTime + 10) * 1000 < mediaTime)){
          uint32_t sleepMs = mediaTime - (Util::bootSecs() - upTime + 10) * 1000;
          if ((finTime - upTime + sleepMs / 1000) >= timeOut){
            WARN_MSG("Reached timeout of %" PRIu64 " seconds, stopping", timeOut);
            return 3;
          }

          if ((finTime - firstMediaBootTime) > (mediaTime - firstMediaTime) + 2000){
            FAIL_MSG("Media time more than 2 seconds behind!");
            //FAIL_MSG("fintime: %llu, upTime: %llu, mediatime: %llu, fin-up: %llu, mediatime/1000: %llu, mediatime: %llu", finTime, upTime, mediaTime, (finTime - upTime), (mediaTime /1000) +2, mediaTime);
            return 4;

          }
        }
        if ((finTime - firstMediaBootTime ) >= timeOut){
          MEDIUM_MSG("Reached timeout of %" PRIu64 " seconds, stopping", timeOut/1000);
          return 3;
        }
      }
    }
  }
  return 0;
}

/// Sets options common to all analysers.
/// Should generally be called by the init function of each analyser.
void Analyser::init(Util::Config &conf){
  JSON::Value opt;

  opt["arg_num"] = 1;
  opt["arg"] = "string";
  opt["default"] = "-";
  opt["help"] = "Filename to analyse, or - for standard input (default)";
  conf.addOption("filename", opt);
  opt.null();

  opt["long"] = "validate";
  opt["short"] = "V";
  opt["help"] = "Enable validation mode (default off)";
  conf.addOption("validate", opt);
  opt.null();

  opt["long"] = "timeout";
  opt["short"] = "T";
  opt["arg"] = "num";
  opt["default"] = 0;
  opt["help"] = "Time out after X seconds of processing/retrieving";
  conf.addOption("timeout", opt);
  opt.null();

  opt["long"] = "detail";
  opt["short"] = "D";
  opt["arg"] = "num";
  opt["default"] = 2;
  opt["help"] = "Detail level for analysis (0 = none, 2 = default, 10 = max)";
  conf.addOption("detail", opt);
  opt.null();
}
