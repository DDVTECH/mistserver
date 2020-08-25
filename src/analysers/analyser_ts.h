#include "analyser.h"
#include <mist/config.h>
#include <mist/ts_packet.h>
#include <fstream>

class AnalyserTS : public Analyser{
public:
  AnalyserTS(Util::Config &conf);
  ~AnalyserTS();
  bool parsePacket();
  static void init(Util::Config &conf);
  std::string printPES(const std::string &d, size_t PID);

private:
  std::ofstream outFile;
  std::map<size_t, std::string> payloads;
  size_t pidOnly;
  TS::Packet packet;
  uint64_t bytes;
};
