#include "analyser.h"

class AnalyserRIFF : public Analyser{
public:
  AnalyserRIFF(Util::Config &conf);
  static void init(Util::Config &conf);
  bool parsePacket();

private:
  uint64_t neededBytes();
  std::string dataBuffer;
  uint64_t curPos;
  uint64_t prePos;
};

