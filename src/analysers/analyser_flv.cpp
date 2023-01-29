#include "analyser_flv.h"

#include <sstream>

void AnalyserFLV::init(Util::Config & conf) {
  Analyser::init(conf);
  JSON::Value opt;
  opt["long"] = "filter";
  opt["short"] = "F";
  opt["arg"] = "num";
  opt["default"] = "0";
  opt["help"] = "Only print information about this tag type (8 = audio, 9 = video, 18 = meta, 0 = all)";
  conf.addOption("filter", opt);
  opt.null();
}

AnalyserFLV::AnalyserFLV(Util::Config & conf) : Analyser(conf) {
  filter = conf.getInteger("filter");
}

bool AnalyserFLV::parsePacket() {
  if (feof(stdin)) {
    stop();
    return false;
  }
  uint64_t currPos = Util::ftell(stdin);
  while (!feof(stdin)) {
    if (flvData.FileLoader(stdin)) { break; }
    if (feof(stdin)) {
      stop();
      return false;
    }
  }

  // If we arrive here, we've loaded a FLV packet
  if (!filter || filter == flvData.data[0]) {
    DETAIL_MED("[%" PRIu64 "+%" PRId32 " @%" PRIu64 "b] %s", flvData.tagTime(), flvData.offset(), currPos,
               flvData.tagType().c_str());
    if (detail >= 10) {
      std::stringstream res;
      Util::hexDump(res, flvData.getData(), flvData.getDataLen(), 2);
      DETAIL_XTR("Raw data:%s", res.str().c_str())
    }
  }
  mediaTime = flvData.tagTime();
  return true;
}
