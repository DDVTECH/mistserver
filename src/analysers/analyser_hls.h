//http://cattop:8080/hls/bunny/index.m3u8 
//
#include <mist/config.h>
#include <mist/timing.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <iostream>
#include "analyser.h"

class HLSPart {
  public:
    HLSPart(std::string u, unsigned int s, unsigned int d) {
      uri = u;
      start = s;
      dur = d;
    }
    std::string uri;
    unsigned int start;
    unsigned int dur;
};


class hlsAnalyser : public analysers 
{
    
  public:
    hlsAnalyser(Util::Config config);
    ~hlsAnalyser();
    bool packetReady();
    void PreProcessing();
    //int Analyse();
    int doAnalyse();
    void doValidate();
    bool hasInput();
    void PostProcessing();

  private:

  unsigned int port;
  std::string url;

  std::string server;
  long long int startTime;
  long long int abortTime;

  std::deque<HLSPart> parts;
  Socket::Connection conn;

  std::string playlist;
  bool repeat;
  std::string lastDown;
  unsigned int pos;
  bool output;

};
