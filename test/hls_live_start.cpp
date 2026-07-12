#include "../src/input/input_hls.h"

#include <cstdlib>
#include <deque>
#include <iostream>

static Mist::playListEntries entry(uint64_t bytePos, uint64_t timestamp){
  Mist::playListEntries e;
  e.bytePos = bytePos;
  e.timestamp = timestamp;
  return e;
}

static void requireEqual(size_t got, size_t expected, const char *msg){
  if (got != expected){
    std::cerr << msg << ": got " << got << ", expected " << expected << std::endl;
    std::exit(1);
  }
}

int main(){
  std::deque<Mist::playListEntries> entries;
  entries.push_back(entry(100, 1000));
  entries.push_back(entry(101, 4000));
  entries.push_back(entry(102, 7000));
  entries.push_back(entry(103, 9000));
  entries.push_back(entry(104, 10000));

  requireEqual(Mist::hlsLiveStartIndex(entries, 0), 0, "zero buffer preserves old behavior");
  requireEqual(Mist::hlsLiveStartIndex(entries, 100000), 0, "large buffer keeps full playlist");
  requireEqual(Mist::hlsLiveStartIndex(entries, 3000), 2, "tail buffer starts at first entry inside window");
  requireEqual(Mist::hlsLiveStartIndex(entries, 1), 4, "tiny buffer keeps the tail segment");

  std::deque<Mist::playListEntries> zeroStart;
  zeroStart.push_back(entry(200, 0));
  zeroStart.push_back(entry(201, 3000));
  zeroStart.push_back(entry(202, 6000));
  zeroStart.push_back(entry(203, 9000));
  requireEqual(Mist::hlsLiveStartIndex(zeroStart, 3000), 2,
               "a valid zero-based timeline still selects the live tail");

  std::deque<Mist::playListEntries> noTimestamps;
  noTimestamps.push_back(entry(1, 0));
  noTimestamps.push_back(entry(2, 0));
  requireEqual(Mist::hlsLiveStartIndex(noTimestamps, 3000), 0, "missing timestamps preserves full playlist");

  return 0;
}
