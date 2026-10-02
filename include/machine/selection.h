#pragma once
#include <atomic>

// Lifetime selection. Disconnects never reopen the race. The BLE host task
// and the board-connect task can finish concurrently; only one may win.
class MachineSelection {
 public:
  enum class Mode { Undecided, Physical, Machine, MachineBoard, MachineVirtual };
  bool cableActivity() {
    cable.store(true);
    return mode.load() == Mode::Undecided;
  }
  bool physicalConnected() {
    Mode expected = Mode::Undecided;
    if (mode.compare_exchange_strong(expected, Mode::Physical) || expected == Mode::Physical) return true;
    if (expected == Mode::MachineBoard) return true;
    expected = Mode::Machine;
    return mode.compare_exchange_strong(expected, Mode::MachineBoard);
  }
  bool softwareConnected() {
    if (!cable.load()) return true; // Existing standalone masquerade.
    Mode expected = Mode::Undecided;
    return mode.compare_exchange_strong(expected, Mode::Machine) || (expected == Mode::Machine || expected == Mode::MachineBoard || expected == Mode::MachineVirtual);
  }
  bool machine() const { const auto m = mode.load(); return m == Mode::Machine || m == Mode::MachineBoard || m == Mode::MachineVirtual; }
  bool board() const { return mode.load() == Mode::MachineBoard; }
  bool virtualOnly() const { return mode.load() == Mode::MachineVirtual; }
  bool lockVirtual() {
    Mode expected = Mode::Machine;
    return mode.compare_exchange_strong(expected, Mode::MachineVirtual);
  }
  bool physical() const { return mode.load() == Mode::Physical; }
 private:
  std::atomic<Mode> mode{Mode::Undecided};
  std::atomic<bool> cable{false};
};
