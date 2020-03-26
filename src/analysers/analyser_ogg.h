#include "analyser.h"
#include <mist/ogg.h>

class AnalyserOGG : public Analyser, public Util::DataCallback{
public:
  AnalyserOGG(Util::Config &conf);
  bool parsePacket();
  static void init(Util::Config &conf);
  void dataCallback(const char * ptr, size_t size);

private:
  std::map<uint64_t, std::string> sn2Codec;
  std::string oggBuffer;
  OGG::Page oggPage;
  int kfgshift;
  Socket::Buffer buffer;
};
