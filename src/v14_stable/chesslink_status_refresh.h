#pragma once

#include <stdint.h>

// One current-position refresh after a configuration burst. The idle
// interval is a compatibility grace period, not a Mode-B protocol timer.
class ChesslinkStatusRefresh {
 public:
  static constexpr uint32_t kIdleMs = 500;

  void reset() { pending_ = false; }
  void noteTraffic(uint32_t now) { lastTrafficAt_ = now; }
  void schedule(uint32_t now) {
    pending_ = true;
    noteTraffic(now);
  }
  bool due(uint32_t now) const {
    return pending_ && static_cast<uint32_t>(now - lastTrafficAt_) >= kIdleMs;
  }

 private:
  bool pending_ = false;
  uint32_t lastTrafficAt_ = 0;
};
