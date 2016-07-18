#include <mist/config.h>
#include "analyser.h"
#include <mist/dtsc.h>

class dtscAnalyser : public analysers 
{
  DTSC::Packet F;
  Socket::Connection conn;
  uint64_t totalBytes;

  public:
    dtscAnalyser(Util::Config config);
    bool packetReady();
    bool hasInput();
    void PreProcessing();
    //int Analyse();
    int doAnalyse();
//    void doValidate();
};
