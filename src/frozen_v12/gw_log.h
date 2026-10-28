#pragma once
#include <Arduino.h>

// Gateway log recorder (2026-10-12). Everything the firmware prints on Serial is also kept here:
//  - always in a RAM ring (the last ~12 KB, noisy lines dropped, long lines cut, millis() prefix);
//  - only while RECORDING is switched on additionally appended to two rotating files in LittleFS
//    (/gwlog0.txt, /gwlog1.txt, 48 KB each) so the log survives a restart or power loss.
// Recording is OFF by default and is switched on/off by the eChessGateway tag app (see
// gw_log_upload.h); it also switches itself off after 3 restarts or 2 hours of uptime.
// Flash writes happen in blocks (>= 3 KB or once a minute) and only while the cable UART has been
// quiet for 150 ms (a Phoenix streams without pauses: then at the latest after 8 KB / 5 minutes),
// because a flash erase can stall the CPU for tens of milliseconds.
namespace gwlog {
void begin();                              // first thing in setup(): flags from NVS, boot line
void feed(const uint8_t* data, size_t len);  // called by the Serial wrapper (gw_serial.h)
void poll(uint32_t uartRxCounter);         // from loop(): flash flush while recording
void capture(bool on);                     // pause capture around binary Serial transfers
bool recording();
void setRecording(bool on);                // persistent; resets the restart counter
void clearStored();                        // deletes both files
const char* buildStamp();

// Sequential reader over: older file, newer file, then the not yet persisted RAM part.
class Reader {
 public:
  Reader();
  ~Reader();
  size_t totalBytes() const { return total_; }
  size_t read(uint8_t* out, size_t max);   // 0 = end
 private:
  size_t total_ = 0;
  int stage_ = 0;                          // 0 older file, 1 newer file, 2 RAM copy, 3 done
  void* file_ = nullptr;                   // fs::File*
  char* ram_ = nullptr;
  size_t ramLen_ = 0, ramPos_ = 0;
  const char* paths_[2] = {nullptr, nullptr};
  bool openStage();
};
}  // namespace gwlog
