#pragma once
#include <stdint.h>
namespace certabo {
// King-only echo filter for the direct one-shot LED path. A King answers every
// physical move with a replay of that move's own two squares (hardware capture
// 2026-10-11: e2/e4, g1/f3, d2/d3 replayed ~140 ms after the board scan) and
// only then with its real suggestion. MoveLights can recognise that replay only
// when a suggestion was added BEFORE the move arrived (the King's lift frame);
// for a fast move, or the very first move after connecting, nothing was known
// yet and the replay was shown for 120 ms. The board scan itself already tells
// us exactly which squares changed, so remember them and drop any frame lying
// completely inside that set.
//
// Cleared as soon as the next move starts (any lift), so the King's lift frame
// of a piece that just arrived there is never filtered, and bounded in time as
// a safety net. Phoenix never reads this: it uses certaboSetMoveLights().
class MoveEcho {
 public:
  static constexpr uint32_t kWindowMs = 4000;
  void reset() { mask_ = 0; at_ = 0; }
  // A piece arrived. `changed` has one bit per square that differs from the
  // previous complete position. Plain/capture = 2, en passant = 3, castling = 4;
  // anything larger is a setup/reset, not a single move.
  void arrived(uint64_t changed, int differences, uint32_t now) {
    mask_ = (differences >= 2 && differences <= 4) ? changed : 0;
    at_ = now;
  }
  void lifted() { mask_ = 0; }
  bool isEcho(uint64_t incoming, uint32_t now) const {
    return mask_ && incoming && !(incoming & ~mask_) && uint32_t(now - at_) < kWindowMs;
  }
 private:
  uint64_t mask_ = 0;
  uint32_t at_ = 0;
};
}
