#include "input.h"
#include <mist/dtsc.h>
#include <mist/urireader.h>
#include <mist/mp4.h>
#include <mist/mp4_stream.h>
#include <mist/mp4_generic.h>
namespace Mist{
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
    bool shiftTo(size_t pos, size_t len);

    HTTP::URIReader inFile;
    Util::ResizeablePointer readBuffer;
    uint64_t readPos;
    uint64_t moovPos;
    uint64_t bps;
    uint64_t nextBox;

    MP4::TrackHeader &headerData(size_t trackID);

    std::deque<MP4::TrackHeader> trackHeaders;
    std::set<MP4::PartTime> curPositions;
  };
}// namespace Mist

typedef Mist::InputMP4 mistIn;
