#include "output_http.h"

#include <mist/cmaf.h>
#include <mist/downloader.h>
#include <mist/http_parser.h>
// #include <mist/mp4_generic.h>

namespace Mist{
  /// Keeps track of the state of an outgoing CMAF Push track.
  class CMAFPushTrack{
  public:
    CMAFPushTrack(){
      debug = false;
      debugFile = 0;
    }
    ~CMAFPushTrack(){disconnect();}
    void connect(std::string debugParam = "");
    void disconnect();

    void send(const char *data, size_t len);
    void send(const std::string &data);

    HTTP::Downloader D;
    HTTP::URL url;
    uint64_t headerFrom;
    uint64_t headerUntil;

    bool debug;
    char debugName[500];
    FILE *debugFile;
  };

  class OutCMAF : public HTTPOutput{
  public:
    OutCMAF(Socket::Connection & conn, Util::Config & cfg, JSON::Value & capa);
    ~OutCMAF();
    static void init(Util::Config *cfg, JSON::Value & capa);
    virtual void respondHTTP(const HTTP::Parser & req, bool headersOnly);
    virtual bool onFinish();
    void sendNext();
    void sendHeader(){};
    bool isReadyForPlay();

  protected:
    virtual void connStats(uint64_t now, Comms::Connections &statComm);
    void onTrackEnd(size_t idx);

    void dashAdaptationSet(size_t id, size_t idx, std::stringstream &r);
    void dashRepresentation(size_t id, size_t idx, std::stringstream & r, bool strictLowLatency = false,
                            uint64_t availabilityStartMs = 0);
    void dashSegmentTemplate(std::stringstream & r, double availabilityTimeOffset = 0.0,
                             size_t timingTrack = INVALID_TRACK_ID, size_t requestTrack = INVALID_TRACK_ID);
    void dashAdaptation(size_t id, std::set<size_t> tracks, bool aligned, std::stringstream & r, uint64_t minStartTime = 0,
                        uint64_t maxEndTime = 0, bool includeForming = false, size_t timingTrack = INVALID_TRACK_ID,
                        double availabilityTimeOffset = 0.0, bool strictLowLatency = false, uint64_t availabilityStartMs = 0);
    void dashMuxedAdaptation(const std::set<size_t> & videoTracks, const std::set<size_t> & audioTracks, std::stringstream & r,
                             uint64_t minStartTime, uint64_t maxEndTime, bool includeForming, double availabilityTimeOffset);
    std::string dashTime(uint64_t time);
    std::string dashManifest(bool checkAlignment, bool muxedPackaging);

    void sendHlsManifest(const HTTP::Parser & req, const std::string url, bool headersOnly);
    void sendHlsMasterManifest(const HTTP::Parser & req);
    void sendHlsMediaManifest(const HTTP::Parser & req, const size_t requestTid);

    void sendSmoothManifest(bool headersOnly);
    std::string smoothManifest(bool checkAlignment = true);
    void smoothAdaptation(const std::string &type, std::set<size_t> tracks, std::stringstream &r);

    struct DashSegmentWindow {
        DashSegmentWindow() : start(0), end(0), count(0) {}
        uint64_t start;
        uint64_t end;
        uint64_t count;
    };
    DashSegmentWindow generateSegmentlist(size_t idx, std::stringstream & s, uint64_t minStartTime = 0, uint64_t maxEndTime = 0,
                                          bool includeForming = false, size_t timingTrack = INVALID_TRACK_ID);
    void sendCmafError(const std::string & code, const std::string & message);
    uint64_t getPartTargetTime(size_t timingTrack, uint64_t fragmentStart, uint64_t fragmentIndex, uint32_t part);
    bool tracksAligned(const std::set<size_t> & trackList);
    uint64_t cmafSegmentEnd; ///< exclusive end time of the current complete segment response
    uint32_t partTargetMs;
    bool muxedByDefault;
    bool cmafMuxedStream;

    // Low-latency DASH chunked-transfer state. When cmafLLStream is set, sendNext()
    // streams the in-progress fragment as a sequence of per-part [moof][mdat] CMAF
    // chunks into one open response (see sendNextLL()).
    bool cmafLLStream;
    uint64_t cmafLLFragStart; ///< stream-clock start time of the fragment being streamed
    uint64_t cmafLLFragEnd; ///< stream-clock end time of the fragment being streamed
    uint64_t cmafLLMsn; ///< fragment index being streamed
    uint64_t cmafLLmTrack; ///< timing track for part-availability math
    size_t cmafLLRequestTrack; ///< stable primary/request track; never derived from packet order
    uint64_t cmafLLPartEnd; ///< end time of the currently-open part chunk
    uint64_t cmafLLPartLeft; ///< payload bytes left in the currently-open mdat
    uint64_t cmafLLSeq; ///< moof sequence number
    void sendNextLL();

    // For CMAF push out
    void startPushOut();
    void pushNext();

    uint32_t crc;
    HTTP::URL pushUrl;
    std::map<size_t, CMAFPushTrack> pushTracks;
    void setupTrackObject(size_t idx);
    bool waitForNextKey(uint64_t maxWait = 15000);
    // End CMAF push out
  };
}// namespace Mist

typedef Mist::OutCMAF mistOut;
