#include "input.h"
#include <mist/checksum.h>
#include <mist/dtsc.h>

namespace Mist{
  class InputFLAC : public Input{
  public:
    InputFLAC(Util::Config *cfg);
    ~InputFLAC();

  protected:
    bool checkArguments();
    bool preRun();
    bool readHeader();
    void getNext(size_t idx = INVALID_TRACK_ID);
    void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID);
    FILE *inFile;

  private:
    void stripID3tag();
    bool readMagicPacket();
    bool fillBuffer(size_t size = 40960);
    uint64_t neededBytes();
    char *ptr;
    std::string flacBuffer;
    Socket::Buffer buffer;
    uint64_t bufferSize;
    uint64_t curPos;

    bool stopProcessing;
    bool stopFilling;
    size_t tNum;
    size_t pos;
    char *end;
    char *start;
    int prev_header_size;
    uint64_t sampleNr;
    uint64_t frameNr;

    size_t sampleRate;
    size_t blockSize;
    size_t bitRate;
    size_t channels;
  };

}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::InputFLAC mistIn;
#endif
