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
      void getNext(bool smart = true);
      void seek(int seekTime);
      void trackSelect(std::string trackSpec);
      bool vtt;

      FILE * inFile;

  };



}

typedef Mist::InputSrt mistIn;

