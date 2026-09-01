#include <mist/encode.h>
#include <mist/hls_support.h>

#include <iostream>

namespace {

  class HlsMeta : public DTSC::Meta {
    public:
      void dropFirstKey(size_t track) {
        DTSC::Track & value = tracks.at(track);
        const uint64_t parts = value.keys.getInt(value.keyPartsField, value.keys.getDeleted());
        value.parts.deleteRecords(parts);
        value.keys.deleteRecords(1);
        setFirstms(track, value.keys.getInt(value.keyTimeField, value.keys.getDeleted()));
      }
  };

  size_t occurrences(const std::string & value, const std::string & needle) {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = value.find(needle, pos)) != std::string::npos) {
      ++count;
      pos += needle.size();
    }
    return count;
  }

  bool contains(const std::string & value, const std::string & needle, const char *description) {
    if (value.find(needle) != std::string::npos) { return true; }
    std::cerr << description << ": missing " << needle << " in:\n" << value << std::endl;
    return false;
  }

  size_t addTrack(DTSC::Meta & meta, const std::string & type, const std::string & codec) {
    const size_t track = meta.addTrack(8, 8, 128, 2, true);
    meta.setType(track, type);
    meta.setCodec(track, codec);
    return track;
  }

  void addFixedGop(DTSC::Meta & meta, size_t track, uint64_t first, uint64_t last, uint32_t sampleDuration) {
    for (uint64_t time = first; time <= last; time += sampleDuration) {
      meta.update(time, 0, track, 20, 0, (time - first) % 1000 == 0, sampleDuration);
    }
  }

  std::string subPlaylist(const DTSC::Meta & meta, const std::map<size_t, Comms::Users> & selection, size_t track,
                          uint32_t partTarget = 500, uint64_t listLimit = 0, bool lowLatency = true,
                          const std::string & skip = "") {
    HLS::Generator generator;
    generator.setExt("m4s");
    generator.setPartTarget(partTarget);
    generator.setListLimit(listLimit);
    generator.setParam("mTrack", std::to_string(track));
    if (!lowLatency) { generator.setParam("llhls", "0"); }
    if (skip.size()) { generator.setParam("_HLS_skip", skip); }
    return generator.subPlaylist(meta, selection, track);
  }

  bool testVersionsAndPartTargets() {
    DTSC::Meta video;
    video.reInit("", true);
    video.setLive(true);
    video.setUTCOffset(1700000000000ull);
    video.setMinimumFragmentDuration(1000);
    const size_t track = addTrack(video, "video", "H264");
    addFixedGop(video, track, 1000, 11560, 40);
    std::map<size_t, Comms::Users> selection;
    selection[track];

    const std::string normal = subPlaylist(video, selection, track, 500, 0, false);
    const std::string delta = subPlaylist(video, selection, track, 500, 0, false, "YES");
    const std::string lowLatency = subPlaylist(video, selection, track);
    if (normal.size() < 4 || delta.size() < 4 || lowLatency.size() < 4) {
      std::cerr << "playlist generation returned an unexpected status" << std::endl;
      return false;
    }
    if (!(contains(normal, "#EXT-X-VERSION:6", "standard HLS version") &&
          contains(delta, "#EXT-X-VERSION:9", "delta HLS version") && contains(lowLatency, "#EXT-X-VERSION:10", "LL-HLS version") &&
          contains(lowLatency, "PART-TARGET=0.54", "sample-aware part target") &&
          contains(lowLatency, "PART-HOLD-BACK=2.16", "sample-aware part hold-back"))) {
      return false;
    }
    if (occurrences(normal, "#EXT-X-PROGRAM-DATE-TIME:") != occurrences(normal, "#EXTINF:")) {
      std::cerr << "standard live playlist contains a dangling program date-time:\n" << normal << std::endl;
      return false;
    }
    if (occurrences(lowLatency, "#EXT-X-PROGRAM-DATE-TIME:") != occurrences(lowLatency, "#EXTINF:") + 1) {
      std::cerr << "LL-HLS program date-time count does not cover its forming partial segment:\n"
                << lowLatency << std::endl;
      return false;
    }
    return true;
  }

  bool testLiveWindowFloor() {
    DTSC::Meta meta;
    meta.reInit("", true);
    meta.setLive(true);
    meta.setUTCOffset(1700000000000ull);
    meta.setMinimumFragmentDuration(1000);
    const size_t track = addTrack(meta, "audio", "AAC");
    for (uint64_t time = 1000; time <= 11000; time += 1000) { meta.update(time, 0, track, 20, 0, true, 1000); }
    JSON::Value encryptionEntry;
    encryptionEntry["hls-master"] =
      Encodings::Base64::encode("#EXT-X-SESSION-KEY:METHOD=SAMPLE-AES-CTR,URI=\"master-key\"");
    encryptionEntry["hls-media"] = Encodings::Base64::encode("#EXT-X-KEY:METHOD=SAMPLE-AES-CTR,URI=\"media-key\"");
    JSON::Value encryption;
    encryption.append(encryptionEntry);
    meta.setEncryption(track, encryption.toString());
    std::map<size_t, Comms::Users> selection;
    selection[track];

    HLS::Generator generator;
    generator.setExt("m4s");
    generator.setPartTarget(500);
    generator.setListLimit(1);
    const std::string master = generator.masterPlaylist(meta, selection, track);
    const std::string media = generator.subPlaylist(meta, selection, track);
    if (occurrences(master, "#EXT-X-VERSION:") != 1 || !contains(master, "#EXT-X-VERSION:7", "CMAF-HLS master version") ||
        !contains(master, "#EXT-X-SESSION-KEY:METHOD=SAMPLE-AES-CTR", "master DRM tag") ||
        !contains(media, "#EXT-X-KEY:METHOD=SAMPLE-AES-CTR", "media DRM tag")) {
      return false;
    }
    if (media.size() < 4 || occurrences(media, "#EXTINF:") < 6) {
      std::cerr << "live playlist did not retain six complete segments:\n" << media << std::endl;
      return false;
    }
    return true;
  }

  bool testPartBoundaries() {
    DTSC::Meta meta;
    meta.reInit("", true);
    meta.setLive(true);
    meta.setUTCOffset(1700000000000ull);
    meta.setMinimumFragmentDuration(1000);
    const size_t track = addTrack(meta, "video", "H264");
    for (uint64_t time = 1000; time <= 1560; time += 40) { meta.update(time, 0, track, 20, 0, time == 1000, 40); }
    std::map<size_t, Comms::Users> selection;
    selection[track];
    const std::string playlist = subPlaylist(meta, selection, track);
    return contains(playlist, "#EXT-X-PART:DURATION=0.52", "sample-accurate part duration") &&
      contains(playlist, "&dur=500", "stable part-grid URI");
  }

  bool testExpiredFragmentPayload() {
    HlsMeta meta;
    meta.reInit("", true);
    meta.setLive(true);
    meta.setUTCOffset(1700000000000ull);
    meta.setMinimumFragmentDuration(1000);
    const size_t track = addTrack(meta, "video", "H264");
    addFixedGop(meta, track, 1000, 11000, 40);
    DTSC::Fragments fragments(meta.fragments(track));
    DTSC::Keys keys(meta.getKeys(track));
    const size_t expiredFragment = fragments.getFirstValid();
    const uint64_t expiredStart = keys.getTime(fragments.getFirstKey(expiredFragment));

    // Fragment metadata can briefly outlive its key and part payload in a live ring buffer.
    meta.dropFirstKey(track);
    std::map<size_t, Comms::Users> selection;
    selection[track];
    const std::string playlist = subPlaylist(meta, selection, track);
    const std::string expiredUri = "chunk_" + std::to_string(expiredStart) + ".m4s?msn=" + std::to_string(expiredFragment);
    if (playlist.find(expiredUri) == std::string::npos && occurrences(playlist, "#EXTINF:") >= 6) { return true; }
    std::cerr << "expired payload handling produced an invalid live window:\n" << playlist << std::endl;
    return false;
  }

  bool testLimitedLivePlaylist() {
    DTSC::Meta meta;
    meta.reInit("", true);
    meta.setLive(true);
    meta.setUTCOffset(1700000000000ull);
    meta.setMinimumFragmentDuration(1000);
    const size_t track = addTrack(meta, "video", "H264");
    addFixedGop(meta, track, 1000, 11000, 40);
    meta.applyLimiter(3000, 0xFFFFFFFFFFFFFFFFull);

    std::map<size_t, Comms::Users> selection;
    selection[track];
    const std::string playlist = subPlaylist(meta, selection, track);
    if (occurrences(playlist, "#EXTINF:") >= 6 && contains(playlist, "chunk_3000.m4s", "limited first segment") &&
        contains(playlist, "chunk_9000.m4s", "limited middle segments")) {
      return true;
    }
    std::cerr << "limited live playlist omitted available segments:\n" << playlist << std::endl;
    return false;
  }

  bool testVodTrackOffset() {
    DTSC::Meta meta;
    meta.reInit("", true);
    meta.setLive(false);
    meta.setMinimumFragmentDuration(1000);
    const size_t timingTrack = addTrack(meta, "video", "H264");
    const size_t requestTrack = addTrack(meta, "audio", "AAC");
    addFixedGop(meta, timingTrack, 2000, 6000, 40);
    for (uint64_t time = 1000; time <= 5000; time += 1000) { meta.update(time, 0, requestTrack, 20, 0, true, 1000); }

    std::map<size_t, Comms::Users> selection;
    selection[timingTrack];
    selection[requestTrack];
    HLS::Generator generator;
    generator.setExt("m4s");
    generator.setParam("mTrack", std::to_string(timingTrack));
    const std::string playlist = generator.subPlaylist(meta, selection, requestTrack, timingTrack);
    return contains(playlist, "chunk_0.m4s", "VOD-relative segment time") && occurrences(playlist, "#EXTINF:") != 0 &&
      contains(playlist, "#EXT-X-ENDLIST", "VOD end marker");
  }

  bool testBlockingReloadValidation() {
    DTSC::Meta meta;
    meta.reInit("", true);
    meta.setLive(true);
    meta.setUTCOffset(1700000000000ull);
    meta.setMinimumFragmentDuration(1000);
    const size_t track = addTrack(meta, "video", "H264");
    addFixedGop(meta, track, 1000, 2500, 40);
    std::map<size_t, Comms::Users> selection;
    selection[track];
    DTSC::Fragments fragments(meta.fragments(track));
    HLS::Generator generator;
    generator.setExt("m4s");
    generator.setPartTarget(500);
    generator.setParam("_HLS_msn", std::to_string(fragments.getEndValid() - 1));
    generator.setParam("_HLS_part", "99");
    const std::string playlist = generator.subPlaylist(meta, selection, track);
    if (playlist == "400") { return true; }
    std::cerr << "invalid advanced part request returned " << playlist << std::endl;
    return false;
  }

  bool testEmptyLivePlaylist() {
    DTSC::Meta meta;
    meta.reInit("", true);
    meta.setLive(true);
    meta.setUTCOffset(1700000000000ull);
    const size_t track = addTrack(meta, "audio", "AAC");
    std::map<size_t, Comms::Users> selection;
    selection[track];
    const std::string playlist = subPlaylist(meta, selection, track);
    return contains(playlist, "#EXT-X-MEDIA-SEQUENCE:", "empty live playlist sequence") && occurrences(playlist, "#EXTINF:") == 0;
  }

  bool testMuxedMaster() {
    DTSC::Meta meta;
    meta.reInit("", true);
    const size_t video = addTrack(meta, "video", "H264");
    const size_t audio = addTrack(meta, "audio", "AAC");
    std::map<size_t, Comms::Users> selection;
    selection[video];
    selection[audio];
    HLS::Generator generator;
    generator.setExt("m4s");
    generator.setMuxed(true);
    const std::string mediaPath = "v" + std::to_string(video) + "/a" + std::to_string(audio);
    generator.setParam("tkn", "session");
    generator.setParam("llhls", "0");
    const std::string playlist = generator.masterPlaylist(meta, selection, video);
    return occurrences(playlist, "#EXT-X-VERSION:") == 1 && contains(playlist, "#EXT-X-VERSION:7", "muxed master version") &&
      contains(playlist, mediaPath + "/index.m3u8?llhls=0&tkn=session", "muxed variant URI");
  }

  bool testPlaybackRangeInMaster() {
    DTSC::Meta meta;
    meta.reInit("", true);
    const size_t video = addTrack(meta, "video", "H264");
    const size_t audio = addTrack(meta, "audio", "AAC");
    std::map<size_t, Comms::Users> selection;
    selection[video];
    selection[audio];

    HLS::Generator generator;
    generator.setExt("m4s");
    generator.setParam("start", "123000");
    generator.setParam("stop", "456000");
    const std::string playlist = generator.masterPlaylist(meta, selection, video);
    return occurrences(playlist, "&start=123000&stop=456000") == 2 && occurrences(playlist, "#EXT-X-START:TIME-OFFSET=0") == 0;
  }

  bool testRenditionReportUsesSiblingPath() {
    DTSC::Meta meta;
    meta.reInit("", true);
    meta.setLive(true);
    meta.setUTCOffset(1700000000000ull);
    meta.setMinimumFragmentDuration(1000);
    const size_t video = addTrack(meta, "video", "H264");
    const size_t audio = addTrack(meta, "audio", "AAC");
    addFixedGop(meta, video, 1000, 11000, 40);
    addFixedGop(meta, audio, 1000, 11000, 40);
    std::map<size_t, Comms::Users> selection;
    selection[video];
    selection[audio];

    HLS::Generator generator;
    generator.setExt("m4s");
    generator.setParam("mTrack", std::to_string(video));
    const std::string playlist = generator.subPlaylist(meta, selection, video, video);
    const std::string sibling = "#EXT-X-RENDITION-REPORT:URI=\"../a" + std::to_string(audio) + "/index.m3u8";
    return contains(playlist, sibling, "rendition report sibling URI");
  }

  bool testAlternateAudioDefaults() {
    DTSC::Meta meta;
    meta.reInit("", true);
    const size_t video = addTrack(meta, "video", "H264");
    const size_t dutch = addTrack(meta, "audio", "AAC");
    const size_t german = addTrack(meta, "audio", "AAC");
    const size_t english = addTrack(meta, "audio", "AAC");
    meta.setLang(dutch, "dut");
    meta.setLang(german, "ger");
    meta.setLang(english, "eng");
    meta.setChannels(dutch, 2);
    meta.setChannels(german, 2);
    meta.setChannels(english, 2);
    std::map<size_t, Comms::Users> selection;
    selection[video];
    selection[dutch];
    selection[german];
    selection[english];

    HLS::Generator generator;
    generator.setExt("m4s");
    const std::string playlist = generator.masterPlaylist(meta, selection, video);
    HLS::Generator tsGenerator;
    const std::string tsPlaylist = tsGenerator.masterPlaylist(meta, selection, video);
    const std::string defaultAudio =
      std::string("LANGUAGE=\"dut\",NAME=\"AAC-dut\",DEFAULT=YES,AUTOSELECT=YES,CHANNELS=\"2\",URI=\"") + "a" +
      std::to_string(dutch) + "/index.m3u8";
    return occurrences(playlist, "TYPE=AUDIO") == 3 && occurrences(playlist, "DEFAULT=YES") == 1 &&
      occurrences(playlist, "AUTOSELECT=YES") == 3 && contains(playlist, defaultAudio, "default alternate audio rendition") &&
      contains(playlist, "LANGUAGE=\"ger\",NAME=\"AAC-ger\",DEFAULT=NO,AUTOSELECT=YES", "non-default alternate audio rendition") &&
      contains(playlist, "v" + std::to_string(video) + "/index.m3u8", "typed video rendition path") &&
      contains(tsPlaylist, std::to_string(video) + "/index.m3u8", "unchanged TS video rendition path");
  }

} // namespace

int main() {
  size_t testCount = 0;
  // TAP header line listing test count
  std::cout << "TAP version 14" << std::endl << "1..12" << std::endl;

#define CHECK_RESPONSE_TEST(expression)                                      \
  if (!(expression)) {                                                       \
    std::cout << "not ok " << ++testCount << " - " #expression << std::endl; \
  } else {                                                                   \
    std::cout << "ok " << ++testCount << " - " #expression << std::endl;     \
  }

  CHECK_RESPONSE_TEST(testVersionsAndPartTargets());
  CHECK_RESPONSE_TEST(testLiveWindowFloor());
  CHECK_RESPONSE_TEST(testPartBoundaries());
  CHECK_RESPONSE_TEST(testExpiredFragmentPayload());
  CHECK_RESPONSE_TEST(testLimitedLivePlaylist());
  CHECK_RESPONSE_TEST(testVodTrackOffset());
  CHECK_RESPONSE_TEST(testBlockingReloadValidation());
  CHECK_RESPONSE_TEST(testEmptyLivePlaylist());
  CHECK_RESPONSE_TEST(testMuxedMaster());
  CHECK_RESPONSE_TEST(testPlaybackRangeInMaster());
  CHECK_RESPONSE_TEST(testRenditionReportUsesSiblingPath());
  CHECK_RESPONSE_TEST(testAlternateAudioDefaults());
  return 0;
}
