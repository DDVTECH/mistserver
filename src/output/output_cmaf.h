#include "output_http.h"
#include <mist/http_parser.h>
#include <mist/mp4_generic.h>

namespace Mist{
  class OutCMAF : public HTTPOutput{
  public:
    OutCMAF(Socket::Connection &conn);
    ~OutCMAF();
    static void init(Util::Config *cfg);
    void onHTTP();
    void sendNext();
    void sendHeader(){};

  protected:
    void sendDashManifest();
    void dashAdaptationSet(size_t id, size_t idx, std::stringstream &r);
    void dashRepresentation(size_t id, size_t idx, std::stringstream &r);
    void dashSegmentTemplate(std::stringstream &r);
    void dashAdaptation(size_t id, std::set<size_t> tracks, bool aligned, std::stringstream &r);
    std::string dashTime(uint64_t time);
    std::string dashManifest(bool checkAlignment = true);

    void sendHlsManifest(size_t idx = INVALID_TRACK_ID, const std::string &sessId = "");
    std::string hlsManifest();
    std::string hlsManifest(size_t idx, const std::string &sessId);

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
  };
}// namespace Mist

typedef Mist::OutCMAF mistOut;
