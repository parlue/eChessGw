#pragma once

#include <stdint.h>

// Application-level health, independent of NimBLE's radio connection state.
// Silence alone triggers a probe, never a disconnect. An unchanged valid
// status is sufficient to answer a probe; no physical move is required.
class MillenniumLinkWatchdog {
 public:
  enum class Action { None, Probe, Disconnect };
  static constexpr uint32_t kQuietMs = 10000;
  static constexpr uint32_t kReplyMs = 5000;

  void reset(uint32_t now) {
    lastReplyMs_ = now;
    probeMs_ = now;
    probes_ = 0;
    writeFailures_ = 0;
  }

  void noteWrite(bool success) {
    if (success) writeFailures_ = 0;
    else if (writeFailures_ < 3) ++writeFailures_;
  }

  void noteReply(uint32_t now, bool status) {
    lastReplyMs_ = now;
    if (status) probes_ = 0;
  }

  bool writeFailedRepeatedly() const { return writeFailures_ >= 3; }

  Action poll(uint32_t now) {
    if (writeFailedRepeatedly()) return Action::Disconnect;
    if (probes_ != 0) {
      if (static_cast<uint32_t>(now - probeMs_) < kReplyMs) return Action::None;
      if (probes_ >= 2) return Action::Disconnect;
    } else if (static_cast<uint32_t>(now - lastReplyMs_) < kQuietMs) {
      return Action::None;
    }
    ++probes_;
    probeMs_ = now;
    return Action::Probe;
  }

 private:
  uint32_t lastReplyMs_ = 0;
  uint32_t probeMs_ = 0;
  uint8_t probes_ = 0;
  uint8_t writeFailures_ = 0;
};
