#include <mist/ogg.h>

namespace OGG {
  struct trackStats{
    unsigned int OGGSerial;
    unsigned int seqNum;
    std::string codec;
    //theora vars
    unsigned int lastKeyFrame;
    unsigned int sinceKeyFrame;
    unsigned int significantValue;//KFGShift for theora and other video;
    int prevBlockFlag;
    //vorbis vars
    bool hadFirst;
    std::deque<vorbis::mode> vorbisModes;//modes for vorbis
    char blockSize[2];
  };
  
  class converter{
    public:
      void readDTSCHeader(DTSC::Meta & meta);
      void readDTSCVector(JSON::Value & DTSCPart, std::string & pageBuffer);
      std::string parsedPages;
    private:
      std::map <long long unsigned int, trackStats> trackInf;
      //long long unsigned int calcGranule(long long unsigned int trackID, bool keyFrame);
  };
  
}
