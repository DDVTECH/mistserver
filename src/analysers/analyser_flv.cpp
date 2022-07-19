#include "analyser_flv.h"

void AnalyserFLV::init(Util::Config &conf){
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

AnalyserFLV::AnalyserFLV(Util::Config &conf) : Analyser(conf){
  filter = conf.getInteger("filter");
}

bool AnalyserFLV::parsePacket(){
  unsigned int pos = 0;
  std::string tmp;
  size_t bytesNeeded = 0;

  while (buffer.size() < (bytesNeeded = FLV::bytesNeeded(buffer, buffer.size()))){
    if (uri.isEOF()){
      FAIL_MSG("End of file");
      return false;
    }
    uri.readSome(bytesNeeded - buffer.size(), *this);
    if (buffer.size() < bytesNeeded){Util::sleep(50);}
  }

  // skip header
  if (bytesNeeded == 13){
    buffer.pop(13);
    return parsePacket();
  }

  //ugly, but memloader needs to be called twice
  if (flvData.MemLoader(buffer, buffer.size(), pos) || flvData.MemLoader(buffer, buffer.size(), pos)){
    if ((!filter || filter == flvData.data[0]) && !validate){
      DETAIL_MED("[%" PRIu64 "+%" PRIu64 "] %s", flvData.tagTime(), flvData.offset(),flvData.tagType().c_str());
    }
    mediaTime = flvData.tagTime();
  }
  buffer.pop(bytesNeeded);
  return true;
}

void AnalyserFLV::dataCallback(const char *ptr, size_t size){
  mediaDown += size;
  buffer.append(ptr, size);
}
