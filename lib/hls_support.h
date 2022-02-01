#include "comms.h"
#include "dtsc.h"
#include <cmath>

namespace HLS{
  // TODO: Implement logic to detect ideal partial fragment size
  const uint32_t partDurationMaxMs = 500; ///< max partial fragment duration in ms

  /// A struct containing data regarding fragments in a particular track
  /// needed for media manifest generation
  struct FragmentData{
    uint64_t firstFrag;
    uint64_t lastFrag;
    uint64_t currentFrag;
    uint64_t startTime;
    uint64_t duration;
    uint64_t lastMs;
    uint64_t partNum; ///< partial fragment number in base 0
  };

  /// A struct containing data regarding a particular track for media manifest generation
  struct TrackData{
    bool isLive;
    bool isVideo;
    bool noLLHLS;
    std::string mediaFormat;   ///< ".m4s" or ".ts"
    std::string encryptMethod; ///< "NONE", "AES-128", or "SAMPLE-AES"
    std::string sessionId;     ///< "" if not applicable
    size_t timingTrackId;
    size_t requestTrackId;
    uint32_t targetDurationMax; ///< Max duration of any fragment in the stream in s
    uint64_t initMsn;           ///< Initial fragment to start with in the media manifest
    uint64_t listLimit;         ///< Max number of fragments to include in the media manifest
    std::string urlPrefix;      ///< for CDN chunk serving
    uint64_t systemBoot;        ///< duration in ms since boot
    int64_t bootMsOffset;       ///< time diff between systemBoot & stream's 0 time in ms
  };

  /// A struct containing http variable data for LLHLS, can be empty strings
  struct HlsSpecData{
    std::string hlsSkip; ///< "YES" or "v2"
    std::string hlsMsn;  ///< requested fragment number
    std::string hlsPart; ///< requested partial fragment number
  };

  /// A struct containing stream related data needed to generate master manifest
  struct MasterData{
    bool hasSessId;
    bool noLLHLS;
    bool isTS;
    size_t mainTrack;
    std::string userAgent; ///< See HLS::getLiveLengthLimit(<args>) for more info
    std::string sessId;    ///< Session ID if applicable
    uint64_t systemBoot;   ///< duration in ms since boot
    int64_t bootMsOffset;  ///< time diff between systemBoot & stream's 0 time in ms
  };

  uint32_t blockPlaylistReload(const DTSC::Meta &M, const std::map<size_t, Comms::Users> &userSelect, const TrackData &trackData,
                               const HlsSpecData &hlsSpecData, const DTSC::Fragments &fragments,
                               const DTSC::Keys &keys);

  void populateFragmentData(const DTSC::Meta &M, const std::map<size_t, Comms::Users> &userSelect, FragmentData &fragData, const TrackData &trackData,
                            const DTSC::Fragments &fragments, const DTSC::Keys &keys);

  void addEndingTags(std::stringstream &result, const DTSC::Meta &M,
                     const std::map<size_t, Comms::Users> &userSelect, const FragmentData &fragData,
                     const TrackData &trackData);

  size_t getTimingTrackId(const DTSC::Meta &M, const std::string &mTrack, const size_t mSelTrack);

  void addStartingMetaTags(std::stringstream &result, FragmentData &fragData,
                           const TrackData &trackData, const HlsSpecData &hlsSpecData);

  void addMediaFragments(std::stringstream &result, const DTSC::Meta &M, FragmentData &fragData,
                         const TrackData &trackData, const DTSC::Fragments &fragments,
                         const DTSC::Keys &keys);

  void addMasterManifest(std::stringstream &result, const DTSC::Meta &M,
                         const std::map<size_t, Comms::Users> &userSelect,
                         const MasterData &masterData);

  uint64_t getPartTargetTime(const DTSC::Meta &M, const uint32_t idx, const uint32_t mTrack,
                             const uint64_t startTime, const uint64_t msn, const uint32_t part);
}// namespace HLS
