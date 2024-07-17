#include "input.h"
#include <mist/dtsc.h>
#include <mist/ogg.h>

namespace Mist{

  struct segPart{
    char *segData;
    size_t len;
  };

  class segment{
  public:
    segment() : time(0), tid(0), bytepos(0), keyframe(0){}
    bool operator<(const segment &rhs) const{
      return time < rhs.time || (time == rhs.time && tid < rhs.tid);
    }
    std::vector<std::string> parts;
    uint64_t time;
    uint64_t tid;
    uint64_t bytepos;
    bool keyframe;
    JSON::Value toJSON(OGG::oggCodec myCodec);
  };

  struct position{
    bool operator<(const position &rhs) const{
      if (time < rhs.time){return true;}
      if (time > rhs.time){return false;}
      return trackID < rhs.trackID;
    }
    uint64_t trackID;
    uint64_t time;
    uint64_t bytepos;
    uint64_t segmentNo;
  };

  class InputOGG : public Input{
  public:
    InputOGG(Util::Config *cfg);

  protected:
    // Private Functions
    bool checkArguments();
    bool preRun();
    bool readHeader();
    position seekFirstData(size_t tid);
    void getNext(size_t idx = INVALID_TRACK_ID);
    void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID);

    void parseBeginOfStream(OGG::Page &bosPage);
    std::set<position> currentPositions;
    FILE *inFile;
    std::map<size_t, OGG::oggTrack> oggTracks; // this remembers all metadata for every track
    std::set<segment> sortedSegments;          // probably not needing this
    uint64_t calcGranuleTime(size_t tid, uint64_t granule);
    uint64_t calcSegmentDuration(size_t tid, std::string &segment);
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::InputOGG mistIn;
#endif
