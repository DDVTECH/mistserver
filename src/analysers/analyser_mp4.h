#include "analyser.h"
#include <mist/mp4.h>

class AnalyserMP4 : public Analyser{
public:
  AnalyserMP4(Util::Config &conf);
  static void init(Util::Config &conf);
  bool parsePacket();

private:
  uint64_t neededBytes();
  std::string mp4Buffer;
  MP4::Box mp4Data;
  uint64_t curPos;
  uint64_t prePos;
};

