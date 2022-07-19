#include "analyser.h"
#include <mist/dtsc.h>

class AnalyserDTSC : public Analyser, public Util::DataCallback{
public:
  AnalyserDTSC(Util::Config &conf);
  bool parsePacket();
  static void init(Util::Config &conf);
  virtual bool open(const std::string &filename);
  void dataCallback(const char * ptr, size_t size);
  uint64_t neededBytes();
private:
  bool headLess;
  bool sizePrepended;
  DTSC::Packet P;
  Socket::Connection conn;
  Socket::Buffer buffer;
  uint64_t totalBytes;
};
