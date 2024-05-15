#include "output.h"
#include <mist/amf.h>
#include <mist/flv_tag.h>
#include <mist/http_parser.h>
#include <mist/rtmpchunks.h>
#include <mist/url.h>
#include <mist/urireader.h>
#include <mist/adts.h>

namespace Mist{

  class OutRTMP : public Output{
  public:
    OutRTMP(Socket::Connection &conn);
    static void init(Util::Config *cfg);
    void onRequest();
    void sendNext();
    void sendHeader();
    static bool listenMode();
    void requestHandler();
    bool onFinish();

  protected:
    std::string streamOut; ///< When pushing out, the output stream name
    bool setRtmpOffset;
    int64_t rtmpOffset;
    uint64_t lastOutTime;
    uint32_t maxbps;
    std::string app_name;
    void parseChunk(Socket::Buffer &inputBuffer);
    void parseAMFCommand(AMF::Object &amfData, int messageType, int streamId);
    void sendCommand(AMF::Object &amfReply, int messageType, int streamId);
    void startPushOut(const char *args);
    uint64_t lastAck;
    HTTP::URL pushApp, pushUrl;
    uint8_t authAttempts;
    void sendSilence(uint64_t currTime);
    bool hasSilence;
    uint64_t lastSilence;
    // Indicates whether the track should loop a custom audio file instead of normal audio
    bool hasCustomAudio;
    // Last timestamp we inserted custom audio / silence
    uint64_t lastAudioInserted;
    // Info on AAC file (including headers and such)
    uint64_t customAudioSize;
    uint64_t customAudioIterator;
    char *customAudioFile;
    // Info on current ADTS frame
    aac::adts currentFrameInfo;
    uint64_t currentFrameTimestamp;
    // Loops .AAC file contents until untilTimestamp is reached
    void sendLoopedAudio(uint64_t untilTimestamp);
    // Gets the next ADTS frame in AAC file. Loops if EOF reached
    void calcNextFrameInfo();
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutRTMP mistOut;
#endif
