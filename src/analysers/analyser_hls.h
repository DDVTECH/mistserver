#include "analyser.h"
#include <fstream>
#include <mist/http_parser.h>
#include <mist/downloader.h>

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
  std::ofstream reconstruct;
  HTTP::Downloader DL;
};

