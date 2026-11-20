#pragma once
#include <cstdint>
#include <cstring>
#include "delivery_gate.h"
#include "pending_leds.h"

struct MachineVirtualPlan {
  char steps[4][64] = {};
  int from = -1, to = -1;
  char promotion = 0;
  unsigned count = 0;
  uint32_t holdMs = 500;
  uint8_t castleFlags = 0;
  int enPassant = -1;
  const char* target() const { return steps[count - 1]; }
};

// LED-off is evidence only after a complete quiet interval AND fresh host
// traffic. A blink's OFF phase, silence, or a stale clear cannot confirm.
struct MachineVirtualAck {
  bool clearSeen = false;
  uint32_t clearAt = 0, lastEvidenceAt = 0, lastActivityAt = 0;
  uint32_t quietMs = 1000;
  unsigned evidence = 0, activity = 0;
  void reset() { *this = MachineVirtualAck{}; }
  void poll(uint32_t now) {
    lastActivityAt = now;
    if (activity < 100) ++activity;
    if (clearSeen) { lastEvidenceAt = now; if (evidence < 100) ++evidence; }
  }
  void leds(bool allOff, uint32_t now, uint32_t blinkPeriod = 0) {
    const uint32_t interval = blinkPeriod + 250;
    if (interval > quietMs) quietMs = interval;
    if (!allOff) { clearSeen = false; evidence = 0; }
    else if (!clearSeen) { clearSeen = true; clearAt = now; evidence = 0; }
    poll(now);
  }
  bool accepted(uint32_t now) const {
    return clearSeen && evidence >= 3 && uint32_t(lastEvidenceAt - clearAt) >= quietMs &&
        uint32_t(now - lastActivityAt) <= 1000;
  }
  bool responsive(uint32_t now) const {
    return activity >= 3 && uint32_t(now - lastActivityAt) <= 1000;
  }
};

struct MachineVirtualOperator {
  enum Phase { Idle, Moving, ChoosingFigure, RemovingChoice, Confirming, Blocked };
  Phase phase = Idle;
  MachineVirtualPlan plans[4];
  unsigned planCount = 0, choice = 0, step = 0;
  bool sourceSoftware = false, cableOverride = false;
  bool nextFound = false, disconnected = false;
  int nextFrom = -1, nextTo = -1;
  uint64_t nextFields = 0;
  uint64_t replySourceFields = 0;
  uint32_t phaseAt = 0;
  uint8_t sourceFrame[67] = {};
  MachineDeliveryGate sourceDelivery;
  MachineVirtualAck ack;
  MachinePendingLeds replyLeds;
  bool active() const { return phase != Idle; }
  const MachineVirtualPlan& plan() const { return plans[choice]; }
  void reset() {
    // Plans are overwritten before reuse; avoid a >1KB stack temporary.
    phase = Idle; planCount = choice = step = 0;
    sourceSoftware = cableOverride = nextFound = disconnected = false;
    nextFrom = nextTo = -1; nextFields = replySourceFields = 0; phaseAt = 0;
    memset(sourceFrame, 0, sizeof(sourceFrame)); sourceDelivery.reset(); ack.reset(); replyLeds.clear();
  }
};
