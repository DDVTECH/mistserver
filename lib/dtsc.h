/// \file dtsc.h
/// Holds all headers for DDVTECH Stream Container parsing/generation.

#pragma once
#include "defines.h"
#include "json.h"
#include "shared_memory.h"
#include "socket.h"
#include "timing.h"
#include "util.h"

#include <functional>
#include <iostream>
#include <set>
#include <stdint.h> //for uint64_t
#include <stdio.h> //for FILE
#include <string>

#define DTSC_INT 0x01
#define DTSC_STR 0x02
#define DTSC_OBJ 0xE0
#define DTSC_ARR 0x0A
#define DTSC_CON 0xFF

#define TRACK_VALID_EXT_HUMAN 1 //(assumed) humans connecting externally
#define TRACK_VALID_EXT_PUSH 2 //(assumed) humans connecting externally
#define TRACK_VALID_INT_PROCESS 4 //internal processes
#define TRACK_VALID_ALL 0xFF //all of the above, default

// Increase this value every time the DTSH file format changes in an incompatible way
// Changelog:
//  Version 0-2: Undocumented changes
//  Version 3: switched to bigMeta-style by default, Parts layout switched from 3/2/4 to 3/3/3 bytes
//  Version 4: renamed bps to maxbps (peak bit rate) and added new value bps (average bit rate)
#define DTSH_VERSION 4

namespace DTSC{

  extern uint64_t veryUglyJitterOverride;
  extern uint8_t trackValidMask;
  extern uint8_t trackValidDefault;

  ///\brief This enum holds all possible datatypes for DTSC packets.
  enum datatype{
    AUDIO,          ///< Stream Audio data
    VIDEO,          ///< Stream Video data
    META,           ///< Stream Metadata
    PAUSEMARK,      ///< Pause marker
    MODIFIEDHEADER, ///< Modified header data.
    INVALID         ///< Anything else or no data available.
  };

  extern char Magic_Header[];  ///< The magic bytes for a DTSC header
  extern char Magic_Packet[];  ///< The magic bytes for a DTSC packet
  extern char Magic_Packet2[]; ///< The magic bytes for a DTSC packet version 2
  extern char Magic_Command[]; ///< The magic bytes for a DTCM packet

  enum packType{DTSC_INVALID, DTSC_HEAD, DTSC_V1, DTSC_V2, DTCM};

  /// This class allows scanning through raw binary format DTSC data.
  /// It can be used as an iterator or as a direct accessor.
  class Scan{
  public:
    Scan();
    Scan(char *pointer, size_t len);
    operator bool() const;
    std::string toPrettyString(size_t indent = 0) const;
    bool hasMember(const std::string &indice) const;
    bool hasMember(const char *indice, size_t ind_len) const;
    Scan getMember(const std::string &indice) const;
    Scan getMember(const char *indice) const;
    Scan getMember(const char *indice, size_t ind_len) const;
    void nullMember(const std::string & indice);
    void nullMember(const char *indice, size_t ind_len);
    Scan getIndice(size_t num) const;
    std::string getIndiceName(size_t num) const;
    size_t getSize() const;
    void forEachMember(std::function<void(const DTSC::Scan &, const std::string &)> cb) const;

    char getType() const;
    bool asBool() const;
    int64_t asInt() const;
    std::string asString() const;
    void getString(char *&result, size_t &len) const;
    JSON::Value asJSON() const;

  private:
    char *p;
    size_t len;
  };

  /// DTSC::Packets can currently be three types:
  /// DTSC_HEAD packets are the "DTSC" header string, followed by 4 bytes len and packed content.
  /// DTSC_V1 packets are "DTPD", followed by 4 bytes len and packed content.
  /// DTSC_V2 packets are "DTP2", followed by 4 bytes len, 4 bytes trackID, 8 bytes time, and packed
  /// content. The len is always without the first 8 bytes counted.
  class Packet{
  public:
    Packet();
    Packet(const Packet &rhs, size_t idx = INVALID_TRACK_ID);
    Packet(const char *data_, unsigned int len, bool noCopy = false);
    virtual ~Packet();
    void null();
    void operator=(const Packet &rhs);
    operator bool() const;
    packType getVersion() const;
    void reInit(Socket::Connection &src);
    void reInit(const char *data_, unsigned int len, bool noCopy = false);
    void genericFill(uint64_t packTime, int64_t packOffset, uint32_t packTrack, const char *packData,
                     size_t packDataSize, uint64_t packBytePos, bool isKeyframe);
    void appendData(const char *appendData, uint32_t appendLen);
    void getString(const char *identifier, char *&result, size_t &len) const;
    void getString(const char *identifier, std::string &result) const;
    void getInt(const char *identifier, uint64_t &result) const;
    uint64_t getInt(const char *identifier) const;
    void getFlag(const char *identifier, bool &result) const;
    bool getFlag(const char *identifier) const;
    bool hasMember(const char *identifier) const;
    void appendNal(const char *appendData, uint32_t appendLen);
    void upgradeNal(const char *appendData, uint32_t appendLen);
    void setKeyFrame(bool kf);
    virtual uint64_t getTime() const;
    void setTime(uint64_t _time);
      void nullMember(const std::string & memb);
    size_t getTrackId() const;
    char *getData() const;
    uint32_t getDataLen() const;
    uint32_t getPayloadLen() const;
    uint32_t getDataStringLen();
    size_t getDataStringLenOffset();
    JSON::Value toJSON() const;
    std::string toSummary() const;
    Scan getScan() const;

  protected:
    bool master;
    packType version;
    void resize(size_t size);
    char *data;
    uint32_t bufferLen;
    uint32_t dataLen;

    uint64_t prevNalSize;
  };

  /// A child class of DTSC::Packet, which allows overriding the packet time efficiently.
  class RetimedPacket : public Packet{
  public:
    RetimedPacket(uint64_t reTime){timeOverride = reTime;}
    RetimedPacket(uint64_t reTime, const Packet &rhs) : Packet(rhs){timeOverride = reTime;}
    RetimedPacket(uint64_t reTime, const char *data_, unsigned int len, bool noCopy = false)
        : Packet(data_, len, noCopy){
      timeOverride = reTime;
    }
    ~RetimedPacket(){}
    virtual uint64_t getTime() const{return timeOverride;}

  protected:
    uint64_t timeOverride;
  };

  class Parts{
  public:
    Parts(const Util::RelAccX &_parts);
    size_t getFirstValid() const;
    size_t getEndValid() const;
    size_t getValidCount() const;
    size_t getSize(size_t idx) const;
    uint64_t getDuration(size_t idx) const;
    int64_t getOffset(size_t idx) const;

  private:
    const Util::RelAccX &parts;
    Util::RelAccXFieldData sizeField;
    Util::RelAccXFieldData durationField;
    Util::RelAccXFieldData offsetField;
  };

  class Fragments{
  public:
    Fragments(const Util::RelAccX &_fragments);
    size_t getFirstValid() const;
    size_t getEndValid() const;
    size_t getValidCount() const;
    uint64_t getDuration(size_t idx) const;
    size_t getKeycount(size_t idx) const;
    size_t getFirstKey(size_t idx) const;
    size_t getSize(size_t idx) const;

  private:
    const Util::RelAccX &fragments;
  };

  class frameRateCalculator {
    public:
      frameRateCalculator();
      bool addTime(uint64_t t);
      size_t dataPoints;
      Util::ResizeablePointer data;
      uint64_t fpks;
      bool precise;
  };

  class Track{
  public:
    Util::RelAccX track;
    Util::RelAccX parts;
    Util::RelAccX keys;
    Util::RelAccX fragments;
    Util::RelAccX pages;
    Util::RelAccX frames;

    frameRateCalculator fpsCalc;

    // Internal buffers so we don't always need to search for everything
    Util::RelAccXFieldData trackIdField;
    Util::RelAccXFieldData trackTypeField;
    Util::RelAccXFieldData trackCodecField;
    Util::RelAccXFieldData trackFirstmsField;
    Util::RelAccXFieldData trackLastmsField;
    Util::RelAccXFieldData trackNowmsField;
    Util::RelAccXFieldData trackBpsField;
    Util::RelAccXFieldData trackMaxbpsField;
    Util::RelAccXFieldData trackLangField;
    Util::RelAccXFieldData trackInitField;
    Util::RelAccXFieldData trackRateField;
    Util::RelAccXFieldData trackSizeField;
    Util::RelAccXFieldData trackChannelsField;
    Util::RelAccXFieldData trackWidthField;
    Util::RelAccXFieldData trackHeightField;
    Util::RelAccXFieldData trackFpksField;
    Util::RelAccXFieldData trackEfpksField;
    Util::RelAccXFieldData trackMissedFragsField;

    Util::RelAccXFieldData partSizeField;
    Util::RelAccXFieldData partDurationField;
    Util::RelAccXFieldData partOffsetField;

    Util::RelAccXFieldData keyFirstPartField;
    Util::RelAccXFieldData keyBposField;
    Util::RelAccXFieldData keyDurationField;
    Util::RelAccXFieldData keyNumberField;
    Util::RelAccXFieldData keyPartsField;
    Util::RelAccXFieldData keyTimeField;
    Util::RelAccXFieldData keySizeField;

    Util::RelAccXFieldData fragmentDurationField;
    Util::RelAccXFieldData fragmentKeysField;
    Util::RelAccXFieldData fragmentFirstKeyField;
    Util::RelAccXFieldData fragmentSizeField;

    Util::RelAccXFieldData pageAvailField;
    Util::RelAccXFieldData pageFirstKeyField;
    Util::RelAccXFieldData pageFirstTimeField;

    Util::RelAccXFieldData framesTimeField;
    Util::RelAccXFieldData framesSizeField;
    Util::RelAccXFieldData framesDataField;
  };

  class Keys {
    public:
      Keys(Util::RelAccX & _keys);
      Keys(const Util::RelAccX & _keys);
      Keys(Track & _trk);
      Keys(const Track & _trk);
      size_t getFirstValid() const;
      size_t getEndValid() const;
      size_t getValidCount() const;
      size_t getFirstPart(size_t idx) const;
      size_t getBpos(size_t idx) const;
      uint64_t getDuration(size_t idx) const;
      size_t getNumber(size_t idx) const;
      size_t getParts(size_t idx) const;
      uint64_t getTime(size_t idx) const;
      void setSize(size_t idx, size_t _size);
      size_t getSize(size_t idx) const;

      uint64_t getTotalPartCount();
      uint32_t getIndexForTime(uint64_t timestamp);

      void applyLimiter(uint64_t _min, uint64_t _max, DTSC::Parts _p);
      void applyLimiter(uint64_t _min, uint64_t _max);

    private:
      bool isConst;
      bool isLimited;
      bool isFrames;
      size_t limMin;
      size_t limMax;
      // Overrides for max key
      size_t limMaxParts;
      uint64_t limMaxDuration;
      size_t limMaxSize;
      // Overrides for min key
      size_t limMinParts;
      size_t limMinFirstPart;
      uint64_t limMinDuration;
      uint64_t limMinTime;
      size_t limMinSize;

      Util::RelAccX empty;

      Util::RelAccX & keys;
      const Util::RelAccX & cKeys;

      Util::RelAccXFieldData firstPartField;
      Util::RelAccXFieldData bposField;
      Util::RelAccXFieldData durationField;
      Util::RelAccXFieldData numberField;
      Util::RelAccXFieldData partsField;
      Util::RelAccXFieldData timeField;
      Util::RelAccXFieldData sizeField;
  };

  class jitterTimer{
  public:
    uint64_t trueTime[8]; ///< Array of bootMS-based measurement points
    uint64_t packTime[8]; ///< Array of corresponding packet times
    uint64_t curJitter; ///< Maximum jitter measurement in the current time window so far
    unsigned int x; ///< Current indice within above two arrays
    uint64_t peaks[8]; ///< Highest jitter observed in each time window
    uint64_t maxJitter; ///< Highest jitter ever observed by this jitterTimer
    uint64_t lastTime; ///< Last packet used for a measurement point
    jitterTimer();
    uint64_t addPack(uint64_t t);
  };

  class Meta{
  public:
    Meta(const std::string &_streamName, const DTSC::Scan &src);
    Meta(const std::string &_streamName = "", bool master = true, bool autoBackOff = true);
    Meta(const std::string &_streamName, const std::string &fileName);

    ~Meta();
    void reInit(const std::string &_streamName, bool master = true, bool autoBackOff = true);
    void reInit(const std::string &_streamName, const std::string &fileName);
    void reInit(const std::string &_streamName, const DTSC::Scan &src);
    void addTrackFrom(const DTSC::Scan &src);

    void refresh();
    bool reloadReplacedPagesIfNeeded();

    operator bool() const;

    void setMaster(bool _master);
    bool getMaster() const;

    void clear();

    void minimalFrom(const Meta &src);

    bool trackLoaded(size_t idx) const;
    uint8_t trackValid(size_t idx) const;
    size_t trackCount() const;

    size_t addCopy(size_t source);
    size_t addDelayedTrack(size_t fragCount = DEFAULT_FRAGMENT_COUNT, size_t keyCount = DEFAULT_KEY_COUNT,
                           size_t partCount = DEFAULT_PART_COUNT, size_t pageCount = DEFAULT_PAGE_COUNT);
    size_t addTrack(size_t fragCount = DEFAULT_FRAGMENT_COUNT, size_t keyCount = DEFAULT_KEY_COUNT,
                    size_t partCount = DEFAULT_PART_COUNT, size_t pageCount = DEFAULT_PAGE_COUNT,
                    bool setValid = true, size_t frameSize = 0);
    void resizeTrack(size_t source, size_t fragCount = DEFAULT_FRAGMENT_COUNT, size_t keyCount = DEFAULT_KEY_COUNT,
                     size_t partCount = DEFAULT_PART_COUNT, size_t pageCount = DEFAULT_PAGE_COUNT, const char * reason = "",
                     size_t frameSize = 0);
    void initializeTrack(Track &t, size_t fragCount = DEFAULT_FRAGMENT_COUNT, size_t keyCount = DEFAULT_KEY_COUNT,
                         size_t parCount = DEFAULT_PART_COUNT, size_t pageCount = DEFAULT_PAGE_COUNT, size_t frameSize = 0);

    void merge(const DTSC::Meta &M, bool deleteTracks = true, bool copyData = true);

    void updatePosOverride(DTSC::Packet &pack, uint64_t bpos);
    void update(const DTSC::Packet &pack);
    void update(uint64_t packTime, int64_t packOffset, uint32_t packTrack, uint64_t packDataSize,
                uint64_t packBytePos, bool isKeyframe, uint64_t packSendSize = 0);

    size_t trackIDToIndex(size_t trackID, size_t pid = 0) const;

    std::string getTrackIdentifier(size_t idx, bool unique = false) const;
    uint64_t packetTimeToUnixMs(uint64_t pktTime, uint64_t systemBoot = 0) const;
    uint64_t unixMsToPacketTime(uint64_t unixTime, uint64_t systemBoot = 0) const;

    void setInit(size_t trackIdx, const std::string &init);
    void setInit(size_t trackIdx, const char *init, size_t initLen);
    std::string getInit(size_t idx) const;

    void setSource(const std::string &src);
    std::string getSource() const;

    void setID(size_t trackIdx, size_t id);
    size_t getID(size_t trackIdx) const;

    void markUpdated(size_t trackIdx);
    uint64_t getLastUpdated(size_t trackIdx) const;
    uint64_t getLastUpdated() const;

    void setChannels(size_t trackIdx, uint16_t channels);
    uint16_t getChannels(size_t trackIdx) const;

    void setRate(size_t trackIdx, uint32_t rate);
    uint32_t getRate(size_t trackIdx) const;

    void setWidth(size_t trackIdx, uint32_t width);
    uint32_t getWidth(size_t trackIdx) const;

    void setHeight(size_t trackIdx, uint32_t height);
    uint32_t getHeight(size_t trackIdx) const;

    void setSize(size_t trackIdx, uint16_t size);
    uint16_t getSize(size_t trackIdx) const;

    void setType(size_t trackIdx, const std::string &type);
    std::string getType(size_t trackIdx) const;

    void setCodec(size_t trackIdx, const std::string &codec);
    std::string getCodec(size_t trackIdx) const;

    void setLang(size_t trackIdx, const std::string &lang);
    std::string getLang(size_t trackIdx) const;

    void setFirstms(size_t trackIdx, uint64_t firstms);
    uint64_t getFirstms(size_t trackIdx) const;

    void setLastms(size_t trackIdx, uint64_t lastms);
    uint64_t getLastms(size_t trackIdx) const;

    void setNowms(size_t trackIdx, uint64_t nowms);
    void upNowms(size_t trackIdx, uint64_t nowms);
    uint64_t getNowms(size_t trackIdx) const;

    uint64_t getDuration(size_t trackIdx) const;

    void setBps(size_t trackIdx, uint64_t bps);
    uint64_t getBps(size_t trackIdx) const;

    void setMaxBps(size_t trackIdx, uint64_t bps);
    uint64_t getMaxBps(size_t trackIdx) const;

    void setFpks(size_t trackIdx, uint64_t bps);
    uint64_t getFpks(size_t trackIdx) const;

    void setEfpks(size_t trackIdx, uint64_t bps);
    uint64_t getEfpks(size_t trackIdx) const;

    void setMissedFragments(size_t trackIdx, uint32_t missedFragments);
    uint32_t getMissedFragments(size_t trackIdx) const;

    void setMinKeepAway(size_t trackIdx, uint64_t minKeepAway);
    uint64_t getMinKeepAway(size_t trackIdx) const;

    void setMaxKeepAway(uint64_t maxKeepAway);
    uint64_t getMaxKeepAway() const;

    bool claimTrack(size_t trackIdx);
    bool isClaimed(size_t trackIdx) const;
    uint64_t isClaimedBy(size_t trackIdx) const;
    void abandonTrack(size_t trackIdx);
    bool hasEmbeddedFrames(size_t trackIdx) const;
    bool getEmbeddedData(size_t trackIdx, size_t num, char * & dataPtr, size_t & dataLen) const;
    bool getEmbeddedTime(size_t trackIdx, size_t num, uint64_t & time) const;

    /*LTS-START*/
    void setSourceTrack(size_t trackIdx, size_t sourceTrack);
    uint64_t getSourceTrack(size_t trackIdx) const;

    void setEncryption(size_t trackIdx, const std::string &encryption);
    std::string getEncryption(size_t trackIdx) const;

    void setPlayReady(size_t trackIdx, const std::string &playReady);
    std::string getPlayReady(size_t trackIdx) const;

    void setWidevine(size_t trackIdx, const std::string &widevine);
    std::string getWidevine(size_t trackIdx) const;

    void setIvec(size_t trackIdx, uint64_t ivec);
    uint64_t getIvec(size_t trackIdx) const;

    void setMinimumFragmentDuration(uint64_t newFragmentDuration = DEFAULT_FRAGMENT_DURATION);
    uint64_t getMinimumFragmentDuration() const;
    /*LTS-END*/
    /*LTS-START
    uint64_t getFragmentDuration() const{return DEFAULT_FRAGMENT_DURATION;}
    LTS-END*/

    void setVod(bool vod);
    bool getVod() const;

    void setLive(bool live);
    bool getLive() const;

    bool hasBFrames(size_t idx = INVALID_TRACK_ID) const;

    void setBufferWindow(uint64_t bufferWindow);
    uint64_t getBufferWindow() const;

    void setBootMsOffset(int64_t bootMsOffset);
    int64_t getBootMsOffset() const;

    void setUTCOffset(int64_t UTCOffset);
    int64_t getUTCOffset() const;

    std::set<size_t> getValidTracks(bool skipEmpty = false) const;
    std::set<size_t> getMySourceTracks(size_t pid) const;

    void validateTrack(size_t trackIdx, uint8_t validType = TRACK_VALID_ALL);
    void removeEmptyTracks();
    void removeTrack(size_t trackIdx);
    bool removeFirstKey(size_t trackIdx);

    size_t mainTrack() const;
    uint32_t biggestFragment(uint32_t idx = INVALID_TRACK_ID) const;
    bool tracksAlign(size_t idx1, size_t idx2) const;

    uint64_t getTimeForFragmentIndex(uint32_t idx, uint32_t fragmentIdx) const;
    uint32_t getFragmentIndexForTime(uint32_t idx, uint64_t timestamp) const;

    uint64_t getTimeForKeyIndex(uint32_t idx, uint32_t keyIdx) const;
    uint32_t getKeyIndexForTime(uint32_t idx, uint64_t timestamp) const;

    uint32_t getPartIndex(uint64_t timestamp, size_t idx) const;
    uint64_t getPartTime(uint32_t partIndex, size_t idx) const;

    bool nextPageAvailable(uint32_t idx, size_t currentPage) const;
    void getPageNumbersForTime(uint32_t idx, uint64_t time, size_t & currPage, size_t & nextPage) const;
    size_t getPageNumberForKey(uint32_t idx, uint64_t keynumber) const;
    size_t getKeyNumForTime(uint32_t idx, uint64_t time) const;
    bool keyTimingsMatch(size_t idx1, size_t idx2) const;

    const Util::RelAccX &parts(size_t idx) const;
    Util::RelAccX &keys(size_t idx);
    const Util::RelAccX &keys(size_t idx) const;
    const Util::RelAccX &fragments(size_t idx) const;
    Util::RelAccX &pages(size_t idx);
    const Util::RelAccX &pages(size_t idx) const;

    const Keys getKeys(size_t trackIdx, bool applyLimiter = true) const;

    void storeFrame(size_t trackIdx, uint64_t time, const char * data, size_t dataSize);

    std::string toPrettyString() const;

    void remap(const std::string &_streamName = "");

    uint64_t getSendLen(bool skipDynamic = false, std::set<size_t> selectedTracks = std::set<size_t>()) const;
    void toFile(const std::string &uri) const;
    void send(Socket::Connection &conn, bool skypDynamic = false,
              std::set<size_t> selectedTracks = std::set<size_t>(), bool reID = false) const;
    void toJSON(JSON::Value &res, bool skipDynamic = true, bool tracksOnly = false) const;

    std::string getStreamName() const{return streamName;}

    JSON::Value inputLocalVars;

    uint8_t version;

    void getHealthJSON(JSON::Value & returnReference) const;

    void removeLimiter();
    void applyLimiter(uint64_t min, uint64_t max);

    void ignorePid(uint64_t ignPid);

  protected:
    uint64_t ignoredPid;
    void sBufMem(size_t trackCount = DEFAULT_TRACK_COUNT);
    void sBufShm(const std::string &_streamName, size_t trackCount = DEFAULT_TRACK_COUNT, bool master = true, bool autoBackOff = true);
    void streamInit(size_t trackCount = DEFAULT_TRACK_COUNT);
    void updateFieldDataReferences();
    void resizeTrackList(size_t newTrackCount);
    void preloadTrackFields();

    std::string streamName;

    IPC::sharedPage streamPage;
    Util::RelAccX stream;
    Util::RelAccX trackList;
    std::map<size_t, Track> tracks;
    std::map<size_t, IPC::sharedPage> tM;

    bool isMaster;
    uint64_t limitMin;
    uint64_t limitMax;
    bool isLimited;

    char *streamMemBuf;
    bool isMemBuf;
    std::map<size_t, char *> tMemBuf;
    std::map<size_t, size_t> sizeMemBuf;

  private:
    std::map<size_t, jitterTimer> theJitters;
    // Internal buffers so we don't always need to search for everything
    Util::RelAccXFieldData streamVodField;
    Util::RelAccXFieldData streamLiveField;
    Util::RelAccXFieldData streamSourceField;
    Util::RelAccXFieldData streamTracksField;
    Util::RelAccXFieldData streamMaxKeepAwayField;
    Util::RelAccXFieldData streamBufferWindowField;
    Util::RelAccXFieldData streamBootMsOffsetField;
    Util::RelAccXFieldData streamUTCOffsetField;
    Util::RelAccXFieldData streamMinimumFragmentDurationField;

    Util::RelAccXFieldData trackValidField;
    Util::RelAccXFieldData trackIdField;
    Util::RelAccXFieldData trackTypeField;
    Util::RelAccXFieldData trackCodecField;
    Util::RelAccXFieldData trackPageField;
    Util::RelAccXFieldData trackLastUpdateField;
    Util::RelAccXFieldData trackPidField;
    Util::RelAccXFieldData trackMinKeepAwayField;
    Util::RelAccXFieldData trackSourceTidField;
    Util::RelAccXFieldData trackEncryptionField;
    Util::RelAccXFieldData trackIvecField;
    Util::RelAccXFieldData trackWidevineField;
    Util::RelAccXFieldData trackPlayreadyField;
  };
}// namespace DTSC
