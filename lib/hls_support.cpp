#include "hls_support.h"
#include "langcodes.h" /*LTS*/
#include "stream.h"
#include <cstdlib>
#include <iomanip>

namespace HLS{

  // TODO: Prefix could be made better
  // Needed for grouping renditions in master manifest
  const std::string groupIdPrefix = "vid-";

  // max partial fragment duration in s
  const float partDurationMax = (partDurationMaxMs + 4) / 1000.0;

  // TODO: Advance Part Limit
  /*If the _HLS_msn is greater than the Media Sequence Number of the last
  Media Segment in the current Playlist plus two, or if the _HLS_partIf the _HLS_msn is greater than
  the Media Sequence Number of the last Media Segment in the current Playlist plus two, or if the
  _HLS_part exceeds the last Partial Segment in the current Playlist by the Advance Part Limit, then
  the server SHOULD immediately return Bad Request, such as HTTP 400 exceeds the last Partial
  Segment in the current Playlist by the Advance Part Limit, then the server SHOULD immediately
  return Bad Request, such as HTTP 400*/
  // NOTE: Not implementing this gives freedom to play with jitter duration
  const uint32_t advancePartLimit =
      std::min((int)std::ceil(3.0 / partDurationMax), 3); // Ref: RFC8216, 6.2.5.2

  const struct{
    bool pdu;   ///< Playlist Delta Update
    bool pduV2; ///< TODO: Playlist Delta Update v2: skips EXT-X-DATERANGE
    bool bpr;   ///< Blocking Playlist Reload
    bool parts; ///< Partial Fragments
    bool tags;  ///< True if any of the above is true
  }serverSupport ={
#ifdef NOLLHLS
      false, false, false, false, false,
#else
      true, false, true, true, true,
#endif
  };

  /// lastms calculation incorporating jitter duration
  /// Always ensures the (lastms <= current time - jitter duration)
  uint64_t getLastms(const DTSC::Meta &M, const std::map<size_t, Comms::Users> &userSelect,
                     const size_t trackIdx, const uint64_t streamStartTime){
    std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin();
    uint64_t maxJitter = 0;
    u_int64_t minKeepAway = 0;
    for (; it != userSelect.end(); it++){
      minKeepAway = M.getMinKeepAway(it->first);
      if (minKeepAway > maxJitter){maxJitter = minKeepAway;}
    }
    return std::min(M.getLastms(trackIdx), Util::unixMS() - streamStartTime - minKeepAway);
  }

  /// Calculate HLS media playlist version compatibility
  /// \return version number
  uint16_t calcManifestVersion(const std::string &hlsSkip){
    // Server and Client support skipping media segments
    if (serverSupport.tags && serverSupport.pdu && (hlsSkip.compare("YES") == 0)){return 9;}
    // Server and Client support skipping ext-x-daterange along with media segments
    if (serverSupport.tags && serverSupport.pduV2 && (hlsSkip.compare("v2") == 0)){return 10;}
    // Default, lowest version supported
    return 6;
  }

  /// returns the main track id provided in master manifest if valid
  /// else returns the current valid main track id
  size_t getTimingTrackId(const DTSC::Meta &M, const std::string &mTrack, const size_t mSelTrack){
    return (mTrack.size() && (M.getValidTracks().count(atoll(mTrack.c_str()))))
               ? atoll(mTrack.c_str())
               : mSelTrack;
  }

  /// Return live edge fragment duration
  uint64_t getLastFragDur(const DTSC::Meta &M, const std::map<size_t, Comms::Users> &userSelect, const TrackData &trackData, const uint64_t hlsMsnNr,
                          const DTSC::Fragments &fragments, const DTSC::Keys &keys){
    return std::min(
               getLastms(M, userSelect, trackData.timingTrackId, trackData.systemBoot + trackData.bootMsOffset),
               getLastms(M, userSelect, trackData.requestTrackId,
                         trackData.systemBoot + trackData.bootMsOffset)) -
           keys.getTime(fragments.getFirstKey(hlsMsnNr));
  }

  /// Waits until the requested fragment & partial fragment are available
  /// Returns 400 if specific part is requested without a specific MSN
  /// Returns 400 if requested MSN > the real live edge MSN plus two
  /// Returns 503 if time spent in BPR > 3x Target Duration
  uint32_t blockPlaylistReload(const DTSC::Meta &M, const std::map<size_t, Comms::Users> &userSelect, const TrackData &trackData,
                               const HlsSpecData &hlsSpecData, const DTSC::Fragments &fragments,
                               const DTSC::Keys &keys){
    // Return if forced noLLHLS
    if (trackData.noLLHLS){return 0;}

    // Check BPR request validity
    if (hlsSpecData.hlsMsn.empty() && hlsSpecData.hlsPart.size()){return 400;}
    if (atol(hlsSpecData.hlsMsn.c_str()) > (fragments.getEndValid() - 1 + 2)){return 400;}

    // BPR logic only if live & _HLS_msn requested
    if (trackData.isLive && hlsSpecData.hlsMsn.size()){
      DEBUG_MSG(5, "Requesting media playlist: Track %zu, MSN %s, part: %s",
                trackData.timingTrackId, hlsSpecData.hlsMsn.c_str(), hlsSpecData.hlsPart.c_str());

      uint64_t hlsMsnNr = atol(hlsSpecData.hlsMsn.c_str());
      uint64_t hlsPartNr = atol(hlsSpecData.hlsPart.c_str()) + 1; // base 1

      // if hlsPart empty (HLS spec) OR if fragment hlsMsn is complete
      // THEN request part 1 of MSN++
      if (hlsSpecData.hlsPart.empty()){hlsPartNr = 1;}
      if (fragments.getDuration(hlsMsnNr)){
        hlsMsnNr++;
        hlsPartNr = 1;
      }

      uint64_t lastFragmentDur = getLastFragDur(M, userSelect, trackData, hlsMsnNr, fragments, keys);
      std::ldiv_t res = std::ldiv(lastFragmentDur, partDurationMaxMs);
      DEBUG_MSG(5, "req MSN %" PRIu64 " fin MSN %zu, req Part %" PRIu64 " fin Part %ld", hlsMsnNr,
                (fragments.getEndValid() - 2), hlsPartNr, res.quot);

      // BPR Time limit = 3x Target Duration (per HLS spec)
      // + Jitter duration (per Mist feature)
      // + 1x Target Duration (extra margin of safety for jitters)
      int64_t bprTimeLimit = (4 * trackData.targetDurationMax * 1000) +
                             std::max(M.getMinKeepAway(trackData.timingTrackId),
                                      M.getMinKeepAway(trackData.requestTrackId));

      while (hlsPartNr > res.quot){
        if (bprTimeLimit < 1){return 503;}
        DEBUG_MSG(5, "Part Block: req %" PRIu64 " fin %ld", hlsPartNr, res.quot);
        Util::wait(partDurationMaxMs - res.rem + 25);
        bprTimeLimit -= (partDurationMaxMs - res.rem + 25);
        lastFragmentDur = getLastFragDur(M, userSelect, trackData, hlsMsnNr, fragments, keys);
        res = std::ldiv(lastFragmentDur, partDurationMaxMs);
      }
    }
    return 0;
  }

  /// Populate FragmentData struct to be used for media manifest generation
  void populateFragmentData(const DTSC::Meta &M, const std::map<size_t, Comms::Users> &userSelect, FragmentData &fragData, const TrackData &trackData,
                            const DTSC::Fragments &fragments, const DTSC::Keys &keys){
    fragData.lastMs = std::min(
        getLastms(M, userSelect, trackData.requestTrackId, trackData.systemBoot + trackData.bootMsOffset),
        getLastms(M, userSelect, trackData.timingTrackId, trackData.systemBoot + trackData.bootMsOffset));
    fragData.firstFrag = fragments.getFirstValid();
    if (trackData.isLive){
      fragData.lastFrag = M.getFragmentIndexForTime(trackData.timingTrackId, fragData.lastMs);
      if (fragments.getEndValid() > fragData.lastFrag){
        fragData.lastFrag = fragments.getEndValid();
      }
    }else{
      // Override to last fragment if VOD
      fragData.lastFrag = fragments.getEndValid() - 1;
    }
    fragData.currentFrag = fragData.firstFrag;
    fragData.startTime = keys.getTime(fragments.getFirstKey(fragData.currentFrag));
    fragData.duration = fragments.getDuration(fragData.currentFrag);

    // Playlist length limit logic:
    // Part 1: Limit any playlist with listlimit config
    if (trackData.listLimit &&
        (fragData.lastFrag - fragData.currentFrag > trackData.listLimit + 2)){
      fragData.currentFrag = fragData.lastFrag - trackData.listLimit;
    }

    // Part 2: Limit a playlist depending on initial MSN data
    // see the NOTE at HLS::getLiveLengthLimit(args)
    if (trackData.isLive && (fragData.lastFrag - fragData.currentFrag) > 2){
      fragData.currentFrag = std::max(trackData.initMsn, fragData.currentFrag + 2);
    }
  }

  /// Encryption logic to LLHLS playlist
  void hlsManifestMediaEncriptionTags(const DTSC::Meta &M, std::stringstream &result,
                                      const size_t timingTid){
    if (M.getEncryption(timingTid) == ""){
      result << "\r\n#EXT-X-KEY:METHOD=NONE";
    }else{
      // NOTE:
      // Defined encryption methods: NONE, AES-128, and SAMPLE-AES
      std::string method = M.getEncryption(timingTid);
      std::string uri = "asd";
      result << "\r\n#EXT-X-KEY:METHOD=" << method;
      result << ",URI=\"" << uri << "\"";
      //      if (version >= 5){
      //        result << "\",KEYFORMAT=\"com.apple.streamingkeydelivery\"";
      //        result << "";
      //}
    }
  }

  void addMsnTag(std::stringstream &result, const uint64_t msn){
    result << "#EXT-X-MEDIA-SEQUENCE:" << msn << "\r\n";
  }

  /// Returns skip boundary duration, calculated as 6x max target duration
  uint32_t hlsSkipBoundary(const uint32_t targetDurationMax){return targetDurationMax * 6;}

  /// Calculates the number full fragments that can be skipped from printing in the manifest
  /// and MUST REPLACE the associated tags
  void addMediaSkipTag(std::stringstream &result, FragmentData &fragData,
                       const TrackData &trackData, const uint16_t version){
    // NOTE: Skips supported from version >= 9
    // Version >=9 supports SKIPPED-SEGMENTS

    // TODO: Support for Version 10 playlists
    // NOTE: Not implemented only because there is no immediate demand from anyone.
    // Adds support for RECENTLY-REMOVED-DATERANGES

    if (version >= 9){
      uint32_t skips = 0;
      const uint32_t skipsFromEnd =
          hlsSkipBoundary(trackData.targetDurationMax) / trackData.targetDurationMax + 2;
      if ((fragData.lastFrag - fragData.currentFrag) > skipsFromEnd){
        skips = ((fragData.lastFrag - fragData.currentFrag) - skipsFromEnd);
      }

      if (version >= 10){
        // TODO: Implement logic for skip calculations date ranges
        skips += 0;
      }

      if (skips){
        result << "#EXT-X-SKIP:SKIPPED-SEGMENTS=" << skips << "\r\n";
        // TODO: Update with version 10 playlist implementation
        // result << ",RECENTLY-REMOVED-DATERANGES=";
        fragData.currentFrag += skips;
      }
    }
  }

  /// Append result with tags that indicates the server supports LLHLS delivery
  void addServerSupportTags(std::stringstream &result, const TrackData &trackData){
    if (trackData.noLLHLS || !trackData.isLive){return;}

    // TODO: Make ifdef
    if (serverSupport.tags){
      result << "#EXT-X-SERVER-CONTROL:";
      if (serverSupport.bpr){result << "CAN-BLOCK-RELOAD=YES,";}
      if (serverSupport.pdu){
        result << "CAN-SKIP-UNTIL=" << hlsSkipBoundary(trackData.targetDurationMax) << ",";
      }
      if (serverSupport.pduV2){result << "CAN-SKIP-DATERANGES=YES,";}
      if (serverSupport.parts){
        result << "PART-HOLD-BACK=" << partDurationMax * 3; // atleast 3x
        result << "\r\n#EXT-X-PART-INF:PART-TARGET=" << partDurationMax;
      }
      result << "\r\n";
    }
  }

  void addTargetDuration(std::stringstream &result, const uint32_t targetDurationMax){
    uint32_t dur = targetDurationMax;
    if (dur <= 0) {
      dur = 1;
    }
    result << "#EXT-X-TARGETDURATION:" << dur << "\r\n";
  }

  /// Appends result with encrytion / drm data
  void addEncriptionTags(std::stringstream &result, const std::string &encryptMethod){
    // TODO: Add support for media encryption
    if (encryptMethod.size()){
      // NOTE:
      // Defined encryption methods: NONE, AES-128, and SAMPLE-AES
      std::string uri = "asd";
      result << "#EXT-X-KEY:METHOD=" << encryptMethod;
      result << ",URI=\"" << uri << "\"\r\n";
      //      if (version >= 5){
      //        result << "\",KEYFORMAT=\"com.apple.streamingkeydelivery\"";
      //        result << "";
      //}
    }
  }

  void addInitTags(std::stringstream &result, const TrackData &trackData){
    // No init data for TS
    if (trackData.mediaFormat == ".ts"){return;}

    result << "#EXT-X-MAP:URI=\"" << trackData.urlPrefix << "init" << trackData.mediaFormat;
    if (trackData.sessionId.size()){result << "?tkn=" << trackData.sessionId;}
    result << "\"\r\n";
  }

  void addMediaBasicTags(std::stringstream &result, const uint16_t version){
    result << "#EXTM3U\r\n";
    result << "#EXT-X-VERSION:" << version << "\r\n";
  }

  /// Append result with media meta tags that are in the beginning of the manifest
  void addStartingMetaTags(std::stringstream &result, FragmentData &fragData,
                           const TrackData &trackData, const HlsSpecData &hlsSpecData){
    const uint16_t version = calcManifestVersion(hlsSpecData.hlsSkip);
    addMediaBasicTags(result, version);
    addServerSupportTags(result, trackData);
    addInitTags(result, trackData);
    addEncriptionTags(result, trackData.encryptMethod);
    addTargetDuration(result, trackData.targetDurationMax);
    addMsnTag(result, trackData.isLive ? fragData.currentFrag : fragData.firstFrag);
    // NOTE: DO NOT move the SKIP tag. Order must be respected per HLS spec.
    addMediaSkipTag(result, fragData, trackData, version);
  }

  /// Appends result with prependStr and timestamp calculated from current time in ms
  void addDateTimeTag(std::stringstream &result, const std::string &prependStr,
                      const uint64_t unixMs){
    result << prependStr << Util::getUTCStringMillis(unixMs) << "\r\n";
  }

  /// Add segment tag to LLHLS playlist
  void addFragmentTag(std::stringstream &result, const FragmentData &fragData,
                      const TrackData &trackData){
    result << "#EXTINF:" << std::fixed << std::setprecision(3) << fragData.duration / 1000.0
           << ",\r\n";

    // NOTE: HLS spec says it isn't mandatory to add date time tag for every fragment.
    // Tests show that there is definitely an influence on consistency for live streams.
    // Printing the tag for every fragment tag was the best.
    if (trackData.isLive){
      addDateTimeTag(result, "#EXT-X-PROGRAM-DATE-TIME:",
                     trackData.systemBoot + trackData.bootMsOffset + fragData.startTime);
    }

    result << trackData.urlPrefix << "chunk_" << fragData.startTime << trackData.mediaFormat;
    result << "?msn=" << fragData.currentFrag;
    result << "&mTrack=" << trackData.timingTrackId;
    result << "&dur=" << fragData.duration;
    if (trackData.sessionId.size()){result << "&tkn=" << trackData.sessionId;}
    result << "\r\n";
  }

  /// Add partial segment tag to LLHLS playlist
  void addPartialTag(std::stringstream &result, const DTSC::Meta &M, const DTSC::Keys &keys,
                     const FragmentData &fragData, const TrackData &trackData,
                     const uint32_t partCount, const uint32_t duration){
    result << "#EXT-X-PART:DURATION=" << duration / 1000.0;
    result << ",URI=\"" << trackData.urlPrefix;
    result << "chunk_" << fragData.startTime << "." << partCount << trackData.mediaFormat;
    result << "?msn=" << fragData.currentFrag;
    result << "&mTrack=" << trackData.timingTrackId;
    result << "&dur=" << duration;
    if (trackData.sessionId.size()){result << "&tkn=" << trackData.sessionId;}
    result << "\"";

    // NOTE: INDEPENDENT tags, specified ONLY for VIDEO tracks, indicate the first partial fragment
    // closest to the before (live edge - PART-HOLD-BACK) time that a client starts playback from.
    if (trackData.isVideo){
      uint64_t partStartTime = fragData.startTime + partCount * partDurationMaxMs;
      uint32_t partKeyIdx = M.getKeyIndexForTime(trackData.timingTrackId, partStartTime);
      uint64_t partKeyIdxTime = M.getTimeForKeyIndex(trackData.timingTrackId, partKeyIdx);
      if (partKeyIdxTime == partStartTime){result << ",INDEPENDENT=YES";}
    }
    result << "\r\n";
  }

  /// Appends result with partial fragment tags if supported/requested
  void addPartialFragmentTags(std::stringstream &result, const DTSC::Meta &M,
                              FragmentData &fragData, const TrackData &trackData,
                              const DTSC::Keys &keys){
    if (trackData.noLLHLS){return;}

    // return if VOD, or no support for server tags, or no support for partial fragments
    if (!(trackData.isLive && serverSupport.tags && serverSupport.parts)){return;}

    // if fragment is last-but-4th or later
    // OR if fragment is 3 target durations from the end
    if ((fragData.lastFrag - fragData.currentFrag < 5) ||
        ((fragData.lastMs - fragData.startTime) <= 3 * trackData.targetDurationMax * 1000)){
      std::ldiv_t durationData = std::ldiv(fragData.duration, partDurationMaxMs);

      // General case: all partial segments with duration equal to partDurationMax
      uint32_t partCount = 0;
      for (partCount = 0; partCount < durationData.quot; partCount++){
        addPartialTag(result, M, keys, fragData, trackData, partCount, partDurationMaxMs);
      }

      // Special case: last partial segment (duration < partDurationMaxMs) in any fragment not at
      // live edge
      if (durationData.rem && (fragData.lastFrag - fragData.currentFrag > 1)){
        addPartialTag(result, M, keys, fragData, trackData, partCount, durationData.rem);
      }
      fragData.partNum = partCount;
    }
  }

  /// Appends result with partial fragment tags, date-time tag and fragment tag for the current
  /// fragment
  void addMediaTags(std::stringstream &result, const DTSC::Meta &M, FragmentData &fragData,
                    const TrackData &trackData, const DTSC::Keys &keys){
    addPartialFragmentTags(result, M, fragData, trackData, keys);

    // do not add the last fragment media tag for the live streams
    if (trackData.isLive && (fragData.currentFrag >= fragData.lastFrag - 2)){return;}

    addFragmentTag(result, fragData, trackData);
  }

  /// Appends result with partial fragment tags, date-time tags and fragment tags for all fragments
  void addMediaFragments(std::stringstream &result, const DTSC::Meta &M, FragmentData &fragData,
                         const TrackData &trackData, const DTSC::Fragments &fragments,
                         const DTSC::Keys &keys){
    for (; fragData.currentFrag < fragData.lastFrag; fragData.currentFrag++){
      fragData.startTime = keys.getTime(fragments.getFirstKey(fragData.currentFrag));

      // adjust fragment start time for vod
      if (!trackData.isLive){fragData.startTime -= M.getFirstms(trackData.timingTrackId);}

      fragData.duration = fragments.getDuration(fragData.currentFrag);
      // NOTE: If duration invalid, it's the last fragment, so calculate duration from live edge
      // Needed for LLHLS
      if (!fragData.duration){fragData.duration = fragData.lastMs - fragData.startTime;}

      addMediaTags(result, M, fragData, trackData, keys);
    }
  }

  void addVodEndingTags(std::stringstream &result){result << "#EXT-X-ENDLIST\r\n";}

  /// Append result with information on alternate renditions, only for LLHLS
  void addAltRenditionReports(std::stringstream &result, const DTSC::Meta &M,
                              const std::map<size_t, Comms::Users> &userSelect,
                              const FragmentData &fragData, const TrackData &trackData){
    DTSC::Fragments fragments(M.fragments(trackData.timingTrackId));
    std::ldiv_t altPart =
        std::ldiv(fragments.getDuration(fragData.currentFrag - 2), partDurationMaxMs);
    std::map<size_t, Comms::Users>::const_iterator it = userSelect.end();
    for (; it != userSelect.end(); it++){
      if (it->first == trackData.timingTrackId){continue;}
      result << "#EXT-X-RENDITION-REPORT:";
      result << "URI=\"" << it->first << "/index.m3u8\"";
      if (fragData.partNum){
        result << ",LAST-MSN=" << fragData.currentFrag - 1;
        result << ",LAST-PART=" << fragData.partNum - 1 << "\r\n";
      }else{
        result << ",LAST-MSN=" << fragData.currentFrag - 2;
        result << ",LAST-PART=" << ((altPart.quot - 1) + (altPart.rem ? 1 : 0)) << "\r\n";
      }
    }
  }

  /// Append result with hinted part, only for LLHLS
  void addPreloadHintTag(std::stringstream &result, const FragmentData &fragData,
                         const TrackData &trackData){
    result << "#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"" << trackData.urlPrefix << "chunk_";
    result << fragData.startTime << "." << fragData.partNum << trackData.mediaFormat;
    result << "?msn=" << fragData.currentFrag - 1;
    result << "&mTrack=" << trackData.timingTrackId;
    result << "&dur=" << partDurationMaxMs;
    if (trackData.sessionId.size()){result << "&tkn=" << trackData.sessionId;}
    result << "\"\r\n";
  }

  /// Append result with live ending tags (supports llhls tags)
  void addLiveEndingTags(std::stringstream &result, const DTSC::Meta &M,
                         const std::map<size_t, Comms::Users> &userSelect,
                         const FragmentData &fragData, const TrackData &trackData){
    if (trackData.noLLHLS){return;}
    if (serverSupport.tags && serverSupport.parts){
      addPreloadHintTag(result, fragData, trackData);
      addAltRenditionReports(result, M, userSelect, fragData, trackData);
    }
  }

  /// Add respective ending tags for VOD and Live streams
  void addEndingTags(std::stringstream &result, const DTSC::Meta &M,
                     const std::map<size_t, Comms::Users> &userSelect, const FragmentData &fragData,
                     const TrackData &trackData){
    trackData.isLive ? addLiveEndingTags(result, M, userSelect, fragData, trackData)
                     : addVodEndingTags(result);
  }

  /// Check if keyframes of given trackId are aligned with that of the main track
  /// returns true if aligned
  /// keyframe alginment is a MUST for LLHLS track switch
  bool checkFramesAlignment(std::stringstream &result, const DTSC::Meta &M,
                            const MasterData &masterData, const size_t trackId){
    if (masterData.noLLHLS || !serverSupport.tags) {
      return true;
    }
    bool keyFramesAligned =
        masterData.mainTrack == trackId || M.keyTimingsMatch(masterData.mainTrack, trackId);
    if (!keyFramesAligned){
      result << "## NOTE: Track " << trackId
             << " is available, but ignored because it is not aligned with track "
             << masterData.mainTrack << ".\r\n";
    }
    return keyFramesAligned;
  }

  /// Adds EXT-X-MEDIA tag for a given trackId
  void addExtXMediaTags(std::stringstream &result, const DTSC::Meta &M,
                        const MasterData &masterData, const size_t trackId,
                        const std::string &mediaType, const std::string &grpid,
                        const uint64_t iFrag){
    std::string lang = "";
    lang = M.getLang(trackId).empty() ? "und" : M.getLang(trackId);
    std::string name = M.getCodec(trackId) + "-";
    if (lang == "und"){
      char intStr[10];
      snprintf(intStr, 10, "%zu", trackId);
      name += intStr;
    }else{
      name += lang;
    }
    result << "#EXT-X-MEDIA:TYPE=" << mediaType;
    result << ",GROUP-ID=\"" << grpid << "\"";
    result << ",LANGUAGE=\"" << lang;
    if (lang == "und"){result << "-" << trackId;}
    result << "\"";
    result << ",NAME=\"" << name << "\",URI=\"" << trackId << "/index.m3u8";
    result << "?mTrack=" << masterData.mainTrack;
    result << "&iMsn=" << iFrag;
    if (masterData.sessId.size()){result << "&tkn=" << masterData.sessId;}
    if (masterData.noLLHLS){result << "&llhls=0";}
    result << "\"\r\n";
  }

  /// Add HLS basic tags for master manifest
  void addMasterBasicTags(std::stringstream &result){
    result.str(std::string()); // reset the stream to empty
    result << "#EXTM3U\r\n#EXT-X-INDEPENDENT-SEGMENTS\r\n";
  }

  void addInfTrackTag(std::stringstream &result, const MasterData &masterData,
                      const std::set<size_t> &aTracks, const size_t tid, const uint64_t iFrag,
                      const bool keyFramesAligned, const bool isVideo){
    result << (keyFramesAligned ? "" : "## DISABLED: ");
    result << tid;
    if (isVideo && masterData.isTS && aTracks.size() == 1){result << "_" << *aTracks.begin();}
    result << "/index.m3u8";
    result << "?mTrack=" << masterData.mainTrack;
    result << "&iMsn=" << iFrag;
    if (masterData.sessId.size()){result << "&tkn=" << masterData.sessId;}
    if (masterData.noLLHLS){result << "&llhls=0";}
    result << "\r\n";
  }

  void addInfBWidthTag(std::stringstream &result, const uint64_t bWidth){
    result << std::fixed << std::setprecision(0);
    result << ",BANDWIDTH=" << bWidth * 1.3 << ",AVERAGE-BANDWIDTH=" << bWidth * 1.1 << "\r\n";
  }

  void addInfResolFrameRate(std::stringstream &result, const DTSC::Meta &M,
                            const std::string &resolution, const size_t trackId){
    result << ",RESOLUTION=" << resolution;
    if (M.getFpks(trackId)){result << ",FRAME-RATE=" << (float)M.getFpks(trackId) / 1000;}
  }

  void addInfCodecsTag(std::stringstream &result, const DTSC::Meta &M, const size_t tid,
                       const std::string &audCodecsStr){
    result << "CODECS=\"" << Util::codecString(M.getCodec(tid), M.getInit(tid));
    result << audCodecsStr << "\"";
  }

  /// creates group id based on the resolution of the track
  void getGroupId(std::stringstream &grpid, const DTSC::Meta &M, const size_t tid){
    grpid.str(std::string()); // reset the stream to empty
    grpid << groupIdPrefix << M.getWidth(tid) << "x" << M.getHeight(tid);
  }

  void addInfMainTag(std::stringstream &result){result << "#EXT-X-STREAM-INF:";}

  /// add #EXT-X-STREAM-INF for audio only streams
  void addAudInfStreamTags(std::stringstream &result, const DTSC::Meta &M,
                           const MasterData &masterData, const std::set<size_t> &aTracks,
                           const uint64_t iFrag){
    if (aTracks.size()){
      for (std::set<size_t>::iterator ita = aTracks.begin(); ita != aTracks.end(); ita++){
        uint64_t bWidth = M.getBps(*ita);
        bWidth = (bWidth < 5 ? 5 : bWidth) * 8;
        addInfMainTag(result);
        addInfCodecsTag(result, M, *ita, "");
        addInfBWidthTag(result, bWidth);
        addInfTrackTag(result, masterData, aTracks, *ita, iFrag, true, false);
      }
    }
  }

  /// Add #EXT-X-STREAM-INF tags for video groups
  void addVidInfStreamTags(std::stringstream &result, const DTSC::Meta &M,
                           const MasterData &masterData, const std::set<std::string> &aCodecs,
                           const std::set<size_t, std::less<size_t> > &vTracks,
                           const std::set<size_t> &aTracks,
                           const std::multimap<std::string, size_t> &vidGroups,
                           const uint64_t asBWidth, const uint64_t iFrag,
                           const uint32_t sTracksSize){
    // Create a comma separated string containing all audio codecs
    std::string audCodecsStr = ""; // comma separated string of "audioCodecs"
    if (aCodecs.size()){
      for (std::set<std::string>::iterator it = aCodecs.begin(); it != aCodecs.end(); ++it){
        audCodecsStr += ",";
        audCodecsStr += *it;
      }
    }

    std::string assocGroupTag = "";
    // add associate group tags
    if ((!masterData.isTS && aTracks.size()) || (masterData.isTS && aTracks.size() > 1)){
      assocGroupTag += "AUDIO=\"aud\",";
    }
    if (sTracksSize){assocGroupTag += "SUBTITLES=\"sub\",";}

    for (std::set<size_t>::iterator itr = vTracks.begin(); itr != vTracks.end(); itr++){
      std::map<std::string, size_t>::const_iterator it = vidGroups.begin();
      while (it != vidGroups.end()){
        if (*itr == it->second){break;}
        it++;
      }
      if (it == vidGroups.end()){continue;}

      bool keyFramesAligned = checkFramesAlignment(result, M, masterData, it->second);
      if (keyFramesAligned){
        uint64_t bWidth = M.getBps(it->second);
        bWidth = ((bWidth < 5 ? 5 : bWidth) + asBWidth) * 8;

        addInfMainTag(result);
        result << assocGroupTag;
        addInfCodecsTag(result, M, it->second, audCodecsStr);
        addInfResolFrameRate(result, M, it->first.substr(groupIdPrefix.size()), it->second);
        addInfBWidthTag(result, bWidth);
        addInfTrackTag(result, masterData, aTracks, it->second, iFrag, keyFramesAligned, true);
      }
    }
  }

  /// Adds EXT-X-MEDIA:TYPE=SUBTITLES tags to the manifest
  uint64_t addSubTags(std::stringstream &result, const DTSC::Meta &M, const MasterData &masterData,
                      const std::set<size_t> &sTracks, const uint64_t iFrag){
    uint64_t subBWidth = 0;
    for (std::set<size_t>::iterator its = sTracks.begin(); its != sTracks.end(); its++){
      addExtXMediaTags(result, M, masterData, *its, "SUBTITLES", "sub", iFrag);
      subBWidth = std::max(subBWidth, M.getBps(*its));
    }
    return subBWidth;
  }

  /// Adds EXT-X-MEDIA:TYPE=AUDIO tags to the manifest
  uint64_t addAudTags(std::stringstream &result, std::set<std::string> &aCodecs,
                      const DTSC::Meta &M, const MasterData &masterData,
                      const std::set<size_t> &aTracks, const uint64_t iFrag,
                      const uint32_t vTracksLength){
    // if video tracks available, audio tracks as EXT-X-MEDIA, else as EXT-X-STREAM-INF
    uint64_t audBWidth = 0;
    if (vTracksLength){
      for (std::set<size_t>::iterator ita = aTracks.begin(); ita != aTracks.end(); ita++){
        if (!masterData.isTS || (masterData.isTS && aTracks.size() > 1)){
          addExtXMediaTags(result, M, masterData, *ita, "AUDIO", "aud", iFrag);
        }
        aCodecs.insert(Util::codecString(M.getCodec(*ita), M.getInit(*ita)));
        audBWidth = std::max(audBWidth, M.getBps(*ita));
      }
    }
    return audBWidth;
  }

  /// Adds EXT-X-MEDIA:TYPE=VIDEO tags to the manifest
  void addVidTags(std::stringstream &result, std::stringstream &grpid, const DTSC::Meta &M,
                  const MasterData &masterData, const std::set<size_t, std::less<size_t> > &vTracks,
                  const std::multimap<std::string, size_t> &vidGroups, const uint64_t iFrag,
                  const uint32_t aTracksSize){
    // if audio tracks available, video tracks are EXT-X-STREAM-INF
    std::set<size_t>::iterator itv = aTracksSize ? vTracks.end() : vTracks.begin();
    for (; itv != vTracks.end(); itv++){
      getGroupId(grpid, M, *itv);
      if (vidGroups.find(grpid.str()) != vidGroups.end() && vidGroups.count(grpid.str()) == 1){
        continue;
      }

      if (checkFramesAlignment(result, M, masterData, *itv)){
        addExtXMediaTags(result, M, masterData, *itv, "VIDEO", grpid.str(), iFrag);
      }
    }
  }

  /// Sorts all tracks into video, audio & subtitle sets & generate rendition groups
  void sortTracks(const DTSC::Meta &M, const std::map<size_t, Comms::Users> &userSelect,
                  std::stringstream &grpid, std::set<size_t, std::less<size_t> > &vTracks,
                  std::set<size_t> &aTracks, std::set<size_t> &sTracks,
                  std::multimap<std::string, size_t> &vidGroups){
    std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin();
    for (; it != userSelect.end(); it++){
      if (M.getType(it->first) == "video"){
        vTracks.insert(it->first);
        getGroupId(grpid, M, it->first);
        vidGroups.insert(std::pair<std::string, size_t>(grpid.str(), it->first));
      }
      if (M.getType(it->first) == "audio"){aTracks.insert(it->first);}
      if (M.getCodec(it->first) == "subtitle"){sTracks.insert(it->first);}
    }
  }

  /// This is a hack to ensure the LLHLS playback starts as close as possible to the live edge
  u_int16_t getLiveLengthLimit(const MasterData &masterData){
    // NOTE:
    // TL;DR: Apple cleints receive the shortest media playlist to ensure a consistent playback at
    // least possible latency.
    // Long story: After experimentation, it was found that Apple clients start streaming
    // consistently at least latency when the first playlist is short, i.e., ~1 full fragment (+
    // partial fragment if any) short. From that point, the playlist can grow with the stream.
    // TODO: remove this when the above issue with apple clients is observed no more.
    return (masterData.userAgent.find(" Mac OS ") != std::string::npos) ? 3 : 6;
  }

  /// Get the first fragment number to be printed in the playlist
  u_int64_t getInitFragment(const DTSC::Meta &M, const MasterData &masterData){
    if (M.getLive()){
      DTSC::Fragments fragments(M.fragments(masterData.mainTrack));
      DTSC::Keys keys(M.keys(masterData.mainTrack));
      u_int64_t iFrag = std::max(fragments.getEndValid() -
                                     (masterData.noLLHLS ? 10 : getLiveLengthLimit(masterData)),
                                 fragments.getFirstValid());
      uint64_t minDur =
          M.getLastms(masterData.mainTrack) - keys.getTime(fragments.getFirstKey(iFrag));
      if (minDur < HLS::partDurationMaxMs * 3){iFrag--;}
      return iFrag;
    }else{
      return 0;
    }
  }

  /// Appends master manifest to result
  void addMasterManifest(std::stringstream &result, const DTSC::Meta &M,
                         const std::map<size_t, Comms::Users> &userSelect,
                         const MasterData &masterData){
    std::set<size_t, std::less<size_t> > vTracks;
    std::set<size_t> aTracks;
    std::set<size_t> sTracks;
    std::stringstream grpid;                      ///< used for vidGroups.
    std::multimap<std::string, size_t> vidGroups; ///< stores 1 video track id from a groupid
    std::set<std::string> aCodecs;                ///< a set to store unique audio codecs

    sortTracks(M, userSelect, grpid, vTracks, aTracks, sTracks, vidGroups);

    const uint64_t iFrag = getInitFragment(M, masterData);

    addMasterBasicTags(result);

    addVidTags(result, grpid, M, masterData, vTracks, vidGroups, iFrag, aTracks.size());

    uint64_t audBWidth = addAudTags(result, aCodecs, M, masterData, aTracks, iFrag, vTracks.size());

    uint64_t subBWidth = addSubTags(result, M, masterData, sTracks, iFrag);

    if (vidGroups.size()){
      addVidInfStreamTags(result, M, masterData, aCodecs, vTracks, aTracks, vidGroups,
                          audBWidth + subBWidth, iFrag, sTracks.size());
    }else{
      addAudInfStreamTags(result, M, masterData, aTracks, iFrag);
    }
  }

  /// returns the end time for a given partial fragment
  /// returns 0 for a hinted part which never got created
  uint64_t getPartTargetTime(const DTSC::Meta &M, const uint32_t idx, const uint32_t mTrack,
                             const uint64_t startTime, const uint64_t msn, const uint32_t part){
    DTSC::Fragments fragments(M.fragments(mTrack));

    // Estimate the target end time for a given part
    // 50 ms is margin of safety to accommodate inconsistencies
    const uint64_t calcTargetTime = startTime + (part + 1) * partDurationMaxMs + 50;

    uint64_t lastms = std::min(M.getLastms(mTrack), M.getLastms(idx));
    uint16_t count = 0;

    // wait until estimated target end time is <= lastms for the track
    while (calcTargetTime > lastms && count++ < 50){
      Util::wait(calcTargetTime - lastms);
      lastms = std::min(M.getLastms(mTrack), M.getLastms(idx));
    }

    // Duration maybe invalid, indicating msn is not complete
    // But the part is ready. So return the end time
    uint64_t duration = fragments.getDuration(msn);
    if (!duration){return startTime + ((part + 1) * partDurationMaxMs);}

    // If duration valid, MSN is fully finished
    // Possible that the last partial fragment duration < partDurationMaxMs
    // Find the exact duration of the last partial fragment
    uint64_t partTargetTime =
        std::min(startTime + duration, startTime + ((part + 1) * partDurationMaxMs));

    if (duration && (partTargetTime - startTime) > duration){return 0;}
    return partTargetTime;
  }

}// namespace HLS
