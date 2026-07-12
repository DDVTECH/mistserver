#include <mist/defines.h>
#include <mist/dtsc.h>

#include <iostream>
#include <set>
#include <string>
#include <unistd.h>

static bool expect(bool condition, const std::string &message){
  if (condition){return true;}
  std::cerr << "FAIL: " << message << std::endl;
  return false;
}

int main(){
  DTSC::Meta original;
  const size_t track = original.addTrack(16, 32, 64, 8);
  bool ok = true;

  ok &= expect(track != INVALID_TRACK_ID, "create metadata track");
  if (track == INVALID_TRACK_ID){return 1;}

  original.setID(track, 42);
  original.setType(track, "video");
  original.setCodec(track, "test");
  original.setInit(track, "test-init");
  original.setLive(true);

  const size_t packetCount = 10;
  for (size_t i = 0; i < packetCount; ++i){
    original.update(i * 1000, 0, track, 100 + i, 1000 + i, true);
  }

  Util::RelAccX &pages = original.pages(track);
  pages.setInt("firstkey", 0, 0);
  pages.setInt("keycount", packetCount, 0);
  pages.setInt("parts", packetCount, 0);
  pages.setInt("size", 4096, 0);
  pages.setInt("avail", 0, 0);
  pages.setInt("firsttime", 0, 0);
  pages.setInt("lastkeytime", (packetCount - 1) * 1000, 0);
  pages.addRecords(1);

  ok &= expect(original.removeFirstKey(track), "prune first key");
  ok &= expect(original.removeFirstKey(track), "prune second key");
  ok &= expect(original.removeFirstKey(track), "prune third key");

  const Util::RelAccX &originalFragments = original.fragments(track);
  const Util::RelAccX &originalKeyRecords = original.keys(track);
  ok &= expect(originalFragments.getStartPos() != originalKeyRecords.getStartPos(),
               "fragment and key ring starts diverge after pruning");

  char pathTemplate[] = "/tmp/mist-dtsh-pruned-roundtrip-XXXXXX";
  int tempFd = mkstemp(pathTemplate);
  ok &= expect(tempFd >= 0, "create checkpoint path");
  if (tempFd < 0){return 1;}
  close(tempFd);
  unlink(pathTemplate);

  const std::string checkpointPath(pathTemplate);
  original.toFile(checkpointPath);
  DTSC::Meta restored("", checkpointPath);
  unlink(checkpointPath.c_str());

  std::set<size_t> restoredTracks = restored.getValidTracks();
  ok &= expect(restoredTracks.size() == 1, "reload exactly one track");
  if (restoredTracks.size() != 1){return 1;}

  const size_t restoredTrack = *restoredTracks.begin();
  const DTSC::Keys originalKeys = original.getKeys(track);
  const DTSC::Keys restoredKeys = restored.getKeys(restoredTrack);
  ok &= expect(originalKeys.getValidCount() == restoredKeys.getValidCount(),
               "preserve key count");

  const size_t originalFirst = originalKeys.getFirstValid();
  const size_t restoredFirst = restoredKeys.getFirstValid();
  for (size_t i = 0; i < originalKeys.getValidCount(); ++i){
    const size_t before = originalFirst + i;
    const size_t after = restoredFirst + i;
    ok &= expect(originalKeys.getBpos(before) == restoredKeys.getBpos(after),
                 "preserve key segment position at index " + std::to_string(i));
    ok &= expect(originalKeys.getTime(before) == restoredKeys.getTime(after),
                 "preserve key time at index " + std::to_string(i));
    ok &= expect(originalKeys.getParts(before) == restoredKeys.getParts(after),
                 "preserve key part count at index " + std::to_string(i));
    ok &= expect(originalKeys.getSize(before) == restoredKeys.getSize(after),
                 "preserve key size at index " + std::to_string(i));
  }

  if (!ok){return 1;}
  std::cout << "PASS: pruned DTSH key metadata round-trip" << std::endl;
  return 0;
}
