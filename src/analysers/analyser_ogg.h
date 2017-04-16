#include "analyser.h"
#include <mist/ogg.h>

class AnalyserOGG : public Analyser{
public:
  AnalyserOGG(Util::Config &conf);
  bool parsePacket();
  static void init(Util::Config &conf);

private:
  std::map<int, std::string> sn2Codec;
  std::string oggBuffer;
  OGG::Page oggPage;
  int kfgshift;
};

