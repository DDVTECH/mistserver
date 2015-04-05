#include "input.h"
#include <mist/dtsc.h>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>
#include <mist/mp4_encryption.h>
#include <set>

namespace Mist {
  struct seekPos {
    bool operator < (const seekPos & rhs) const {
      if (time < rhs.time){
        return true;
      }
      return (time == rhs.time && trackId < rhs.trackId);
    }
    long long int position;
    int trackId;
    long long int time;
    long long int duration;
    int size;
    long long int offset;
    bool isKeyFrame;
    std::string iVec;
  };


  class inputISMV : public Input {
    public:
      inputISMV(Util::Config * cfg);
    protected:
      //Private Functions
      bool setup();
      bool readHeader();
      void getNext(bool smart = true);
      void seek(int seekTime);
      void trackSelect(std::string trackSpec);
      bool atKeyFrame();

      FILE * inFile;

      void parseMoov(MP4::MOOV & moovBox);
      bool parseFrag(int & tId, std::vector<MP4::trunSampleInformation> & trunSamples, std::vector<std::string> & initVecs, std::string & mdat);
      void parseFragHeader(const unsigned int & trackId, const unsigned int & keyNum);
      void readBox(const char * type,  std::string & result);
      std::set<seekPos> buffered;
      std::map<int, int> lastKeyNum;
  };
}

typedef Mist::inputISMV mistIn;

