#pragma once
#include "virtual_operator.h"
// Preflight is visible ONLY to the moving host, never the other engine/robot.
struct MachinePhysicalPromotion {
  enum Phase { Idle, Preparing, Choosing, Removing, Executing, Blocked };
  Phase phase = Idle;
  MachineVirtualPlan plans[4];
  unsigned count = 0, choice = 0, step = 0;
  bool sourceSoftware = false, paused = false;
  uint32_t phaseAt = 0;
  uint8_t frame[67] = {};
  MachineDeliveryGate delivery;
  MachineVirtualAck ack;
  bool negotiating() const { return phase==Preparing || phase==Choosing || phase==Removing; }
  bool active() const { return phase!=Idle; }
  const MachineVirtualPlan& plan() const { return plans[choice]; }
  void reset() {
    phase=Idle;count=choice=step=0;sourceSoftware=paused=false;phaseAt=0;
    delivery.reset();ack.reset();memset(frame,0,sizeof(frame));
  }
};
