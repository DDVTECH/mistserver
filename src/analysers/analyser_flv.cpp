#include "analyser_flv.h"

void AnalyserFLV::init(Util::Config &conf) {
  Analyser::init(conf);
  JSON::Value opt;
  opt["long"] = "filter";
  opt["short"] = "F";
  opt["arg"] = "num";
  opt["default"] = "0";
  opt["help"] = "Only print information about this tag type (8 = audio, 9 = "
                "video, 18 = meta, 0 = all)";
  conf.addOption("filter", opt);
  opt.null();
}

AnalyserFLV::AnalyserFLV(Util::Config &conf) : Analyser(conf) {
  filter = conf.getInteger("filter");
}

bool AnalyserFLV::parsePacket() {
  unsigned int pos = 0;
  std::string tmp;
  size_t bytesNeeded = 0;

  while (tmp.size() < (bytesNeeded = FLV::bytesNeeded(tmp.data(), tmp.size()))) {
    while (!buffer.available(bytesNeeded)) {
      if (uri.isEOF()) {
        FAIL_MSG("End of file");
        return false;
      }
      uri.readSome(bytesNeeded - buffer.bytes(bytesNeeded), *this);
      if (!buffer.available(bytesNeeded)) {
        Util::sleep(50);
      }
    }
    tmp = buffer.copy(bytesNeeded);
//    WARN_MSG("copying %llu bytes", bytesNeeded);
  }

//  INFO_MSG("removing %llu, bytes from buffer, buffersize: %llu", bytesNeeded, buffer.bytes(0xffffffff));
  buffer.remove(bytesNeeded);

  // skip header
  if (bytesNeeded == 13) {
    return parsePacket();
  }

  //ugly, but memloader needs to be called twice
  if (flvData.MemLoader(tmp.data(), tmp.size(), pos) || flvData.MemLoader(tmp.data(), tmp.size(), pos)) {
    if ((!filter || filter == flvData.data[0]) && !validate) {
      DETAIL_MED("[%" PRIu64 "+%" PRIu64 "] %s", flvData.tagTime(), flvData.offset(),flvData.tagType().c_str());
    }
    mediaTime = flvData.tagTime();
  }
  return true;
}

void AnalyserFLV::dataCallback(const char *ptr, size_t size) {
  mediaDown += size;
//  WARN_MSG("flv add buffer callback, size: %lu", size);
  buffer.append(ptr, size);
}

