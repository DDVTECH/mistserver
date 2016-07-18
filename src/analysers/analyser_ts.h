#include <mist/config.h>
#include <mist/ts_packet.h>
#include "analyser.h"

class tsAnalyser : public analysers 
{
  long long filter;
  
  std::map<unsigned long long, std::string> payloads;
  TS::Packet packet;
  long long int upTime;
  int64_t pcr;
  unsigned int bytes;
  char packetPtr[188];
  int detailLevel;
  int incorrectPacket;

  public:
    tsAnalyser(Util::Config config);
    ~tsAnalyser();
    bool packetReady();
    void PreProcessing();
    //int Analyse();
    int doAnalyse();
    void doValidate();
    std::string printPES(const std::string & d, unsigned long PID, int detailLevel);
};
