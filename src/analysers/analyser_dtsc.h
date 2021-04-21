#include "analyser.h"
#include <mist/dtsc.h>

class AnalyserDTSC : public Analyser{
public:
  AnalyserDTSC(Util::Config &conf);
  bool parsePacket();
  static void init(Util::Config &conf);
  virtual bool open(const std::string &filename);

private:
  bool headLess;
  bool sizePrepended;
  DTSC::Packet P;
  Socket::Connection conn;
  uint64_t totalBytes;
};
