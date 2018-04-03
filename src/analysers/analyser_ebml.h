#include "analyser.h"
#include <deque>

class AnalyserEBML : public Analyser{
public:
  AnalyserEBML(Util::Config &conf);
  static void init(Util::Config &conf);
  bool parsePacket();

private:
  uint64_t neededBytes();
  std::string dataBuffer;
  uint64_t curPos;
  uint64_t prePos;
  uint64_t segmentOffset;
  uint32_t lastSeekId;
  uint64_t lastSeekPos;
  std::map<uint32_t, uint64_t> seekChecks;
  std::deque<uint64_t> depthStash;///<Contains bytes to read to go up a level in the element depth.
};

