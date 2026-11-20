#pragma once
#include <cstdint>

// Virtual Engine-vs-Engine only. Phoenix can keep blinking its completed
// move while the other engine thinks and while its reply is being delivered.
// An OFF blink is not a fresh move command. Keep ignoring that old field set
// until Phoenix indicates another square or explicitly clears on its turn.
// This also prevents the old pair becoming a phantom reverse move next turn.
struct MachineCableEcho {
  uint64_t completedFields = 0;
  void clear() { completedFields = 0; }
  void completed(uint64_t fields) { completedFields = fields; }
  bool ignores(uint64_t fields) {
    if (!fields || !completedFields) return false;
    if (fields & ~completedFields) { clear(); return false; }
    return true;
  }
};
