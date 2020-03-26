#include "analyser.h"

class AnalyserH264 : public Analyser, public Util::DataCallback{
public:
  AnalyserH264(Util::Config &conf);
  static void init(Util::Config &conf);
  bool parsePacket();
  void dataCallback(const char * ptr, size_t size);

private:
  std::string dataBuffer;
  uint64_t prePos, curPos;
  uint64_t neededBytes();
  Socket::Buffer buffer;
  bool sizePrepended;
};
