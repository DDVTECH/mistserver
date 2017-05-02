#include "analyser_flv.h"

void AnalyserFLV::init(Util::Config &conf){
  Analyser::init(conf);
  JSON::Value opt;
  opt["long"] = "filter";
  opt["short"] = "F";
  opt["arg"] = "num";
  opt["default"] = "0";
  opt["help"] =
      "Only print information about this tag type (8 = audio, 9 = video, 18 = meta, 0 = all)";
  conf.addOption("filter", opt);
  opt.null();
}

AnalyserFLV::AnalyserFLV(Util::Config &conf) : Analyser(conf){
  filter = conf.getInteger("filter");
}

bool AnalyserFLV::parsePacket(){
  if (feof(stdin)){
    stop();
    return false;
  }
  while (!feof(stdin)){
    if (flvData.FileLoader(stdin)){break;}
    if (feof(stdin)){
      stop();
      return false;
    }
  }

  // If we arrive here, we've loaded a FLV packet
  if (!filter || filter == flvData.data[0]){
    DETAIL_MED("[%llu+%llu] %s", flvData.tagTime(), flvData.offset(), flvData.tagType().c_str());
  }
  mediaTime = flvData.tagTime();
  return true;
}

