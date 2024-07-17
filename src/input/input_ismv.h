#include "input.h"
#include <mist/dtsc.h>
#include <mist/mp4.h>
#include <mist/mp4_encryption.h>
#include <mist/mp4_generic.h>
#include <mist/util.h>
#include <set>

namespace Mist{
  struct seekPosISMV{
    bool operator<(const seekPosISMV &rhs) const{
      if (time < rhs.time){return true;}
      return (time == rhs.time && trackId < rhs.trackId);
    }
    uint64_t position;
    size_t trackId;
    uint64_t time;
    uint64_t duration;
    uint64_t size;
    int64_t offset;
    bool isKeyFrame;
    std::string iVec;
  };

  class InputISMV : public Input{
  public:
    InputISMV(Util::Config *cfg);

  protected:
    // Private Functions
    bool checkArguments();
    bool preRun();
    bool readHeader();
    virtual void getNext(size_t idx = INVALID_TRACK_ID);
    virtual void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID);

    FILE *inFile;

    void parseMoov(MP4::MOOV &moovBox);
    bool readMoofSkipMdat(size_t &tId, std::vector<MP4::trunSampleInformation> &trunSamples);

    void bufferFragmentData(size_t trackId, uint32_t keyNum);
    std::set<seekPosISMV> buffered;
    std::map<size_t, uint32_t> lastKeyNum;

    Util::ResizeablePointer dataPointer;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::InputISMV mistIn;
#endif
