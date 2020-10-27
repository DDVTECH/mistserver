#include "analyser.h"
#include <mist/config.h>
#include <mist/ts_packet.h>
#include <fstream>

class AnalyserTS : public Analyser, public Util::DataCallback{
public:
  AnalyserTS(Util::Config &conf);
  ~AnalyserTS();
  bool parsePacket();
  static void init(Util::Config &conf);
  std::string printPES(const std::string &d, unsigned long PID);
  void dataCallback(const char * ptr, size_t size);

private:
  std::ofstream outFile;
  std::map<unsigned long long, std::string> payloads;
  uint32_t pidOnly;
  TS::Packet packet;
  uint64_t bytes;
  Util::ResizeablePointer buffer;
};

