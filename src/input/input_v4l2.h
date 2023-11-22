#include "input.h"
#include <mist/dtsc.h>
#include <fstream>
#include <linux/videodev2.h>

namespace Mist{
  class inputVideo4Linux : public Input{
  public:
    inputVideo4Linux(Util::Config *cfg);

  protected:
    bool checkArguments();
    virtual bool needHeader(){return false;}
    virtual bool isSingular(){return true;}
    bool needsLock(){return false;}
    JSON::Value enumerateSources(const std::string & device);
    JSON::Value getSourceCapa(const std::string & device);
    
    void parseStreamHeader(){};
    bool openStreamSource();
    void closeStreamSource();
    void streamMainLoop();

    std::string intToString(int n);
    int strToInt(std::string str);
    std::string getInput(std::string input);

    uint64_t width;
    uint64_t height;
    uint64_t fpsDenominator;
    uint64_t fpsNumerator;
    uint pixelFmt;
    uint64_t startTime;
    size_t tNumber;
    int fd;
    v4l2_buffer bufferinfo;
    char* buffer;
  };
}// namespace Mist

typedef Mist::inputVideo4Linux mistIn;
