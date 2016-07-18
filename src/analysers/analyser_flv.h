#include <mist/flv_tag.h> //FLV support
#include <mist/config.h>
#include "analyser.h"

class flvAnalyser : public analysers 
{
  FLV::Tag flvData;
  long long filter;
  
  public:
    flvAnalyser(Util::Config config);
    bool packetReady();
    void PreProcessing();
    //int Analyse();
    int doAnalyse();
    bool hasInput();
//    void doValidate();
};
