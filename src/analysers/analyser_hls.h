#include "analyser.h"
#include <fstream>
#include <mist/http_parser.h>

class HLSPart{
public:
  HLSPart(const HTTP::URL &u, uint64_t n, float d) : uri(u), no(n), dur(d){}
  HTTP::URL uri;
  uint64_t no;
  float dur;
};

class AnalyserHLS : public Analyser{

public:
  AnalyserHLS(Util::Config &conf);
  bool parsePacket();
  static void init(Util::Config &conf);
  bool isOpen();
  bool open(const std::string &filename);
  void stop();

private:
  std::deque<HLSPart> parts;
  void getParts(const std::string &body);
  HTTP::URL root;
  float hlsTime;
  uint64_t parsedPart;
  uint64_t refreshAt;
  HTTP::Parser H;
  std::string connectedHost;
  uint32_t connectedPort;
  bool download(const HTTP::URL &link);
  Socket::Connection conn;
  std::ofstream reconstruct;
};

