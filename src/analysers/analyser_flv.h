#include "analyser.h"
#include <mist/flv_tag.h> //FLV support

class AnalyserFLV : public Analyser{
public:
  AnalyserFLV(Util::Config &conf);
  bool parsePacket();
  static void init(Util::Config &conf);

private:
  FLV::Tag flvData;
  int64_t filter;
};
