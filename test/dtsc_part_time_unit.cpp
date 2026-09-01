#include <mist/dtsc.h>

#include <iostream>

class PartTimeMeta : public DTSC::Meta {
  public:
    void dropFirstKey(size_t track) {
      DTSC::Track & t = tracks.at(track);
      uint64_t partsToDelete = t.keys.getInt(t.keyPartsField, t.keys.getDeleted());
      t.parts.deleteRecords(partsToDelete);
      t.keys.deleteRecords(1);
      setFirstms(track, t.keys.getInt(t.keyTimeField, t.keys.getDeleted()));
    }
};

int main() {
  size_t testCount = 0;
  // TAP header line listing test count
  std::cout << "TAP version 14" << std::endl << "1..4" << std::endl;

#define CHECK(expression, title)                                       \
  if (!(expression)) {                                                 \
    std::cout << "not ok " << ++testCount << " - " title << std::endl; \
  } else {                                                             \
    std::cout << "ok " << ++testCount << " - " title << std::endl;     \
  }

  PartTimeMeta meta;
  meta.reInit("", true);

  size_t track = meta.addTrack(8, 8, 16, 2, true);
  meta.setType(track, "video");
  meta.setCodec(track, "H264");

  meta.update(1000, 0, track, 100, 0, true, 100);
  meta.update(1500, 0, track, 100, 0, false, 100);
  meta.update(2000, 0, track, 100, 0, true, 100);
  meta.update(2500, 0, track, 100, 0, false, 100);
  meta.update(3000, 0, track, 100, 0, true, 100);
  meta.update(3500, 0, track, 100, 0, false, 100);

  meta.dropFirstKey(track);

  DTSC::Parts parts(meta.parts(track));
  uint64_t firstPartTime = meta.getPartTime(parts.getFirstValid(), track);
  CHECK(firstPartTime == 2000, "first part at 2000ms");

  CHECK(!meta.getPartTime(parts.getFirstValid() - 1, track), "deleted parts have no timestamp");

  const size_t finalPart = parts.getEndValid() - 1;
  CHECK(meta.getPartIndex(meta.getPartTime(finalPart, track), track) == finalPart, "final part timestamp matches");

  const uint64_t finalEnd = meta.getPartTime(finalPart, track) + parts.getDuration(finalPart);
  CHECK(meta.getPartIndex(finalEnd, track) == parts.getEndValid(), "final part resolves to the half-open range end");

  return 0;
}
