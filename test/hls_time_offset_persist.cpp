#include "../src/input/input_hls.h"

#include <cstdlib>
#include <iostream>

/// Regression test: live-parsed playlist entries must persist the timestamp
/// remap offset derived during ingest, so that later reparses (seek /
/// bufferFrame page loads) apply the exact same timestamps as the original
/// live parse. Before this fix, only header-parsed entries persisted their
/// offset; live-parsed entries checkpointed timeOffset=0 and every catchup
/// reparse re-derived a per-segment anchor, misaligning key spans and causing
/// "Key N was X bytes but should've been Y" page-load storms.

static void requireEqual(int64_t got, int64_t expected, const char *msg){
  if (got != expected){
    std::cerr << msg << ": got " << got << ", expected " << expected << std::endl;
    std::exit(1);
  }
}

int main(){
  // No stored offset yet: adopt the offset derived during this parse
  requireEqual(Mist::hlsPersistTimeOffset(0, true, 1752301000123LL), 1752301000123LL,
               "live-parsed entry adopts the derived remap offset");

  // Entry already has an offset (header parse or earlier persist): keep it,
  // even if a different offset was derived later in the same playlist run
  requireEqual(Mist::hlsPersistTimeOffset(500, true, 900), 500,
               "existing entry offset wins over later derived offset");

  // Nothing known: stays unset
  requireEqual(Mist::hlsPersistTimeOffset(0, false, 0), 0,
               "no offset known leaves entry unset");

  // Negative offsets are valid (media timestamps ahead of zUTC) and must persist
  requireEqual(Mist::hlsPersistTimeOffset(0, true, -86400123LL), -86400123LL,
               "negative derived offsets persist unchanged");
  requireEqual(Mist::hlsPersistTimeOffset(-42, true, 900), -42,
               "existing negative entry offset wins over derived offset");

  return 0;
}
