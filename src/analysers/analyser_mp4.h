#include <mist/config.h>
#include <mist/mp4.h>
#include "analyser.h"


class mp4Analyser : public analysers 
{
  std::string mp4Buffer;
  MP4::Box mp4Data;
  int dataSize;
  int curPos;
    
  public:
    mp4Analyser(Util::Config config);
    ~mp4Analyser();
    bool packetReady();
    void PreProcessing();
    //int Analyse();
    int doAnalyse();
//    void doValidate();
    bool hasInput();
    void PostProcessing();
};
