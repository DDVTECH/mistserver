#include "analyser.h"

class AnalyserH264 : public Analyser{
public:
  AnalyserH264(Util::Config &conf);
  static void init(Util::Config &conf);
  bool parsePacket();

private:
  std::string dataBuffer;
  uint64_t prePos, curPos;
  uint64_t neededBytes();
  bool sizePrepended;
};

