#include "output_http.h"

namespace Mist{
  class OutEBML : public HTTPOutput{
  public:
    OutEBML(Socket::Connection &conn);
    static void init(Util::Config *cfg);
    void onHTTP();
    void sendNext();
    void sendHeader();
    uint32_t clusterSize(uint64_t start, uint64_t end);

  private:
    bool isRecording();
    std::string doctype;
    void sendElemTrackEntry(const DTSC::Track & Trk);
    uint32_t sizeElemTrackEntry(const DTSC::Track & Trk);
    std::string trackCodecID(const DTSC::Track & Trk);
    uint64_t currentClusterTime;
    uint64_t newClusterTime;
    //VoD-only
    void calcVodSizes();
    uint64_t segmentSize;//size of complete segment contents (excl. header)
    uint32_t tracksSize;//size of Tracks (incl. header)
    uint32_t infoSize;//size of Info (incl. header)
    uint32_t cuesSize;//size of Cues (excl. header)
    uint32_t seekheadSize;//size of SeekHead (incl. header)
    uint32_t seekSize;//size of contents of SeekHead (excl. header)
    std::map<uint64_t, uint64_t> clusterSizes;//sizes of Clusters (incl. header)
    void byteSeek(uint64_t startPos);
  };
}

typedef Mist::OutEBML mistOut;

