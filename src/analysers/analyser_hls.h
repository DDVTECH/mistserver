#include "analyser.h"
#include <fstream>
#include <mist/downloader.h>
#include <mist/http_parser.h>

class HLSPart{
public:
  HLSPart(const HTTP::URL &u, uint64_t n, float d) : uri(u), no(n), dur(d){}
  HTTP::URL uri;
  uint64_t no;
  float dur;
};

class AnalyserHLS : public Analyser, public Util::DataCallback{
public:
  AnalyserHLS(Util::Config &conf);
  bool parsePacket();
  static void init(Util::Config &conf);
  bool isOpen();
//  bool open(const std::string &filename);
  void stop();
  void dataCallback(const char * ptr, size_t size);
  bool readPlaylist(std::string source);
  bool open(const std::string & filename);

private:
  std::deque<HLSPart> parts;
  void getParts(const std::string &body);
  HTTP::URL root;
  float hlsTime;
  uint64_t parsedPart;
  uint64_t refreshAt;
  std::ofstream reconstruct;
  HTTP::Downloader DL;
  Socket::Buffer buffer;
  bool refreshPlaylist;
};
