#include "analyser.h"

class AnalyserRIFF : public Analyser, public Util::DataCallback{
public:
  AnalyserRIFF(Util::Config &conf);
  static void init(Util::Config &conf);
  bool parsePacket();
  void dataCallback(const char * ptr, size_t size);
private:
  uint64_t neededBytes();
  std::string dataBuffer;
  Socket::Buffer buffer;
  uint64_t curPos;
  uint64_t prePos;
};
