#pragma once
#include "input.h"
#include <mist/dtsc.h>
#include <mist/nal.h>
#include <mist/segmentreader.h>

#define BUFFERTIME 10

namespace Mist{

  extern bool streamIsLive; //< Playlist can be sliding window or get new segments appended
  extern bool streamIsVOD; //< Playlist segments do not disappear
  extern uint32_t globalWaitTime; // largest waitTime for any playlist we're loading - used to update minKeepAway
  void parseKey(std::string key, char *newKey, unsigned int len);

  struct playListEntries{
    std::string filename;
    std::string relative_filename;
    std::string mapName;
    uint64_t startAtByte; ///< Byte position inside filename where to start reading
    uint64_t stopAtByte; ///< Byte position inside filename where to stop sending
    uint64_t bytePos;
    uint64_t mUTC; ///< UTC unix millis timestamp of first packet, if known
    float duration; ///< Duration of entry in seconds
    uint64_t timestamp; ///< zUTC-based timestamp for this entry
    int64_t timeOffset; ///< Value timestamps in the media are shifted by to get zUTC-based timestamps
    uint64_t wait;
    char ivec[16];
    char keyAES[16];
    playListEntries(){
      bytePos = 0;
      mUTC = 0;
      duration = 0;
      timestamp = 0;
      timeOffset = 0;
      wait = 0;
      startAtByte = 0;
      stopAtByte = 0;
      for (size_t i = 0; i < 16; ++i){
        ivec[i] = 0;
        keyAES[i] = 0;
      }
    }
    std::string shortName() const{
      if (!startAtByte && !stopAtByte){return filename;}
      std::string ret = filename;
      ret += " (";
      ret += JSON::Value(startAtByte).asString();
      ret += "-";
      ret += JSON::Value(stopAtByte).asString();
      ret += ")";
      return ret;
    }
  };

  inline bool operator< (const playListEntries a, const playListEntries b){
    return a.filename < b.filename || (a.filename == b.filename && a.startAtByte < b.startAtByte);
  }

  inline bool operator== (const playListEntries a, const playListEntries b){
    return a.filename == b.filename && a.startAtByte == b.startAtByte;
  }

  /// Keeps the segment entry list by playlist ID
  extern std::map<uint32_t, std::deque<playListEntries> > listEntries;

  class Playlist{
  public:
    Playlist(const std::string &uriSrc = "");
    bool isUrl() const;
    bool reload();
    void addEntry(const std::string & absolute_filename, const std::string &filename, float duration, uint64_t &bpos,
                  const std::string &key, const std::string &keyIV, const std::string mapName, uint64_t startByte, uint64_t lenByte);

    std::string uri; // link to the current playlistfile
    HTTP::URL root;
    std::string relurl;

    uint64_t reloadNext;

    uint32_t id;
    bool playlistEnd;
    int noChangeCount;

    uint64_t waitTime;
    uint64_t lastTimestamp;
    uint64_t startTime;
    int64_t oUTC;
    uint64_t nextUTC; ///< If non-zero, the UTC timestamp of the next segment on this playlist
    char keyAES[16];
    std::map<std::string, std::string> keys;
    std::map<std::string, std::string> maps;
    uint64_t firstIndex; //< the index of the first segment in the playlist
    uint64_t lastSegment;
    std::map<size_t, bool> tracks;
  };

  void playlistRunner(void *ptr);

  class InputHLS : public Input{
  public:
    InputHLS(Util::Config *cfg);
    ~InputHLS(){}
    bool needsLock(){return !config->getBool("realtime");}
    bool openStreamSource();
    bool callback();

  protected:
    uint64_t zUTC; ///< Zero point in local millis, as UTC unix time millis
    int64_t streamOffset; ///< bootMsOffset we need to set once we have parsed the header
    unsigned int startTime;
    SegmentReader segDowner;
    int version;
    int targetDuration;
    bool endPlaylist;
    uint64_t currentPlaylist;

    bool allowRemap;     ///< True if the next packet may remap the timestamps
    std::map<uint64_t, uint64_t> pidMapping;
    std::map<uint64_t, uint64_t> pidMappingR;
    std::map<int, int64_t> plsTimeOffset;
    std::map<int, uint64_t> plsLastTime;
    std::map<int, uint64_t> plsInterval;

    size_t currentIndex;
    std::string currentFile;

    // Used to map packetId of packets in pidMapping
    int pidCounter;

    // Override userLeadOut to buffer new data as live packets
    void userLeadOut();
    // Removes any metadata which is no longer and the playlist or buffered in memory
    void updateMeta();
    /// Tries to add as much live packets from a TS file at the given location
    bool parseSegmentAsLive(uint64_t segmentIndex);
    // index of last playlist entry finished parsing
    long previousSegmentIndex;

    size_t firstSegment();
    void waitForNextSegment();
    void readPMT();
    bool checkArguments();
    bool preSetup();
    bool readHeader();
    bool readExistingHeader();
    void postHeader();
    void getNext(size_t idx = INVALID_TRACK_ID);
    void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID);
    bool keepRunning(bool updateActCtr = true);

    bool readIndex();
    bool initPlaylist(const std::string &uri, bool fullInit = true);
    bool readPlaylist(const HTTP::URL &uri, const std::string & relurl, bool fullInit = true);
    bool readNextFile();

    void parseStreamHeader();
    void parseLivePoint();

    uint32_t getMappedTrackId(uint64_t id);
    uint32_t getMappedTrackPlaylist(uint64_t id);
    uint64_t getOriginalTrackId(uint32_t playlistId, uint32_t id);
    uint64_t getPacketTime(uint64_t packetTime, uint64_t tid, uint64_t currentPlaylist, uint64_t nUTC = 0);
    uint64_t getPacketID(uint64_t currentPlaylist, uint64_t trackId);
    virtual void finish();
    void injectLocalVars();
    virtual void checkHeaderTimes(const HTTP::URL & streamFile);
    // Used to immediately mark pages for removal when we're bursting through segments on initial boot
    bool isInitialRun;
  };
}// namespace Mist

typedef Mist::InputHLS mistIn;
