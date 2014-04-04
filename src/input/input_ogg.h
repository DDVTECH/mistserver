#include "input.h"
#include <mist/dtsc.h>
#include <mist/ogg.h>

namespace Mist {
  enum codecType {THEORA, VORBIS};

  struct segPart{
    char * segData;
    unsigned int len;
  };

  struct segment{
    bool operator < (const segment & rhs) const {
      return time < rhs.time || (time == rhs.time && tid < rhs.tid);
    }
    std::vector<segPart> parts;
    unsigned int time;
    unsigned int tid;
  };

  class oggTrack{
    public:
      oggTrack() : lastTime(0), parsedHeaders(false), lastPageOffset(0), nxtSegment(0) { }
      codecType codec;
      std::string contBuffer;//buffer for continuing pages
      double lastTime;
      long long unsigned int lastGran;
      bool parsedHeaders;
      double msPerFrame;
      long long unsigned int lastPageOffset;
      OGG::Page myPage;
      unsigned int nxtSegment;
      //Codec specific elements
      //theora
      theora::header idHeader;
      //vorbis
      std::deque<vorbis::mode> vModes;
      char channels;
      long long unsigned int blockSize[2];
  };
  
  class inputOGG : public Input {
    public:
      inputOGG(Util::Config * cfg);
    protected:
      //Private Functions
      bool setup();
      bool readHeader();
      bool seekNextPage(int tid);
      void getNext(bool smart = true);
      void seek(int seekTime);
      void trackSelect(std::string trackSpec);

      void parseBeginOfStream(OGG::Page & bosPage);

      FILE * inFile;
      std::map<long long int, long long int> snum2tid;
      std::map<long long int, oggTrack> oggTracks;
      std::set<segment> sortedSegments;
  };
}

typedef Mist::inputOGG mistIn;

