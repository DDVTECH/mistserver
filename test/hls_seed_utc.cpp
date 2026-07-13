#include "../src/input/input_hls.h"

#include <cstdlib>
#include <deque>
#include <iostream>

/// Regression test: a live playlist without EXT-X-PROGRAM-DATE-TIME must get a
/// synthetic wall-clock anchor at first load, with the newest entry ending at
/// "now". Without this, the ingest anchors to the source's raw TS timestamp
/// history: a source transcoder running for 4.5 days produced a catchup
/// recording stamped 4.5 days in the past, whose future-dated older entries
/// were then mass-pruned by the recorder.

static Mist::playListEntries entry(float durationSecs, uint64_t mUTC = 0){
  Mist::playListEntries e;
  e.duration = durationSecs;
  e.mUTC = mUTC;
  return e;
}

static void require(bool cond, const char *msg){
  if (!cond){
    std::cerr << "FAIL: " << msg << std::endl;
    std::exit(1);
  }
}

int main(){
  const uint64_t now = 1783911600000ULL; // 2026-07-13T03:00:00Z

  // Three 4s entries, no UTC info anywhere: anchor the tail at now
  std::deque<Mist::playListEntries> entries;
  entries.push_back(entry(4.0));
  entries.push_back(entry(4.0));
  entries.push_back(entry(4.0));
  require(Mist::hlsSeedEntryUTC(entries, now), "seeding reports it anchored the window");
  require(entries[2].mUTC == now - 4000, "newest entry starts one duration before now");
  require(entries[1].mUTC == now - 8000, "middle entry backfills consecutively");
  require(entries[0].mUTC == now - 12000, "oldest entry backfills consecutively");

  // Any pre-existing UTC info means the source is authoritative: do not touch
  std::deque<Mist::playListEntries> hasUTC;
  hasUTC.push_back(entry(4.0));
  hasUTC.push_back(entry(4.0, 1234567890123ULL));
  require(!Mist::hlsSeedEntryUTC(hasUTC, now), "existing UTC info is never overwritten");
  require(hasUTC[0].mUTC == 0 && hasUTC[1].mUTC == 1234567890123ULL,
          "entries stay untouched when any UTC info exists");

  // Empty window: nothing to do
  std::deque<Mist::playListEntries> empty;
  require(!Mist::hlsSeedEntryUTC(empty, now), "empty playlist is not seeded");

  return 0;
}
