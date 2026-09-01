#include "hls_support.h"

#include "encode.h"
#include "http_parser.h"
#include "stream.h"
#include "timing.h"

#include <cstdlib>
#include <iomanip>
#include <sstream>

/// Returns the time in milliseconds between last fragment start and current live point
static inline uint64_t liveFragmentDuration(const DTSC::Meta & M, size_t requestTrack, size_t timingTrack, uint64_t requestedMsn,
                                            const DTSC::Fragments & fragments, const DTSC::Keys & keys) {
  const uint64_t liveEdge = std::min(M.getLastms(requestTrack), M.getLastms(timingTrack));
  const uint64_t fragmentStart = keys.getTime(fragments.getFirstKey(requestedMsn));
  return liveEdge > fragmentStart ? liveEdge - fragmentStart : 0;
}

/// Returns the duration of the media between startTime and endTime, which may be 0
static inline uint64_t mediaDuration(const DTSC::Meta & M, size_t track, uint64_t startTime, uint64_t endTime) {
  if (endTime <= startTime || !M.getValidTracks().count(track)) { return 0; }

  // Meta may already be limited to an explicitly requested playback range. Clamp to that
  // visible range, then apply the segment-local limiter once to the underlying key data.
  startTime = std::max(startTime, M.getFirstms(track));
  endTime = std::min(endTime, M.getLastms(track));
  if (endTime <= startTime) { return 0; }

  DTSC::Parts parts(M.parts(track));
  DTSC::Keys keys(M.getKeys(track, false));
  if (!parts.getValidCount() || !keys.getValidCount()) { return 0; }

  keys.applyLimiter(startTime, endTime, parts);
  if (!keys.getValidCount()) { return 0; }

  const size_t firstKey = keys.getFirstValid();
  const size_t lastKey = keys.getEndValid() - 1;
  const size_t firstPart = keys.getFirstPart(firstKey);
  const size_t endPart = keys.getFirstPart(lastKey) + keys.getParts(lastKey);
  if (firstPart < parts.getFirstValid() || firstPart >= endPart || endPart > parts.getEndValid()) { return 0; }

  for (size_t part = firstPart; part < endPart; ++part) {
    if (parts.getSize(part)) { return keys.getTime(lastKey) + keys.getDuration(lastKey) - keys.getTime(firstKey); }
  }
  return 0;
}

/// Checks if a fragment has content by checking if mediaDuration(...) is greater than 0
static inline bool hasFragmentPayload(const DTSC::Meta & M, bool isLive, size_t requestTrack, size_t timingTrack,
                                      const DTSC::Fragments & fragments, const DTSC::Keys & keys, uint64_t fragment) {
  if (fragment < fragments.getFirstValid() || fragment >= fragments.getEndValid()) { return false; }
  const uint64_t duration = fragments.getDuration(fragment);
  if (!duration) { return false; }
  uint64_t startTime = keys.getTime(fragments.getFirstKey(fragment));
  if (!isLive) {
    startTime -= M.getFirstms(timingTrack);
    startTime += M.getFirstms(requestTrack);
  }
  return mediaDuration(M, requestTrack, startTime, startTime + duration);
}

/// Returns a string describing the video group ("vid-WIDTHxHEIGHT")
static inline std::string groupId(const DTSC::Meta & M, size_t track) {
  std::stringstream result;
  result << "vid-" << M.getWidth(track) << "x" << M.getHeight(track);
  return result.str();
}

/// Appends start/stop parameters if they are non-empty
static inline void writePlaybackRange(std::stringstream & result, const std::string & start, const std::string & stop) {
  if (start.size()) { result << "&start=" << start; }
  if (stop.size()) { result << "&stop=" << stop; }
}

/// Writes out the path for a sub playlist
static inline void writeMediaRendition(std::stringstream & result, const DTSC::Meta & M, size_t mainTrack, bool lowLatencyDisabled,
                                       bool typedPaths, const std::string & sessionId, const std::string & start,
                                       const std::string & stop, size_t track, const std::string & type,
                                       const std::string & group, bool defaultRendition = false, bool autoSelect = false) {
  const std::string lang = M.getLang(track).empty() ? "und" : M.getLang(track);
  result << "#EXT-X-MEDIA:TYPE=" << type << ",GROUP-ID=\"" << group << "\",LANGUAGE=\"" << lang;
  if (lang == "und") { result << "-" << track; }
  result << "\",NAME=\"" << M.getCodec(track) << "-" << (lang == "und" ? std::to_string(track) : lang) << "\"";
  if (defaultRendition || autoSelect) {
    result << ",DEFAULT=" << (defaultRendition ? "YES" : "NO") << ",AUTOSELECT=" << (autoSelect ? "YES" : "NO");
  }
  if (type[0] == 'A' && M.getChannels(track)) { result << ",CHANNELS=\"" << M.getChannels(track) << "\""; }
  result << ",URI=\"" << (char)(type[0] + 32) << std::to_string(track) << "/index.m3u8?mTrack=" << mainTrack;
  if (sessionId.size()) { result << "&tkn=" << sessionId; }
  if (lowLatencyDisabled) { result << "&llhls=0"; }
  writePlaybackRange(result, start, stop);
  result << "\"\r\n";
}

namespace HLS {

  Generator::Generator() {
    ext = "ts";
  }

  void Generator::setParam(const std::string & name, const std::string & value) {
    params[name] = value;
  }

  void Generator::setExt(const std::string & value) {
    ext = value;
  }

  void Generator::setListLimit(uint64_t value) {
    listLimit = value;
    if (listLimit && listLimit < 6) { listLimit = 6; }
  }

  void Generator::setPartTarget(uint32_t value) {
    if (value) { partTargetMs = value; }
  }

  void Generator::setUrlPrefix(const std::string & value) {
    urlPrefix = value;
  }

  void Generator::setMuxed(bool value) {
    muxed = value;
  }

  std::string Generator::masterPlaylist(const DTSC::Meta & M, const std::map<size_t, Comms::Users> & userSelect, size_t mainTrack) {
    if (muxed) {
      std::stringstream result;
      result << "#EXTM3U\r\n#EXT-X-VERSION:7\r\n#EXT-X-INDEPENDENT-SEGMENTS\r\n";

      {
        std::set<std::string> tags;
        for (const auto & track : userSelect) {
          const JSON::Value encryption = JSON::fromString(M.getEncryption(track.first));
          jsonForEachConst (encryption, entry) {
            if (entry->isMember("hls-master")) {
              tags.insert(Encodings::Base64::decode((*entry)["hls-master"].asStringRef()));
            }
          }
        }
        for (const auto & tag : tags) { result << tag << "\r\n"; }
      }

      result << "#EXT-X-STREAM-INF:";
      uint64_t bandwidth = 0;
      std::string codecs;
      size_t video = INVALID_TRACK_ID;
      std::string mediaPath;
      for (const auto & track : userSelect) {
        bandwidth += M.getBps(track.first) * 8;
        if (codecs.size()) { codecs += ","; }
        codecs += Util::codecString(M.getCodec(track.first), M.getInit(track.first));
        if (M.getType(track.first) == "video") { video = track.first; }
        mediaPath += M.getType(track.first)[0] + std::to_string(track.first) + "/";
      }
      result << "BANDWIDTH=" << (uint64_t)(bandwidth * 1.3) << ",AVERAGE-BANDWIDTH=" << (uint64_t)(bandwidth * 1.1)
             << ",CODECS=\"" << codecs << "\"";
      if (video != INVALID_TRACK_ID) {
        result << ",RESOLUTION=" << M.getWidth(video) << "x" << M.getHeight(video);
        if (M.getFpks(video)) { result << ",FRAME-RATE=" << M.getFpks(video) / 1000.0; }
      }

      result << "\r\n" << mediaPath << "index.m3u8" << HTTP::argStr(params) << "\r\n";
      return result.str();
    }

    const bool lowLatencyDisabled = params["llhls"] == "0";
    const bool isTs = ext == "ts";
    const std::string sessionId = params["tkn"];
    const std::string start = params["start"];
    const std::string stop = params["stop"];
    std::set<size_t> videoTracks;
    std::set<size_t> audioTracks;
    std::set<size_t> subtitleTracks;
    std::multimap<std::string, size_t> videoGroups;
    for (std::map<size_t, Comms::Users>::const_iterator track = userSelect.begin(); track != userSelect.end(); ++track) {
      if (M.getType(track->first) == "video") {
        videoTracks.insert(track->first);
        videoGroups.insert(std::make_pair(groupId(M, track->first), track->first));
      }
      if (M.getType(track->first) == "audio") { audioTracks.insert(track->first); }
      if (M.getCodec(track->first) == "subtitle") { subtitleTracks.insert(track->first); }
    }

    std::stringstream result;
    result << "#EXTM3U\r\n#EXT-X-VERSION:7\r\n#EXT-X-INDEPENDENT-SEGMENTS\r\n";
    {
      std::set<std::string> tags;
      for (const auto & track : userSelect) {
        const JSON::Value encryption = JSON::fromString(M.getEncryption(track.first));
        jsonForEachConst (encryption, entry) {
          if (entry->isMember("hls-master")) {
            tags.insert(Encodings::Base64::decode((*entry)["hls-master"].asStringRef()));
          }
        }
      }
      for (const auto & tag : tags) { result << tag << "\r\n"; }
    }

    if (!audioTracks.size()) {
      for (const auto & track : videoTracks) {
        const std::string group = groupId(M, track);
        if (videoGroups.count(group) == 1) { continue; }

        if (track == mainTrack || M.keyTimingsMatch(mainTrack, track)) {
          writeMediaRendition(result, M, mainTrack, lowLatencyDisabled, !isTs, sessionId, start, stop, track, "VIDEO", group);
        } else {
          result << "## NOTE: Track " << track << " is available, but ignored because it is not aligned with track "
                 << mainTrack << ".\r\n";
        }
      }
    }

    std::set<std::string> audioCodecs;
    uint64_t audioBandwidth = 0;
    if (videoTracks.size()) {
      for (std::set<size_t>::const_iterator track = audioTracks.begin(); track != audioTracks.end(); ++track) {
        if (!isTs || audioTracks.size() > 1) {
          writeMediaRendition(result, M, mainTrack, lowLatencyDisabled, !isTs, sessionId, start, stop, *track, "AUDIO",
                              "aud", track == audioTracks.begin(), true);
        }
        audioCodecs.insert(Util::codecString(M.getCodec(*track), M.getInit(*track)));
        audioBandwidth = std::max(audioBandwidth, M.getBps(*track));
      }
    }

    uint64_t subtitleBandwidth = 0;
    for (std::set<size_t>::const_iterator track = subtitleTracks.begin(); track != subtitleTracks.end(); ++track) {
      writeMediaRendition(result, M, mainTrack, lowLatencyDisabled, !isTs, sessionId, start, stop, *track, "SUBTITLES", "sub");
      subtitleBandwidth = std::max(subtitleBandwidth, M.getBps(*track));
    }

    if (!videoTracks.size()) {
      for (const auto & track : audioTracks) {
        const uint64_t bandwidth = std::max<uint64_t>(M.getBps(track), 5) * 8;
        result << "#EXT-X-STREAM-INF:CODECS=\"" << Util::codecString(M.getCodec(track), M.getInit(track)) << "\""
               << std::fixed << std::setprecision(0) << ",BANDWIDTH=" << bandwidth * 1.3
               << ",AVERAGE-BANDWIDTH=" << bandwidth * 1.1 << "\r\n";
        result << M.getType(track)[0] << track;
        result << "/index.m3u8?mTrack=" << mainTrack;
        if (sessionId.size()) { result << "&tkn=" << sessionId; }
        if (lowLatencyDisabled) { result << "&llhls=0"; }
        writePlaybackRange(result, start, stop);
        result << "\r\n";
      }
      return result.str();
    }

    std::string audioCodecList;
    for (std::set<std::string>::const_iterator codec = audioCodecs.begin(); codec != audioCodecs.end(); ++codec) {
      audioCodecList += "," + *codec;
    }
    std::string associatedGroups;
    if ((!isTs && audioTracks.size()) || (isTs && audioTracks.size() > 1)) { associatedGroups += "AUDIO=\"aud\","; }
    if (subtitleTracks.size()) { associatedGroups += "SUBTITLES=\"sub\","; }

    for (const auto & track : videoTracks) {
      if (!(track == mainTrack || M.keyTimingsMatch(mainTrack, track))) {
        result << "## NOTE: Track " << track << " is available, but ignored because it is not aligned with track "
               << mainTrack << ".\r\n";
        continue;
      }
      const uint64_t bandwidth = (std::max<uint64_t>(M.getBps(track), 5) + audioBandwidth + subtitleBandwidth) * 8;
      result << "#EXT-X-STREAM-INF:" << associatedGroups << "CODECS=\""
             << Util::codecString(M.getCodec(track), M.getInit(track)) << audioCodecList
             << "\",RESOLUTION=" << M.getWidth(track) << "x" << M.getHeight(track);
      if (M.getFpks(track)) { result << ",FRAME-RATE=" << (float)M.getFpks(track) / 1000; }
      result << std::fixed << std::setprecision(0) << ",BANDWIDTH=" << bandwidth * 1.3
             << ",AVERAGE-BANDWIDTH=" << bandwidth * 1.1 << "\r\n";
      result << M.getType(track)[0] << track;
      if (isTs && audioTracks.size() == 1) { result << "_" << *audioTracks.begin(); }
      result << "/index.m3u8?mTrack=" << mainTrack;
      if (sessionId.size()) { result << "&tkn=" << sessionId; }
      if (lowLatencyDisabled) { result << "&llhls=0"; }
      writePlaybackRange(result, start, stop);
      result << "\r\n";
    }
    return result.str();
  }

  std::string Generator::subPlaylist(const DTSC::Meta & M, const std::map<size_t, Comms::Users> & userSelect,
                                     size_t mainTrack, size_t requestedTrack) {
    if (requestedTrack == INVALID_TRACK_ID) { requestedTrack = mainTrack; }
    if (!M.getValidTracks().count(requestedTrack)) { return "404"; }

    const JSON::Value & mTrack = params["mTrack"];
    const size_t timingTrack =
      (mTrack.size() && (M.getValidTracks().count(atoll(mTrack.c_str())))) ? atoll(mTrack.c_str()) : mainTrack;
    const bool isTs = ext == "ts";
    const bool isLive = M.getLive();
    const std::string sessionId = params["tkn"];
    const std::string hlsSkip = params["_HLS_skip"];
    const std::string hlsMsn = params["_HLS_msn"];
    const std::string hlsPart = params["_HLS_part"];
    bool lowLatencyDisabled = params["llhls"] == "0";
    uint32_t targetDuration = (M.biggestFragment(timingTrack) + 500) / 1000;
    if (!targetDuration) { targetDuration = 1; }

    uint64_t maxSampleDuration = 0;
    for (const auto & selected : userSelect) {
      if (!M.getValidTracks().count(selected.first)) { continue; }
      DTSC::Parts parts(M.parts(selected.first));
      for (size_t part = parts.getFirstValid(); part < parts.getEndValid(); ++part) {
        maxSampleDuration = std::max<uint64_t>(maxSampleDuration, parts.getDuration(part));
      }
    }
    const uint32_t advertisedPartTarget = partTargetMs + maxSampleDuration;
    if (maxSampleDuration && 3 * partTargetMs < 37 * maxSampleDuration) {
      lowLatencyDisabled = true;
      MEDIUM_MSG("Disabling LL-HLS for track %zu: %ums part grid is too short for %" PRIu64 "ms media samples",
                 requestedTrack, partTargetMs, maxSampleDuration);
    }

    DTSC::Fragments fragments(M.fragments(timingTrack));
    DTSC::Keys keys(M.getKeys(timingTrack));

    // Check if we need to send a special response code related to blocking reload
    if (!lowLatencyDisabled) {

      // Check BPR request validity
      if (hlsMsn.empty() && hlsPart.size()) { return "400"; }
      if (fragments.getEndValid() && atol(hlsMsn.c_str()) > fragments.getEndValid() + 1) { return "400"; }

      // BPR logic only if live & _HLS_msn requested
      if (M.getLive() && hlsMsn.size()) {
        MEDIUM_MSG("Requesting media playlist: Track %zu, MSN %s, part: %s", timingTrack, hlsMsn.c_str(), hlsPart.c_str());

        uint64_t requestedMsn = atol(hlsMsn.c_str());
        uint64_t requestedPart = atol(hlsPart.c_str()) + 1; // base 1
        int64_t bprTimeLimit =
          (4ll * targetDuration * 1000) + std::max(M.getMinKeepAway(timingTrack), M.getMinKeepAway(requestedTrack));

        // if hlsPart empty (HLS spec) OR if fragment hlsMsn is complete
        // THEN request part 1 of MSN++
        if (hlsPart.empty()) { requestedPart = 1; }
        if (requestedMsn < fragments.getFirstValid()) { return 0; }
        while (requestedMsn >= fragments.getEndValid()) {
          if (bprTimeLimit < 1) { return "503"; }
          Util::wait(partTargetMs + 25);
          bprTimeLimit -= (partTargetMs + 25);
        }
        if (fragments.getDuration(requestedMsn)) {
          requestedMsn++;
          requestedPart = 1;
        }
        while (requestedMsn >= fragments.getEndValid()) {
          if (bprTimeLimit < 1) { return "503"; }
          Util::wait(partTargetMs + 25);
          bprTimeLimit -= (partTargetMs + 25);
        }

        uint64_t lastFragmentDur = liveFragmentDuration(M, requestedTrack, timingTrack, requestedMsn, fragments, keys);
        std::ldiv_t res = std::ldiv(lastFragmentDur, partTargetMs);
        size_t finalMsn = fragments.getEndValid() > 1 ? fragments.getEndValid() - 2 : 0;
        MEDIUM_MSG("req MSN %" PRIu64 " fin MSN %zu, req Part %" PRIu64 " fin Part %ld", requestedMsn, finalMsn,
                   requestedPart, res.quot);

        // RFC 8216bis 6.2.5.2: if the requested part is beyond the last available part by more than
        // the Advance Part Limit, reject immediately (400) instead of blocking until timeout (503).
        // This is three divided by PART-TARGET when PART-TARGET is below one second, or three otherwise.
        const uint32_t advertisedTarget = advertisedPartTarget ? advertisedPartTarget : partTargetMs;
        uint32_t advancePartLimit = (advertisedTarget && advertisedTarget < 1000) ? 3000 / advertisedTarget : 3;
        if (requestedPart > (uint64_t)res.quot + advancePartLimit) { return "400"; }

        while (requestedPart > res.quot) {
          if (bprTimeLimit < 1) { return "503"; }
          MEDIUM_MSG("Part Block: req %" PRIu64 " fin %ld", requestedPart, res.quot);
          Util::wait(partTargetMs - res.rem + 25);
          bprTimeLimit -= (partTargetMs - res.rem + 25);
          lastFragmentDur = liveFragmentDuration(M, requestedTrack, timingTrack, requestedMsn, fragments, keys);
          res = std::ldiv(lastFragmentDur, partTargetMs);
        }
      }
    }

    const uint64_t availableFirstFragment = fragments.getFirstValid();
    const uint64_t liveEdge = std::min(M.getLastms(requestedTrack), M.getLastms(timingTrack));
    uint64_t endFragment = fragments.getEndValid();
    if (isLive) {
      const uint64_t edgeFragment = M.getFragmentIndexForTime(timingTrack, liveEdge);
      endFragment = std::min<uint64_t>(endFragment, edgeFragment + 1);
      if (endFragment < availableFirstFragment) { endFragment = availableFirstFragment; }
    }

    uint64_t firstFragment = availableFirstFragment;
    if (isLive) {
      // endFragment is one past the currently forming fragment.
      // Only fragments before that fragment have full entries
      if (endFragment <= availableFirstFragment + 1) { firstFragment = availableFirstFragment; }
      const uint64_t completeEnd = endFragment - 1;
      firstFragment = availableFirstFragment;

      // Preserve the historical two-fragment safety trim only when doing so leaves the Apple
      // authoring profile's minimum six complete segments available.
      if (completeEnd - availableFirstFragment >= 8) { firstFragment += 2; }

      if (listLimit) {
        if (completeEnd - firstFragment > listLimit) { firstFragment = completeEnd - listLimit; }
      }

      // Retain six complete segments when that many still have payload data.
      if (!isTs && endFragment > availableFirstFragment + 1) {
        uint64_t candidate = endFragment - 1;
        uint32_t servable = 0;
        while (candidate > availableFirstFragment && servable < 6) {
          --candidate;
          if (hasFragmentPayload(M, true, requestedTrack, timingTrack, fragments, keys, candidate)) { ++servable; }
        }
        const uint64_t floorStart = servable >= 6 ? candidate : availableFirstFragment;
        if (firstFragment > floorStart) { firstFragment = floorStart; }
      }
    }
    while (!isTs && firstFragment + 1 < endFragment &&
           !hasFragmentPayload(M, isLive, requestedTrack, timingTrack, fragments, keys, firstFragment)) {
      ++firstFragment;
    }

    const uint64_t mediaSequence = firstFragment;
    uint64_t skippedFragments = 0;

#ifndef NOLLHLS
    if (hlsSkip == "YES") {
      const uint32_t retained = targetDuration * 6 / targetDuration + 2;
      const uint64_t available = endFragment - firstFragment;
      if (available > retained) {
        skippedFragments = available - retained;
        firstFragment += skippedFragments;
      }
    }
#endif

    std::stringstream result;
    result << "#EXTM3U\r\n";

#ifndef NOLLHLS
    result << "#EXT-X-VERSION:" << ((isLive && !lowLatencyDisabled) ? 10 : ((hlsSkip == "YES") ? 9 : 6)) << "\r\n";
    if (isLive && !lowLatencyDisabled) {
      const uint32_t advertisedTarget = advertisedPartTarget ? advertisedPartTarget : partTargetMs;
      const float partTargetSeconds = advertisedTarget / 1000.0;
      result << "#EXT-X-SERVER-CONTROL:CAN-BLOCK-RELOAD=YES,CAN-SKIP-UNTIL=" << targetDuration * 6
             << ",HOLD-BACK=" << targetDuration * 3 << ",PART-HOLD-BACK=" << partTargetSeconds * 4
             << "\r\n#EXT-X-PART-INF:PART-TARGET=" << partTargetSeconds << "\r\n";
    }
#else
    result << "#EXT-X-VERSION:6\r\n";
#endif

    if (!isTs) {
      result << "#EXT-X-MAP:URI=\"" << urlPrefix << "init." << ext;
      if (sessionId.size()) { result << "?tkn=" << sessionId; }
      result << "\"\r\n";
    }
    const std::string encryptionMethod = M.getEncryption(requestedTrack);
    if (encryptionMethod.size()) {
      const JSON::Value encryption = JSON::fromString(encryptionMethod);
      if (encryption.isArray() && encryption.size()) {
        std::set<std::string> tags;
        jsonForEachConst (encryption, entry) {
          if ((*entry)["hls-media"].asStringRef().size()) {
            tags.insert(Encodings::Base64::decode((*entry)["hls-media"].asStringRef()));
          }
        }
        for (std::set<std::string>::const_iterator tag = tags.begin(); tag != tags.end(); ++tag) {
          result << *tag << "\r\n";
        }
      } else {
        result << "#EXT-X-KEY:METHOD=" << encryptionMethod << ",URI=\"asd\"\r\n";
      }
    }
    result << "#EXT-X-TARGETDURATION:" << targetDuration << "\r\n"
           << "#EXT-X-MEDIA-SEQUENCE:" << mediaSequence << "\r\n";
    if (skippedFragments) { result << "#EXT-X-SKIP:SKIPPED-SEGMENTS=" << skippedFragments << "\r\n"; }

    uint64_t lastFragmentStart = liveEdge;
    uint32_t lastPartCount = 0;
    for (uint64_t fragment = firstFragment; fragment < endFragment; ++fragment) {
      uint64_t startTime = keys.getTime(fragments.getFirstKey(fragment));
      if (!isLive) { startTime -= M.getFirstms(timingTrack); }

      uint64_t duration = fragments.getDuration(fragment);
      if (!duration) { duration = liveEdge > startTime ? liveEdge - startTime : 0; }
      lastFragmentStart = startTime;
      const uint64_t liveEdgeDistance = liveEdge > startTime ? liveEdge - startTime : 0;
      uint64_t availableDuration = duration;
      if (fragment == endFragment - 1) { availableDuration = std::min(availableDuration, liveEdgeDistance); }
      std::string dateTimeLine;
      if (isLive) {
        const uint64_t unixMs = M.packetTimeToUnixMs(startTime);
        if (unixMs) { dateTimeLine = "#EXT-X-PROGRAM-DATE-TIME:" + Util::getUTCStringMillis(unixMs) + "\r\n"; }
      }
      bool wrotePart = false;
#ifndef NOLLHLS
      if (!lowLatencyDisabled && isLive && ((endFragment - fragment < 5) || (liveEdgeDistance <= 3ull * targetDuration * 1000))) {
        const size_t fullPartCount = availableDuration / partTargetMs;
        const uint64_t remainderMs = availableDuration % partTargetMs;
        const size_t totalParts = fullPartCount + (((remainderMs > 0) && (endFragment - fragment > 1)) ? 1 : 0);

        for (lastPartCount = 0; lastPartCount < totalParts; ++lastPartCount) {
          const uint64_t rangeStart = startTime + uint64_t(lastPartCount) * partTargetMs;
          const uint64_t rangeEnd = rangeStart + ((lastPartCount < fullPartCount) ? partTargetMs : remainderMs);

          const uint64_t sampleDuration = mediaDuration(M, requestedTrack, rangeStart, rangeEnd);
          if (!sampleDuration) { break; }

          if (!wrotePart) {
            result << dateTimeLine;
            wrotePart = true;
          }

          // A muxed part is defined by the shared grid interval: another selected track can have a
          // sample before the primary track's first sample. Per-track playlists retain their historical
          // snap-to-first-sample duration.
          const uint64_t duration = muxed ? rangeEnd - rangeStart : sampleDuration;
          const uint64_t requestDuration = rangeEnd - rangeStart;
          result << "#EXT-X-PART:DURATION=" << duration / 1000.0;
          result << ",URI=\"" << urlPrefix;
          result << "chunk_" << startTime << "." << lastPartCount << "." << ext;
          result << "?msn=" << fragment;
          result << "&mTrack=" << timingTrack;
          // Keep the resource identity tied to the production grid. This guarantees
          // that an EXT-X-PRELOAD-HINT URI remains identical when the part is later
          // advertised with its exact sample-aligned duration.
          result << "&dur=" << requestDuration;
          if (sessionId.size()) { result << "&tkn=" << sessionId; }
          result << "\"";

          // NOTE: INDEPENDENT tags, specified ONLY for VIDEO tracks, indicate the first partial fragment
          // closest to the before (live edge - PART-HOLD-BACK) time that a client starts playback from.
          if (M.getType(requestedTrack) == "video") {
            const uint64_t partStartTime = startTime + uint64_t(lastPartCount) * partTargetMs;
            const uint32_t partKeyIdx = M.getKeyIndexForTime(timingTrack, partStartTime);
            const uint64_t partKeyIdxTime = M.getTimeForKeyIndex(timingTrack, partKeyIdx);
            if (partKeyIdxTime == partStartTime) { result << ",INDEPENDENT=YES"; }
          }
          result << "\r\n";
        }
      }
#endif

      // The final live fragment is still forming and has no EXTINF entry yet.
      if (isLive && fragment == endFragment - 1) { continue; }

      uint64_t payloadStart = startTime;
      if (!isLive) { payloadStart += M.getFirstms(requestedTrack); }
      if (!isTs && !mediaDuration(M, requestedTrack, payloadStart, payloadStart + duration)) { continue; }
      if (!wrotePart) { result << dateTimeLine; }
      result << "#EXTINF:" << std::fixed << std::setprecision(3) << duration / 1000.0 << ",\r\n";
      result << urlPrefix << "chunk_" << startTime << "." << ext << "?msn=" << fragment << "&mTrack=" << timingTrack
             << "&dur=" << duration;
      if (sessionId.size()) { result << "&tkn=" << sessionId; }
      result << "\r\n";
    }

    if (!isLive) {
      result << "#EXT-X-ENDLIST\r\n";
      return result.str();
    }

#ifdef NOLLHLS
    return result.str();
#endif
    if (lowLatencyDisabled) { return result.str(); }

    if (endFragment > availableFirstFragment && endFragment) {
      result << "#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"" << urlPrefix << "chunk_" << lastFragmentStart << "." << lastPartCount
             << "." << ext << "?msn=" << endFragment - 1 << "&mTrack=" << timingTrack << "&dur=" << partTargetMs;
      if (sessionId.size()) { result << "&tkn=" << sessionId; }
      result << "\"\r\n";
    }

    if (muxed) { return result.str(); }
    if (endFragment < fragments.getFirstValid() + 2) { return result.str(); }
    const std::ldiv_t previousPart = std::ldiv(fragments.getDuration(endFragment - 2), partTargetMs);
    for (std::map<size_t, Comms::Users>::const_iterator rendition = userSelect.begin(); rendition != userSelect.end(); ++rendition) {
      if (rendition->first == requestedTrack) { continue; }
      result << "#EXT-X-RENDITION-REPORT:URI=\"../" << M.getType(rendition->first)[0] << rendition->first
             << "/index.m3u8?mTrack=" << timingTrack;
      if (sessionId.size()) { result << "&tkn=" << sessionId; }
      result << "\"";
      if (lastPartCount) {
        result << ",LAST-MSN=" << endFragment - 1 << ",LAST-PART=" << lastPartCount - 1 << "\r\n";
      } else {
        result << ",LAST-MSN=" << endFragment - 2 << ",LAST-PART=" << previousPart.quot - 1 + (previousPart.rem ? 1 : 0) << "\r\n";
      }
    }
    return result.str();
  }

} // namespace HLS
