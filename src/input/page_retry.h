#pragma once

#include <cstdint>
#include <cstddef>
#include <map>
#include <utility>

namespace Mist{

  /// Bounds repeated VoD/DVR page load attempts after under-filled loads.
  ///
  /// When a bufferFrame page load produces fewer parts than the metadata
  /// expects (index/disk divergence), the page is erased "for later retry" and
  /// the waiting output re-requests it on every 250ms serve loop, forever.
  /// Each retry re-seeks and re-demuxes segments from disk, so a permanent
  /// mismatch becomes an unbounded CPU/disk/log amplifier that starves live
  /// playlist ingest. This guard allows a bounded burst of attempts per
  /// (track, page), then blocks further attempts until a cooldown passes;
  /// after the cooldown a single half-open attempt is allowed, and a failure
  /// re-arms the cooldown. A successful load clears all state for the page.
  class PageRetryGuard{
  public:
    PageRetryGuard(uint32_t maxAttempts = 3, uint64_t cooldownSecs = 30)
        : maxAttempts(maxAttempts), cooldownSecs(cooldownSecs){}

    /// Returns true if a load attempt for this page may proceed at nowSecs.
    bool shouldAttempt(size_t track, uint32_t page, uint64_t nowSecs) const{
      std::map<std::pair<size_t, uint32_t>, State>::const_iterator it =
          states.find(std::make_pair(track, page));
      if (it == states.end()){return true;}
      if (it->second.failures < maxAttempts){return true;}
      return nowSecs >= it->second.lastFailure + cooldownSecs;
    }

    /// Records an under-filled/aborted load of this page.
    void recordFailure(size_t track, uint32_t page, uint64_t nowSecs){
      State &state = states[std::make_pair(track, page)];
      if (state.failures < UINT32_MAX){++state.failures;}
      state.lastFailure = nowSecs;
      // Opportunistically drop long-expired entries so the map stays bounded
      if (states.size() > 4096){
        std::map<std::pair<size_t, uint32_t>, State>::iterator cleanIt = states.begin();
        while (cleanIt != states.end()){
          if (cleanIt->second.lastFailure + 10 * cooldownSecs < nowSecs){
            states.erase(cleanIt++);
          }else{
            ++cleanIt;
          }
        }
      }
    }

    /// Records a fully-buffered load of this page, clearing its failure state.
    void recordSuccess(size_t track, uint32_t page){
      states.erase(std::make_pair(track, page));
    }

  private:
    struct State{
      State() : failures(0), lastFailure(0){}
      uint32_t failures;
      uint64_t lastFailure;
    };
    uint32_t maxAttempts;
    uint64_t cooldownSecs;
    std::map<std::pair<size_t, uint32_t>, State> states;
  };

}// namespace Mist
