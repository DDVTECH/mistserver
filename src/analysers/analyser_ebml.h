#include "analyser.h"
#include <deque>

class AnalyserEBML : public Analyser, public Util::DataCallback{
public:
  AnalyserEBML(Util::Config &conf);
  static void init(Util::Config &conf);
  bool parsePacket();
  void dataCallback(const char * ptr, size_t size);

private:
  uint64_t neededBytes();
  std::string dataBuffer;
  Socket::Buffer buffer;
  uint64_t curPos;
  size_t prePos;
  uint64_t segmentOffset;
  uint32_t lastSeekId;
  uint64_t lastSeekPos;
  std::map<uint32_t, uint64_t> seekChecks;
  std::deque<uint64_t> depthStash; ///< Contains bytes to read to go up a level in the element depth.
  uint64_t lastClusterTime;
};
