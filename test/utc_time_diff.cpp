#include "../lib/timing.h"

#include <cstdlib>
#include <iostream>

/// Regression test: Util::getUTCTimeDiff must never underflow when the parsed
/// timestamp lies in the future relative to the reference time. The rolling
/// playlist pruner treats the returned value as a segment age: an unsigned
/// underflow made future-dated entries look billions of seconds old, causing
/// the recorder to unlink an entire 24h catchup window within seconds when a
/// mis-anchored input produced back-dated "now" values.

static void requireEqual(uint64_t got, uint64_t expected, const char *msg){
  if (got != expected){
    std::cerr << msg << ": got " << got << ", expected " << expected << std::endl;
    std::exit(1);
  }
}

int main(){
  // 2026-07-13T03:00:00Z == 1783911600000 ms since epoch
  const uint64_t refMs = 1783911600000ULL;

  // A segment stamped one hour in the past is 3600 seconds old
  requireEqual(Util::getUTCTimeDiff("2026-07-13T02:00:00.000Z", refMs), 3600,
               "past timestamps age normally");

  // Identical timestamp: age zero
  requireEqual(Util::getUTCTimeDiff("2026-07-13T03:00:00.000Z", refMs), 0,
               "same timestamp has zero age");

  // A segment stamped in the FUTURE must have age zero, not underflow
  requireEqual(Util::getUTCTimeDiff("2026-07-13T04:00:00.000Z", refMs), 0,
               "future timestamps must clamp to zero age");
  requireEqual(Util::getUTCTimeDiff("2026-07-18T03:00:00.000Z", refMs), 0,
               "far-future timestamps must clamp to zero age");

  // Invalid input keeps the existing behavior
  requireEqual(Util::getUTCTimeDiff("", refMs), 0, "empty string is age zero");
  requireEqual(Util::getUTCTimeDiff("2026-07-13T02:00:00.000Z", 0), 0,
               "zero reference time is age zero");

  return 0;
}
