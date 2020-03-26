#include "analyser.h"
#include <mist/mp4.h>

class AnalyserMP4 : public Analyser, public Util::DataCallback{
public:
  AnalyserMP4(Util::Config &conf);
  static void init(Util::Config &conf);
  bool parsePacket();
  void dataCallback(const char * ptr, size_t size);

private:
  uint64_t neededBytes();
  std::string mp4Buffer;
  Socket::Buffer buffer;
  MP4::Box mp4Data;
  uint64_t curPos;
  uint64_t prePos;

};
