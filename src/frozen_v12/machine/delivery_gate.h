#pragma once
#include <cstdint>

// Completion of UART output is not acknowledgement by the chess computer.
// It is the minimum prerequisite for starting a physical movement's hold.
struct MachineDeliveryGate {
  uint32_t firstSentAt = 0;
  unsigned sends = 0;
  void reset() { firstSentAt = 0; sends = 0; }
  void sent(uint32_t now) { if (!sends) firstSentAt = now; if (sends < 3) ++sends; }
  bool ready(uint32_t now, uint32_t holdMs) const {
    return sends >= 3 && uint32_t(now - firstSentAt) >= holdMs;
  }
};
