#include "analyser.h"
#include <mist/ogg.h>

class AnalyserOGG : public Analyser{
public:
  AnalyserOGG(Util::Config &conf);
  bool parsePacket();
  static void init(Util::Config &conf);

private:
  std::map<uint64_t, std::string> sn2Codec;
  std::string oggBuffer;
  OGG::Page oggPage;
  uint16_t kfgshift;
};
