#include "../src/input/page_retry.h"

#include <cstdlib>
#include <iostream>

/// Regression test: repeated under-filled VoD/DVR page loads must be bounded.
/// Before this fix, a page erased "for later retry" was re-requested by waiting
/// outputs every 250ms forever; each retry re-demuxed segments from disk,
/// turning a permanent index/disk mismatch into an unbounded CPU/disk/log
/// amplifier that starved live playlist ingest.

static void require(bool condition, const char *msg){
  if (!condition){
    std::cerr << "FAIL: " << msg << std::endl;
    std::exit(1);
  }
}

int main(){
  const uint32_t maxAttempts = 3;
  const uint64_t cooldown = 30;
  Mist::PageRetryGuard guard(maxAttempts, cooldown);
  uint64_t now = 1000;

  // Fresh page: attempts allowed up to the limit
  require(guard.shouldAttempt(1, 40, now), "first attempt allowed");
  guard.recordFailure(1, 40, now);
  require(guard.shouldAttempt(1, 40, now), "second attempt allowed");
  guard.recordFailure(1, 40, now);
  require(guard.shouldAttempt(1, 40, now), "third attempt allowed");
  guard.recordFailure(1, 40, now);

  // Limit reached: blocked until the cooldown expires
  require(!guard.shouldAttempt(1, 40, now), "blocked after max consecutive failures");
  require(!guard.shouldAttempt(1, 40, now + cooldown - 1), "still blocked within cooldown");
  require(guard.shouldAttempt(1, 40, now + cooldown), "half-open attempt allowed after cooldown");

  // Half-open attempt fails: re-armed for another full cooldown
  guard.recordFailure(1, 40, now + cooldown);
  require(!guard.shouldAttempt(1, 40, now + cooldown + 1), "failed half-open attempt re-arms cooldown");
  require(guard.shouldAttempt(1, 40, now + 2 * cooldown), "next half-open attempt allowed after second cooldown");

  // Success clears all state
  guard.recordSuccess(1, 40);
  require(guard.shouldAttempt(1, 40, now + 2 * cooldown + 1), "success resets the failure count");
  guard.recordFailure(1, 40, now + 2 * cooldown + 1);
  require(guard.shouldAttempt(1, 40, now + 2 * cooldown + 1), "single failure after success does not block");

  // Other pages and tracks are independent
  guard.recordFailure(1, 41, now);
  guard.recordFailure(1, 41, now);
  guard.recordFailure(1, 41, now);
  require(!guard.shouldAttempt(1, 41, now), "sibling page blocks independently");
  require(guard.shouldAttempt(2, 41, now), "same page on another track is unaffected");

  return 0;
}
