#pragma once
#include "input.h"
#include <fstream>
#include <mist/dtsc.h>
#include <mist/nal.h>
#include <mist/ts_packet.h>
#include <mist/ts_stream.h>
#include <set>
#include <string>
#include <vector>
//#include <stdint.h>
#include <mist/downloader.h>
#include <mist/http_parser.h>

#define BUFFERTIME 10

namespace Mist{

  enum PlaylistType{VOD, LIVE, EVENT};

  extern bool streamIsLive;
  extern uint32_t globalWaitTime; // largest waitTime for any playlist we're loading - used to update minKeepAway
  void parseKey(std::string key, char *newKey, unsigned int len);

  struct playListEntries{
    std::string filename;
    uint64_t bytePos;
    uint64_t mUTC; ///< UTC unix millis timestamp of first packet, if known
    float duration;
    unsigned int timestamp;
    unsigned int wait;
    char ivec[16];
    char keyAES[16];
  };

  /// Keeps the segment entry list by playlist ID
  extern std::map<uint32_t, std::deque<playListEntries> > listEntries;

  class SegmentDownloader{
  public:
    SegmentDownloader();
    HTTP::Downloader segDL;
    const char *packetPtr;
    bool loadSegment(const playListEntries &entry);
    bool atEnd() const;
  };

  class Playlist{
  public:
    Playlist(const std::string &uriSrc = "");
    bool isUrl() const;
    bool reload();
    void addEntry(const std::string &filename, float duration, uint64_t &totalBytes,
                  const std::string &key, const std::string &keyIV);
    bool isSupportedFile(const std::string filename);

    std::string uri; // link to the current playlistfile
    HTTP::URL root;

    HTTP::Downloader plsDL;

    uint64_t reloadNext;

    uint32_t id;
    bool playlistEnd;
    int noChangeCount;
    uint64_t lastFileIndex;

    int waitTime;
    PlaylistType playlistType;
    unsigned int lastTimestamp;
    unsigned int startTime;
    uint64_t nextUTC; ///< If non-zero, the UTC timestamp of the next segment on this playlist
    char keyAES[16];
    std::map<std::string, std::string> keys;
  };

  void playlistRunner(void *ptr);

  class inputHLS : public Input{
  public:
    inputHLS(Util::Config *cfg);
    ~inputHLS();
    bool needsLock();
    bool openStreamSource();
    bool callback();

  protected:
    uint64_t zUTC; ///< Zero point in local millis, as UTC unix time millis
    uint64_t nUTC; ///< Next packet timestamp in UTC unix time millis
    unsigned int startTime;
    PlaylistType playlistType;
    SegmentDownloader segDowner;
    int version;
    int targetDuration;
    bool endPlaylist;
    int currentPlaylist;

    bool allowRemap;     ///< True if the next packet may remap the timestamps
    bool allowSoftRemap; ///< True if the next packet may soft-remap the timestamps
    std::map<uint64_t, uint64_t> pidMapping;
    std::map<uint64_t, uint64_t> pidMappingR;
    std::map<int, int64_t> plsTimeOffset;
    std::map<int, uint64_t> plsLastTime;
    std::map<int, uint64_t> plsInterval;

    int currentIndex;
    std::string currentFile;

    TS::Stream tsStream; ///< Used for parsing the incoming ts stream

    Socket::Connection conn;
    TS::Packet tsBuf;

    int firstSegment();
    void waitForNextSegment();
    void readPMT();
    bool checkArguments();
    bool preSetup();
    bool readHeader();
    bool needHeader(){return true;}
    void getNext(bool smart = true);
    void seek(int seekTime);
    void trackSelect(std::string trackSpec);
    FILE *inFile;
    FILE *tsFile;

    bool readIndex();
    bool initPlaylist(const std::string &uri, bool fullInit = true);
    bool readPlaylist(const HTTP::URL &uri, bool fullInit = true);
    bool readNextFile();

    void parseStreamHeader();

    uint32_t getMappedTrackId(uint64_t id);
    uint32_t getMappedTrackPlaylist(uint64_t id);
    uint64_t getOriginalTrackId(uint32_t playlistId, uint32_t id);
    int getEntryId(int playlistId, uint64_t bytePos);
  };
}// namespace Mist

typedef Mist::inputHLS mistIn;
