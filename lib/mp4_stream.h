#include "dtsc.h"
#include "util.h"
#include "mp4_generic.h"

namespace MP4{

  class PartTime{
  public:
    PartTime() : time(0), duration(0), offset(0), trackID(0), bpos(0), size(0), index(0){}
    bool operator<(const PartTime &rhs) const{
      if (time < rhs.time){return true;}
      if (time > rhs.time){return false;}
      if (trackID < rhs.trackID){return true;}
      return (trackID == rhs.trackID && bpos < rhs.bpos);
    }
    uint64_t time;
    uint64_t duration;
    int32_t offset;
    size_t trackID;
    uint64_t bpos;
    uint32_t size;
    uint64_t index;
    bool keyframe;
  };


  class TrackHeader{
  public:
    TrackHeader();

    /// Reads (new) track header information for processing
    void read(TRAK &trakBox);
    /// Reads (new) track header information for processing
    void read(TREX &trexBox);
    /// Reads (new) track header information for processing
    void read(TRAF &trafBox);

    /// Signal that we're going to be reading the next moof box now.
    /// Wipes internal TRAF boxes, ensures TRAF mode is enabled so no reads happen from MOOV headers anymore.
    void nextMoof();

    /// Switch back to non-moof reading mode, disabling TRAF mode and wiping all TRAF boxes
    void revertToMoov();

    /// Returns true if we know how to parse this track, false otherwise
    bool compatible() const {return isCompatible;}

    /// Retrieves the information associated with a specific part (=frame).
    void getPart(uint64_t index, uint64_t * byteOffset = 0, uint32_t * byteLen = 0, uint64_t * time = 0, int32_t * timeOffset = 0, bool * keyFrame = 0, uint64_t moofPos = 0);

    /// Returns the number of parts this track header contains
    uint64_t size() const;

    // Information about the track. Public for convenience, but setting them has no effect.
    // The exception is sType, which affects processing of the data in some cases and should not be written to.
    // All of these are filled by the read() function when reading an MP4::TRAK box.
    size_t trackId; ///< MP4-internal ID for this track
    uint64_t timeScale; ///< Timescale in units per second
    std::string sType; ///< MP4-internal codec name for this track - do not write to externally!
    std::string codec; ///< Mist codec name for this track
    std::string trackType; ///< Which Mist-compatible track type this is
    std::string initData; ///< Initialization data for the track, in Mist-compatible format
    std::string lang; ///< Language of the track
    uint32_t vidWidth, vidHeight;
    uint32_t audChannels, audRate, audSize;

  private:
    /// Internal function that increases the time of the current part to the next part
    void increaseTime(uint32_t delta);

    // next variables are needed for the stsc/stco loop
    uint64_t bposIndex; ///< Current read index in stsc box
    uint64_t bposSample; ///< First sample number in current chunk entry
    // next variables are needed for the stts loop
    uint64_t timeIndex; ///< Index in STTS box
    uint64_t timeSample;   ///< Sample counter for STTS box
    uint64_t timeFirstSample;   ///< First sample in STTS box entry
    uint64_t timeTotal; ///< Total timestamp for STTS box
    uint64_t timeExtra; ///< Extra timestamp for STTS box
    uint64_t offsetIndex; ///< Index in CTTS box
    uint64_t offsetSample; ///< First sample number in CTTS entry
    uint64_t keyIndex; ///< Index in stss box
    uint64_t keySample; ///< First sample number in stss entry

    STSS stssBox; ///< keyframe list
    STCO stcoBox; ///< positions of chunks (32-bit)
    CO64 co64Box; ///< positions of chunks (64-bit)
    STSZ stszBox; ///< packet sizes
    STTS sttsBox; ///< packet durations
    CTTS cttsBox; ///< packet time offsets (optional)
    STSC stscBox; ///< packet count per chunk
    TREX trexBox; ///< packet count per chunk
    TREX * trexPtr; ///< Either 0 or pointer to trexBox member
    std::deque<TRAF> trafs; ///< Current traf boxes, if any
    bool stco64; // 64 bit chunk offsets?
    bool hasOffsets; ///< Are time offsets present?
    bool hasKeys; ///< Are keyframes listed?
    bool isVideo; ///< Is this a video track?
    bool isCompatible; ///< True if Mist supports this track type
    bool trafMode; ///< True if we are ignoring the moov headers and only looking at traf headers
  };

  class Stream{
  public:
    Stream();
    ~Stream();
    void open(Util::ResizeablePointer & ptr);
    bool hasPacket(size_t tid) const;
    bool hasPacket() const;
    void getPacket(size_t tid, DTSC::Packet &pack, uint64_t &thisTime, size_t &thisIdx);
    uint32_t getEarliestPID();
    void getEarliestPacket(DTSC::Packet &pack, uint64_t &thisTime, size_t &thisIdx);
    void initializeMetadata(DTSC::Meta &meta, size_t tid = INVALID_TRACK_ID, size_t mappingId = INVALID_TRACK_ID);
  private:
    std::map<size_t, TrackHeader> trkHdrs;
    std::map<size_t, std::string> codecs;
    std::set<MP4::PartTime> curPositions;
    MOOV moovBox;
    Box mdatBox;
  };

} // namespace MP4

