#pragma once
#include "input.h"
#include <fstream>
#include <mist/dtsc.h>
#include <string>

namespace Mist{

  class InputSubRip : public Input{
  public:
    InputSubRip(Util::Config *cfg);

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

#ifndef ONE_BINARY
typedef Mist::InputSubRip mistIn;
#endif
