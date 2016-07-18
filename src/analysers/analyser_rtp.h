#include <mist/config.h>
#include "analyser.h"
#include <cstdlib>
#include <iomanip>
#include <vector>
#include <sstream>
#include <mist/socket.h>
#include <mist/rtp.h>
#include <mist/http_parser.h>

class rtpAnalyser : public analysers 
{
  Socket::Connection conn;
  HTTP::Parser HTTP_R, HTTP_S;//HTTP Receiver en HTTP Sender.
  int step;
  std::vector<std::string> tracks;
  std::vector<Socket::UDPConnection> connections;
  unsigned int trackIt;

  public:
    rtpAnalyser(Util::Config config);
    bool packetReady();
    void PreProcessing();
    //int Analyse();
    int doAnalyse();
    void doValidate();
};
