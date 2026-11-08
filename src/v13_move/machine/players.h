#pragma once
#include <cstdint>

struct MachinePlayers {
  int whiteEndpoint = -1; // 0 cable, 1 software; unknown until a legal opening.
  uint64_t opening[2] = {};
  uint32_t seen[2][64] = {};
  void reset() { *this = MachinePlayers{}; }
  bool assigned() const { return whiteEndpoint >= 0; }
  bool accepts(bool software, bool whiteTurn) const {
    return !assigned() ? whiteTurn : ((int(software) == whiteEndpoint) == whiteTurn);
  }
  void assignFirst(bool software) { if (!assigned()) whiteEndpoint = int(software); }
  void clearOpening(bool software) { opening[int(software)] = 0; }
  uint64_t accumulate(bool software, uint64_t mask, uint32_t now) {
    const int side = int(software);
    for (int sq = 0; sq < 64; ++sq) {
      const uint64_t bit = uint64_t(1) << sq;
      if (mask & bit) { opening[side] |= bit; seen[side][sq] = now; }
      else if (now - seen[side][sq] > 2000) opening[side] &= ~bit;
    }
    return opening[side];
  }
};
