#pragma once
#include "output_http.h"

namespace Mist{
  class OutEBML : public HTTPOutput{
  public:
    OutEBML(Socket::Connection &conn);
    static void init(Util::Config *cfg);
    void respondHTTP(const HTTP::Parser & req, bool headersOnly);
    void sendNext();
    void sendHeader();
    size_t clusterSize(uint64_t start, uint64_t end);

  protected:
    virtual bool inlineRestartCapable() const{return true;}
    bool isFileTarget(){
      if (config->getString("target").size() && config->getString("target").substr(0, 9) != "mkv-exec:"){return true;}
      return false;
    }

  private:
    bool isRecording();
    std::string doctype;
    void sendElemTrackEntry(size_t idx);
    size_t sizeElemTrackEntry(size_t idx);
    std::string trackCodecID(size_t idx);
    uint64_t currentClusterTime;
    uint64_t newClusterTime;
    // VoD-only
    void calcVodSizes();
    uint64_t segmentSize;                    // size of complete segment contents (excl. header)
    size_t tracksSize;                     // size of Tracks (incl. header)
    size_t infoSize;                       // size of Info (incl. header)
    size_t cuesSize;                       // size of Cues (excl. header)
    size_t seekheadSize;                   // size of SeekHead (incl. header)
    size_t seekSize;                       // size of contents of SeekHead (excl. header)
    std::map<size_t, size_t> clusterSizes; // sizes of Clusters (incl. header)
    void byteSeek(size_t startPos);
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutEBML mistOut;
#endif
