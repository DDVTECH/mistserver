#include "hls_support.h"

#include "http_parser.h"
#include "langcodes.h" /*LTS*/
#include "stream.h"
#include "timing.h"

#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace HLS{

  Generator::Generator() {
    ext = "ts";
    listLimit = 0;
  }

  void Generator::setParam(const std::string & name, const std::string & value) {
    params[name] = value;
  }

  void Generator::setExt(const std::string & value) {
    ext = value;
  }

  void Generator::setListLimit(uint64_t value) {
    listLimit = value;
  }

  std::string Generator::masterPlaylist(const DTSC::Meta & M, const std::map<size_t, Comms::Users> & userSelect, size_t mainTrack) {

    std::stringstream result;
    result << "#EXTM3U\r\n";
    size_t audioId = INVALID_TRACK_ID;
    size_t vidTracks = 0;
    bool hasSubs = false;
    for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin(); it != userSelect.end(); ++it) {
      if (audioId == INVALID_TRACK_ID && M.getType(it->first) == "audio") { audioId = it->first; }
      if (!hasSubs && M.getCodec(it->first) == "subtitle") { hasSubs = true; }
    }
    std::string args = HTTP::argStr(params);
    for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin(); it != userSelect.end(); ++it) {
      if (M.getType(it->first) == "video") {
        ++vidTracks;
        int bWidth = M.getBps(it->first);
        if (bWidth < 5) { bWidth = 5; }
        if (audioId != INVALID_TRACK_ID) { bWidth += M.getBps(audioId); }
        result << "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" << (bWidth * 8);
        result << ",RESOLUTION=" << M.getWidth(it->first) << "x" << M.getHeight(it->first);
        if (M.getFpks(it->first)) { result << ",FRAME-RATE=" << (float)M.getFpks(it->first) / 1000; }
        if (hasSubs) { result << ",SUBTITLES=\"sub1\""; }
        result << ",CODECS=\"";
        result << Util::codecString(M.getCodec(it->first), M.getInit(it->first));
        if (audioId != INVALID_TRACK_ID) {
          result << "," << Util::codecString(M.getCodec(audioId), M.getInit(audioId));
        }
        result << "\"\r\nv" << it->first;
        if (audioId != INVALID_TRACK_ID) { result << "/a" << audioId; }
        result << "/index.m3u8" << args << "\r\n";
      } else if (M.getCodec(it->first) == "subtitle") {
        result << "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"sub1\",LANGUAGE=\"" << M.getLang(it->first) << "\",NAME=\""
               << Encodings::ISO639::decode(M.getLang(it->first)) << "\",AUTOSELECT=NO,DEFAULT=NO,FORCED=NO,URI=\""
               << it->first << "/index.m3u8" << args << "\""
               << "\r\n";
      }
    }
    if (!vidTracks && audioId != INVALID_TRACK_ID) {
      result << "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" << (M.getBps(audioId) * 8);
      result << ",CODECS=\"" << Util::codecString(M.getCodec(audioId), M.getInit(audioId)) << "\"";
      result << "\r\n";
      result << "a" << audioId << "/index.m3u8" << args << "\r\n";
    }
    return result.str();
  }

  std::string Generator::subPlaylist(const DTSC::Meta & M, const std::map<size_t, Comms::Users> & userSelect, size_t mainTrack) {
    std::stringstream result;
    // parse single track

    std::string args = HTTP::argStr(params);

    // Start preparing "lines".
    // These one segment each (and actually multi-line entries, but we call them lines here for historical reasons).
    uint64_t systemBoot = Util::getGlobalConfig("systemBoot").asInt();
    std::deque<std::string> lines;
    std::deque<uint16_t> durations;
    uint32_t totalDuration = 0;
    DTSC::Keys keys(M.keys(mainTrack));
    DTSC::Parts parts(M.parts(mainTrack));
    DTSC::Fragments fragments(M.fragments(mainTrack));
    uint32_t firstFragment = fragments.getFirstValid();
    uint32_t endFragment = fragments.getEndValid();
    size_t skippedLines = 0;
    for (int i = firstFragment; i < endFragment; i++) {
      uint64_t duration = fragments.getDuration(i);
      size_t keyNumber = fragments.getFirstKey(i);
      uint64_t startTime = keys.getTime(keyNumber);
      if (!duration) { duration = M.getLastms(mainTrack) - startTime; }
      if (startTime + duration <= M.getFirstms(mainTrack)) {
        skippedLines++;
        continue;
      }
      if (startTime >= M.getLastms(mainTrack)) { continue; }
      if (startTime + duration > M.getLastms(mainTrack)) { duration = M.getLastms(mainTrack) - startTime; }

      bool isDiscon = false;
      // For fragments that are not the last fragment, we check if there is a discontinuity
      // We do this by checking the duration of the last part (i.e. frame) of the current fragment.
      // If this duration is more than 5s, we assume there was one, and we strip that last part just to be sure.
      if (i + 1 < endFragment) {
        uint64_t partNum = keys.getFirstPart(fragments.getFirstKey(i + 1));
        if (partNum) {
          partNum--;
          if (parts.getDuration(partNum) > 5000) {
            isDiscon = true;
            // Unfortunately, we cannot trust the last duration to be accurate.
            // Let's calculate it from scratch...
            uint64_t currPart = keys.getFirstPart(fragments.getFirstKey(i));
            duration = 0;
            while (currPart < partNum) {
              duration += parts.getDuration(currPart);
              ++currPart;
            }
          }
        }
      }

      double floatDur = (double)duration / 1000;
      std::string dateTime;
      if (M.getLive() || M.getUTCOffset()) {
        if (M.getUTCOffset()) {
          uint64_t unixMs = M.getUTCOffset() + startTime;
          dateTime = "#EXT-X-PROGRAM-DATE-TIME:" + Util::getUTCStringMillis(unixMs) + "\r\n";
        } else {
          uint64_t unixMs = M.getBootMsOffset() + systemBoot + startTime;
          dateTime = "#EXT-X-PROGRAM-DATE-TIME:" + Util::getUTCStringMillis(unixMs) + "\r\n";
        }
      }
      char lineBuf[600];
      snprintf(lineBuf, 600, "%s#EXTINF:%f,\r\n%" PRIu64 "_%" PRIu64 ".%s%s\r\n%s", dateTime.c_str(), floatDur,
               startTime, startTime + duration, ext.c_str(), args.c_str(), isDiscon ? "#EXT-X-DISCONTINUITY\r\n" : "");
      totalDuration += duration;
      durations.push_back(duration);
      lines.push_back(lineBuf);
    }

    // Calculate target duration from actual segment durations we have/know
    uint32_t targetDuration = 1;
    for (std::deque<uint16_t>::iterator it = durations.begin(); it != durations.end(); ++it) {
      if (*it / 1000 + 1 > targetDuration) { targetDuration = *it / 1000 + 1; }
    }

    if (M.getLive() && lines.size()) {
      // only print the last segment when non-live
      lines.pop_back();
      totalDuration -= durations.back();
      durations.pop_back();
      // skip the first two segments when live, unless that brings us under 4 target durations
      while ((totalDuration - durations.front()) > (targetDuration * 4000) && skippedLines < 2) {
        lines.pop_front();
        totalDuration -= durations.front();
        durations.pop_front();
        ++skippedLines;
      }
      /*LTS-START*/
      // remove lines to reduce size towards listlimit setting - but keep at least 4X target
      // duration available
      if (listLimit) {
        while (lines.size() > listLimit && (totalDuration - durations.front()) > (targetDuration * 4000)) {
          lines.pop_front();
          totalDuration -= durations.front();
          durations.pop_front();
          ++skippedLines;
        }
      }
      /*LTS-END*/
    }

    // Start writing the actual playlist
    result << "#EXTM3U\r\n#EXT-X-VERSION:";
    if (ext == "m4s") {
      result << 6;
    } else {
      result << 3;
    }
    result << "\r\n";
    result << "#EXT-X-TARGETDURATION:" << targetDuration << "\r\n";

    result << "#EXT-X-MEDIA-SEQUENCE:" << firstFragment + skippedLines << "\r\n";
    if (ext == "m4s") { result << "#EXT-X-MAP:URI=\"init.m4s\"\r\n"; }

    // Not write all the "lines" we prepared before
    for (std::deque<std::string>::iterator it = lines.begin(); it != lines.end(); it++) { result << *it; }
    if (!M.getLive() || !totalDuration) { result << "#EXT-X-ENDLIST\r\n"; }
    HIGH_MSG("Sending this index: %s", result.str().c_str());
    return result.str();
  }

}// namespace HLS
