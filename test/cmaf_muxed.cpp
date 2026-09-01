#include <mist/bitfields.h>
#include <mist/cmaf.h>
#include <mist/mp4_dash.h>
#include <mist/mp4_generic.h>

#include <algorithm>
#include <iostream>
#include <vector>

int main() {
  size_t testCount = 0;
  // TAP header line listing test count
  std::cout << "TAP version 14" << std::endl << "1..30" << std::endl;

#define CHECK(expression, title)                                       \
  if (!(expression)) {                                                 \
    std::cout << "not ok " << ++testCount << " - " title << std::endl; \
  } else {                                                             \
    std::cout << "ok " << ++testCount << " - " title << std::endl;     \
  }

  DTSC::Meta meta;
  meta.reInit("", true);
  meta.setLive(true);
  meta.setVod(false);
  meta.setMinimumFragmentDuration(100);

  const size_t video = meta.addTrack(8, 8, 32, 2, true);
  meta.setType(video, "video");
  meta.setCodec(video, "H264");
  meta.update(1000, 0, video, 100, 0, true, 100);
  meta.update(1040, 80, video, 100, 0, false, 100);
  meta.update(1080, 0, video, 100, 0, false, 100);
  meta.update(1120, 0, video, 100, 0, true, 100);
  meta.update(1240, 0, video, 100, 0, true, 100);

  const size_t audio = meta.addTrack(8, 8, 32, 2, true);
  meta.setType(audio, "audio");
  meta.setCodec(audio, "AAC");
  for (uint64_t time = 1000; time <= 1240; time += 20) {
    meta.update(time, 0, audio, 20, 0, time == 1000 || time == 1120 || time == 1240, 20);
  }

  std::map<size_t, Comms::Users> selected;
  Comms::Users videoSelection;
  Comms::Users audioSelection;
  selected.insert(std::make_pair(video, videoSelection));
  selected.insert(std::make_pair(audio, audioSelection));

  Util::ResizeablePointer init;
  CHECK(CMAF::header(init, meta, selected) && init.size() >= 16, "CMAF::header supports multitrack");

  MP4::Box ftyp(init, false);
  CHECK(ftyp.isType("ftyp"), "init begins with ftyp");

  MP4::Box moovBox(init + ftyp.boxedSize(), false);
  CHECK(moovBox.isType("moov"), "init contains moov after ftyp");

  MP4::MOOV & moov = (MP4::MOOV &)moovBox;
  CHECK(moov.getChildren("trak").size() == 2, "init has one trak per selected track");

  MP4::MVEX liveMvex = moov.getChild<MP4::MVEX>();
  CHECK(!liveMvex.getChildren("mehd").size(), "live init has no mehd box");

  Util::ResizeablePointer fragment;
  CHECK(CMAF::fragmentHeader(fragment, meta, selected, video, 1000, 1120, 7), "fragmentHeader succeeds");
  CHECK(fragment.size() >= 16, "fragment header is at least 16 bytes");

  MP4::Box moofBox(fragment, false);
  CHECK(moofBox.isType("moof"), "first fragment box is moof");

  const uint64_t moofSize = moofBox.boxedSize();
  CHECK(moofSize + 8 == fragment.size(), "fragment is only moof+mdat");

  MP4::Box mdatBox(fragment + moofSize, false);
  CHECK(mdatBox.isType("mdat") && mdatBox.boxedSize() == 428, "mdat size is 428");

  MP4::MOOF & moof = (MP4::MOOF &)moofBox;
  std::deque<MP4::Box> trafs = moof.getChildren("traf");
  CHECK(trafs.size() == 2, "moof contains 2 traf boxes")

  std::vector<uint32_t> offsets;
  size_t trunCount = 0;
  size_t sampleCount = 0;
  for (const auto & trafBox : trafs) {
    MP4::TRAF & traf = (MP4::TRAF &)trafBox;
    std::deque<MP4::Box> truns = traf.getChildren("trun");
    trunCount += truns.size();
    for (const auto & trunBox : truns) {
      MP4::TRUN & trun = (MP4::TRUN &)trunBox;
      sampleCount += trun.getSampleInformationCount();
      offsets.push_back(trun.getDataOffset());
    }
  }
  CHECK(trunCount == 9 && sampleCount == 9, "moof has 9 runs and 9 samples");

  std::sort(offsets.begin(), offsets.end());
  const uint32_t mediaStart = moofSize + 8;
  const uint32_t relativeOffsets[] = {0, 100, 120, 140, 240, 260, 280, 380, 400};
  for (size_t i = 0; i < offsets.size(); ++i) {
    CHECK(offsets[i] == mediaStart + relativeOffsets[i], "trun data offset matches");
  }

  // The trun order matches Output's timestamp/track packet ordering.
  const uint64_t expectedTimes[] = {1000, 1000, 1020, 1040, 1040, 1060, 1080, 1080, 1100};
  const size_t expectedTracks[] = {video, audio, audio, video, audio, audio, video, audio, audio};
  uint64_t describedPayload = 0;
  std::string completeSegment(fragment, fragment.size());
  for (size_t i = 0; i < 9; ++i) {
    DTSC::Parts parts(meta.parts(expectedTracks[i]));
    const size_t part = meta.getPartIndex(expectedTimes[i], expectedTracks[i]);
    const uint32_t size = parts.getSize(part);
    describedPayload += size;
    completeSegment.append(size, (char)((expectedTracks[i] + part) & 0xFF));
  }
  CHECK(describedPayload == 420 && mdatBox.boxedSize() == describedPayload + 8, "declared payload matches actual");

  CHECK(completeSegment.size() == moofSize + mdatBox.boxedSize(), "segment matches boxes");

  Util::ResizeablePointer compatibilityOutput;
  const bool compatibilityBuilt = CMAF::fragmentHeader(compatibilityOutput, meta, selected, 0);
  CHECK(compatibilityBuilt && compatibilityOutput.size() == fragment.size(), "fragment by index matches fragment by range");

  const size_t lateAudio = meta.addTrack(8, 8, 16, 2, true);
  meta.setType(lateAudio, "audio");
  meta.setCodec(lateAudio, "AAC");
  meta.update(1200, 0, lateAudio, 20, 0, true, 20);
  meta.update(1220, 0, lateAudio, 20, 0, false, 20);
  std::map<size_t, Comms::Users> sparseSelection;
  sparseSelection[video];
  sparseSelection[lateAudio];
  Util::ResizeablePointer sparse;
  CHECK(CMAF::fragmentHeader(sparse, meta, sparseSelection, video, 1000, 1120, 8), "empty optional track is handled");

  MP4::Box sparseMoofBox((char *)sparse, false);
  MP4::MOOF & sparseMoof = (MP4::MOOF &)sparseMoofBox;
  CHECK(sparseMoof.getChildren("traf").size() == 1, "empty track has no traf");

  DTSC::Meta vod;
  vod.reInit("", true);
  vod.setMinimumFragmentDuration(100);
  vod.setVod(true);
  vod.setLive(false);

  const size_t vodVideo = vod.addTrack(8, 8, 16, 2, true);
  vod.setType(vodVideo, "video");
  vod.setCodec(vodVideo, "H264");
  vod.update(1000, 0, vodVideo, 100, 0, true, 100);
  vod.update(1040, 0, vodVideo, 100, 0, false, 100);
  vod.update(1080, 0, vodVideo, 100, 0, false, 100);
  vod.update(1120, 0, vodVideo, 100, 0, true, 100);

  const size_t vodAudio = vod.addTrack(8, 8, 16, 2, true);
  vod.setType(vodAudio, "audio");
  vod.setCodec(vodAudio, "AAC");
  for (uint64_t time = 1020; time <= 1140; time += 20) {
    vod.update(time, 0, vodAudio, 20, 0, time == 1020 || time == 1120, 20);
  }

  std::map<size_t, Comms::Users> vodSelection;
  vodSelection[vodVideo];
  vodSelection[vodAudio];
  Util::ResizeablePointer vodInit;
  CHECK(CMAF::header(vodInit, vod, vodSelection), "two VoD tracks builds header");

  char *vodInitData = vodInit;
  MP4::Box vodFtyp(vodInitData, false);
  MP4::Box vodMoovBox(vodInitData + vodFtyp.boxedSize(), false);
  MP4::MOOV & vodMoov = (MP4::MOOV &)vodMoovBox;
  MP4::MVEX vodMvex = vodMoov.getChild<MP4::MVEX>();
  std::deque<MP4::Box> vodMehds = vodMvex.getChildren("mehd");
  const uint64_t expectedVodDuration = std::max(vod.getDuration(vodVideo), vod.getDuration(vodAudio));
  CHECK(vodMehds.size() == 1 && ((MP4::MEHD &)vodMehds.front()).getFragmentDuration() == expectedVodDuration,
        "VoD has presentation duration");

  Util::ResizeablePointer vodFragment;
  CHECK(CMAF::fragmentHeader(vodFragment, vod, vodSelection, vodVideo, 1000, 1120, 9), "staggered tracks");

  MP4::Box vodMoofBox((char *)vodFragment, false);
  MP4::MOOF & vodMoof = (MP4::MOOF &)vodMoofBox;
  std::deque<MP4::Box> vodTrafs = vodMoof.getChildren("traf");
  std::map<uint32_t, uint64_t> decodeTimes;
  for (std::deque<MP4::Box>::iterator trafBox = vodTrafs.begin(); trafBox != vodTrafs.end(); ++trafBox) {
    MP4::TRAF & traf = (MP4::TRAF &)*trafBox;
    const uint32_t trackId = traf.getChild<MP4::TFHD>().getTrackID();
    decodeTimes[trackId] = traf.getChild<MP4::TFDT>().getBaseMediaDecodeTime();
  }
  CHECK(!decodeTimes[vodVideo + 1] && decodeTimes[vodAudio + 1] == 20, "VoD 20ms offset intact");

  return 0;
}
