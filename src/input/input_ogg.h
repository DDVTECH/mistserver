#include "input.h"
#include <mist/dtsc.h>
#include <mist/ogg.h>

namespace Mist {

  struct segPart {
    char * segData;
    unsigned int len;
  };

  class segment {
    public:
      segment() : time(0), tid(0), bytepos(0), keyframe(0){}
      bool operator < (const segment & rhs) const {
        return time < rhs.time || (time == rhs.time && tid < rhs.tid);
      }
      std::vector<std::string> parts;
      unsigned long long int time;
      unsigned long long int tid;
      long long unsigned int bytepos;
      bool keyframe;
      JSON::Value toJSON(OGG::oggCodec myCodec);
  };

  struct position {
    bool operator < (const position & rhs) const {
      if (time < rhs.time){
        return true;
      } else {
        if (time == rhs.time){
          if (trackID < rhs.trackID){
            return true;
          }
        }
      }
      return false;
    }
    long unsigned int trackID;
    long long unsigned int time;
    long long unsigned int bytepos;
    long long unsigned int segmentNo;
  };
/*
  class oggTrack {
    public:
      oggTrack() : lastTime(0), parsedHeaders(false), lastPageOffset(0), nxtSegment(0){ }
      codecType codec;
      std::string contBuffer;//buffer for continuing pages
      segment myBuffer;
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
      unsigned long blockSize[2];
      unsigned long getBlockSize(unsigned int vModeIndex);
  };*/

  class inputOGG : public Input {
    public:
      inputOGG(Util::Config * cfg);
    protected:
      //Private Functions
      bool checkArguments();
      bool preRun();
      bool readHeader();
      position seekFirstData(long long unsigned int tid);      
      void getNext(bool smart = true);
      void seek(int seekTime);
      void trackSelect(std::string trackSpec);

      void parseBeginOfStream(OGG::Page & bosPage);
      std::set<position> currentPositions;
      FILE * inFile;
      std::map<long unsigned int, OGG::oggTrack> oggTracks;//this remembers all metadata for every track
      std::set<segment> sortedSegments;//probably not needing this
      long long unsigned int calcGranuleTime(unsigned long tid, long long unsigned int granule);
      long long unsigned int calcSegmentDuration(unsigned long tid , std::string & segment);

  };
}

typedef Mist::inputOGG mistIn;


