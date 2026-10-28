#pragma once
#include <cstdint>

// Distinct nonempty LED masks from the next player during a physical move.
// Empty blink phases and X acknowledgements do not cancel a queued move.
struct MachinePendingLeds {
  uint64_t masks[8] = {};
  unsigned count = 0;
  void clear() { count = 0; }
  bool add(uint64_t mask) {
    if (!mask) return true;
    for (unsigned i = 0; i < count; ++i) if (masks[i] == mask) return true;
    if (count == 8) return false;
    masks[count++] = mask;
    return true;
  }
  template<class Match> int resolve(Match match, int& from, int& to) const {
    bool found = false;
    for (unsigned i = 0; i < count; ++i) for (unsigned j = i; j < count; ++j) {
      int f = -1, t = -1;
      const int n = match(masks[i] | masks[j], f, t);
      if (n > 1) return 2; // Includes unspecified promotion choice.
      if (n == 0) continue;
      if (found && (f != from || t != to)) return 2;
      from = f; to = t; found = true;
    }
    return found ? 1 : 0;
  }
};
