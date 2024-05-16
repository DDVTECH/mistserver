#include "output_http.h"
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
    OutCMAF(Socket::Connection &conn);
    ~OutCMAF();
    static void init(Util::Config *cfg);
    void onHTTP();
    void sendNext();
    void sendHeader(){};
    bool isReadyForPlay();

  protected:
    virtual void connStats(uint64_t now, Comms::Connections &statComm);
    void onTrackEnd(size_t idx);
    bool hasSessionIDs(){return !config->getBool("mergesessions");}

    void sendDashManifest();
    void dashAdaptationSet(size_t id, size_t idx, std::stringstream &r);
    void dashRepresentation(size_t id, size_t idx, std::stringstream &r);
    void dashSegmentTemplate(std::stringstream &r);
    void dashAdaptation(size_t id, std::set<size_t> tracks, bool aligned, std::stringstream &r);
    std::string dashTime(uint64_t time);
    std::string dashManifest(bool checkAlignment = true);

    void sendHlsManifest(const std::string url);
    void sendHlsMasterManifest();
    void sendHlsMediaManifest(const size_t requestTid);

    void sendSmoothManifest();
    std::string smoothManifest(bool checkAlignment = true);
    void smoothAdaptation(const std::string &type, std::set<size_t> tracks, std::stringstream &r);

    void generateSegmentlist(size_t idx, std::stringstream &s,
                             void callBack(uint64_t, uint64_t, std::stringstream &, bool));
    bool tracksAligned(const std::set<size_t> &trackList);
    std::string buildNalUnit(size_t len, const char *data);
    uint64_t targetTime;

    std::string h264init(const std::string &initData);
    std::string h265init(const std::string &initData);

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

#ifndef ONE_BINARY
typedef Mist::OutCMAF mistOut;
#endif
