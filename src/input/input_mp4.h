#include "input.h"
#include <mist/dtsc.h>
#include <mist/urireader.h>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>
namespace Mist{
  class mp4PartTime{
  public:
    mp4PartTime() : time(0), duration(0), offset(0), trackID(0), bpos(0), size(0), index(0){}
    bool operator<(const mp4PartTime &rhs) const{
      if (time < rhs.time){return true;}
      if (time > rhs.time){return false;}
      if (trackID < rhs.trackID){return true;}
      return (trackID == rhs.trackID && bpos < rhs.bpos);
    }
    uint64_t time;
    uint64_t duration;
    int32_t offset;
    size_t trackID;
    uint64_t bpos;
    uint32_t size;
    uint64_t index;
  };

  struct mp4PartBpos{
    bool operator<(const mp4PartBpos &rhs) const{
      if (time < rhs.time){return true;}
      if (time > rhs.time){return false;}
      if (trackID < rhs.trackID){return true;}
      return (trackID == rhs.trackID && bpos < rhs.bpos);
    }
    uint64_t time;
    size_t trackID;
    uint64_t bpos;
    uint64_t size;
    uint64_t stcoNr;
    int32_t timeOffset;
    bool keyframe;
  };

  class mp4TrackHeader{
  public:
    mp4TrackHeader();
    size_t trackId;
    void read(MP4::TRAK &trakBox);
    MP4::STCO stcoBox;
    MP4::CO64 co64Box;
    MP4::STSZ stszBox;
    MP4::STTS sttsBox;
    bool hasCTTS;
    MP4::CTTS cttsBox;
    MP4::STSC stscBox;
    uint64_t timeScale;
    void getPart(uint64_t index, uint64_t &offset);
    uint64_t size();

  private:
    bool initialised;
    // next variables are needed for the stsc/stco loop
    uint64_t stscStart;
    uint64_t sampleIndex;
    // next variables are needed for the stts loop
    uint64_t deltaIndex; ///< Index in STTS box
    uint64_t deltaPos;   ///< Sample counter for STTS box
    uint64_t deltaTotal; ///< Total timestamp for STTS box
    // for CTTS box loop
    uint64_t offsetIndex; ///< Index in CTTS box
    uint64_t offsetPos;   ///< Sample counter for CTTS box

    bool stco64;
  };

  class InputMP4 : public Input, public Util::DataCallback {
  public:
    InputMP4(Util::Config *cfg);
    virtual void dataCallback(const char *ptr, size_t size);
    virtual size_t getDataCallbackPos() const;

  protected:
    // Private Functions
    bool checkArguments();
    bool preRun();
    bool readHeader();
    bool needHeader();
    void getNext(size_t idx = INVALID_TRACK_ID);
    void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID);
    void handleSeek(uint64_t seekTime, size_t idx);

    HTTP::URIReader inFile;
    Util::ResizeablePointer readBuffer;
    uint64_t readPos;
    uint64_t bps;

    mp4TrackHeader &headerData(size_t trackID);

    std::deque<mp4TrackHeader> trackHeaders;
    std::set<mp4PartTime> curPositions;

    // remember last seeked keyframe;
    std::map<size_t, uint32_t> nextKeyframe;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::InputMP4 mistIn;
#endif
