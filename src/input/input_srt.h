#pragma once
#include "input.h"
#include <fstream>
#include <mist/dtsc.h>
#include <string>

namespace Mist{

  class InputSrt : public Input{
  public:
    InputSrt(Util::Config *cfg);

  protected:
    std::ifstream fileSource;

    bool checkArguments();
    bool readHeader();
    bool preRun();
    void getNext(size_t idx = INVALID_TRACK_ID);
    void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID);
    bool vtt;

    FILE *inFile;
  };

}// namespace Mist

typedef Mist::InputSrt mistIn;
