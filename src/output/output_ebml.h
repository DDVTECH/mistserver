#pragma once
#include "output_http.h"

#include <mist/ebml_socketglue.h>

namespace Mist{
  class OutEBML : public HTTPOutput{
  public:
    OutEBML(Socket::Connection & conn, Util::Config & cfg, JSON::Value & capa);
    static void init(Util::Config *cfg, JSON::Value & capa);

    void respondHTTP(const HTTP::Parser & req, bool headersOnly) override;
    void sendNext() override;
    void sendHeader() override;
    void preHTTP() override;
    virtual void dataCallback(const char *ptr, size_t size) override;
    virtual void initialSeek(bool dryRun = false) override;

    size_t clusterSize(uint64_t start, uint64_t end);

  protected:
    virtual bool inlineRestartCapable() const override { return true; }
    bool isFileTarget() override {
      if (config->getString("target").size() && config->getString("target").substr(0, 9) != "mkv-exec:") {
        return true;
      }
      return false;
    }

    int64_t subtractTime{0}; ///< Milliseconds that will be subtracted from internal timestamps to convert to external timestamps.

  private:
    // For PUT input
    EBML::toDTSC parser;
    Util::ResizeablePointer readBuffer;
    bool seenTime{false};
    int64_t timeOffset{0};

    std::string doctype{"matroska"};
    void sendElemTrackEntry(size_t idx);
    size_t sizeElemTrackEntry(size_t idx);
    std::string trackCodecID(size_t idx);
    uint64_t currentClusterTime{0};
    uint64_t newClusterTime{0};
    // VoD-only
    void calcVodSizes();
    uint64_t segmentSize{0xFFFFFFFFFFFFFFFFull}; ///< size of complete segment contents (excl. header)
    size_t tracksSize{0}; ///< size of Tracks (incl. header)
    size_t infoSize{0}; ///< size of Info (incl. header)
    size_t cuesSize{0}; ///< size of Cues (excl. header)
    size_t seekheadSize{0}; ///< size of SeekHead (incl. header)
    size_t seekSize{0}; ///< size of contents of SeekHead (excl. header)
    std::map<size_t, size_t> clusterSizes; ///< sizes of Clusters (incl. header)
    void byteSeek(size_t startPos);
  };
}// namespace Mist

typedef Mist::OutEBML mistOut;
