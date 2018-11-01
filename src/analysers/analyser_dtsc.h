#include "analyser.h"
#include <mist/dtsc.h>

class AnalyserDTSC : public Analyser{
public:
  AnalyserDTSC(Util::Config &conf);
  bool parsePacket();
  static void init(Util::Config &conf);

private:
  bool headLess;
  DTSC::Packet P;
  Socket::Connection conn;
  uint64_t totalBytes;
};

