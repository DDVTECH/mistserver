#include "analyser.h"
#include <iostream>
#include <mist/timing.h>
#include <mist/http_parser.h>
#include <mist/urireader.h>
#include <mist/checksum.h>

/// Reads configuration and opens a passed filename replacing standard input if needed.
Analyser::Analyser(Util::Config &conf){
  validate = conf.getBool("validate");
  detail = conf.getInteger("detail");
  timeOut = conf.getInteger("timeout") *1000;
  leeway = conf.getInteger("leeway");
  mediaTime = 0;
  upTime = Util::bootMS();
  isActive = &conf.is_active;
  firstMediaTime=0;
  firstMediaBootTime=0;
  measureInterval = timeOut / 300;
  if (measureInterval < 1000){measureInterval = 1000;}
}

///Opens the filename. Supports stdin and plain files.
bool Analyser::open(const std::string & filename){
  uri.userAgentOverride = APPIDENT " - Load Tester " + JSON::Value(getpid()).asString();
  std::string sidAsString = uri.userAgentOverride + JSON::Value(getpid()).asString();
  uri.sidOverride = JSON::Value(checksum::crc32(0, sidAsString.data(), sidAsString.size())).asString();
  uri.open(filename);
  return true;
}

/// Stops analysis by closing the standard input
void Analyser::stop(){
  uri.close();
}


void Analyser::stopReason(const std::string & reason){
  if (!reasonForStop.size()){reasonForStop = reason;}
}

/// Prints validation message if needed
Analyser::~Analyser(){
  if (validate){
    finTime = Util::bootMS();
    JSON::Value out;
    out["system_start"] = upTime;
    out["system_stop"] = finTime;
    out["system_firstmedia"] = firstMediaBootTime;
    out["media_start"] = firstMediaTime;
    out["media_stop"] = mediaTime;
    if (reasonForStop.size()){out["error"] = reasonForStop;}
    for (std::map<uint64_t, uint64_t>::iterator it = mediaTimes.begin(); it != mediaTimes.end(); ++it){
      JSON::Value val;
      val.append(it->first);
      val.append(it->second);
      val.append(mediaBytes[it->first]);
      out["media"].append(val);

    }
    std::cout << out.toString() << std::endl;
  }
}

/// Checks if standard input is still valid.
bool Analyser::isOpen(){
  return (*isActive) && !uri.isEOF();
}

/// Main loop for all analysers. Reads packets while not interrupted, parsing and/or printing them.
int Analyser::run(Util::Config &conf){
  isActive = &conf.is_active;
  if (!open(conf.getString("filename"))){return 1;}
  while (isOpen()){
    if (!parsePacket()){
      if (isOpen()){
        stopReason("media parse error");
        FAIL_MSG("Could not parse packet!");
        return 1;
      }
      stopReason("reached end of file/stream");
      MEDIUM_MSG("Reached end of file/stream");
      return 0;
    }
    if (validate){
      finTime = Util::bootMS();
      if (mediaTime){
        if (!mediaTimes.size() || finTime > mediaTimes.rbegin()->first + measureInterval){
          mediaTimes[finTime] = mediaTime;
          mediaBytes[finTime] = mediaDown;
        }
      }
      if(mediaTime && !firstMediaBootTime){
        firstMediaBootTime = finTime;
        firstMediaTime = mediaTime;
      }
      if (firstMediaBootTime && finTime - upTime < timeOut){
        // slow down to no faster than real-time speed + 10s
        if ((finTime - firstMediaBootTime + 10000) < mediaTime - firstMediaTime){
          uint32_t sleepMs = (mediaTime - firstMediaTime) - (finTime - firstMediaBootTime + 10000);
          if (finTime - upTime + sleepMs > timeOut){
            sleepMs = timeOut - (finTime - upTime);
          }
          DONTEVEN_MSG("Sleeping for %" PRIu32 "ms", sleepMs);
          Util::sleep(sleepMs);
        }
        //Stop analysing when too far behind real-time speed
        if ((finTime - firstMediaBootTime) > (mediaTime - firstMediaTime) + leeway){
          stopReason("fell too far behind");
          FAIL_MSG("Media time %" PRIu64 " ms behind!", (finTime - firstMediaBootTime) - (mediaTime - firstMediaTime));
          return 4;
        }
      }

      if ((finTime - upTime ) >= timeOut){
        MEDIUM_MSG("Reached timeout of %" PRIu64 " seconds, stopping", timeOut/1000);
        break;
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
  opt["default"] = 30;
  opt["help"] = "Time out after X seconds of processing/retrieving";
  conf.addOption("timeout", opt);
  opt.null();

  opt["long"] = "detail";
  opt["short"] = "D";
  opt["arg"] = "num";
  opt["default"] = 2;
  opt["help"] = "Detail level for analysis (0 = none, 2 = default, 10 = max)";
  conf.addOption("detail", opt);

  opt["long"] = "leeway";
  opt["short"] = "L";
  opt["arg"] = "num";
  opt["default"] = 7500;
  opt["help"] = "How far behind live playback will we allow before failing, in milliseconds";
  conf.addOption("leeway", opt);
  opt.null();
}
