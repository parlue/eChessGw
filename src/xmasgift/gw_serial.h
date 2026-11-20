#pragma once
#include <Arduino.h>
#include "gw_log.h"

// Include this as the LAST #include of a source file: from there on `Serial` is a wrapper that
// forwards everything to the real USB serial port unchanged and additionally feeds gwlog.
// (The frozen _vNN snapshots do not include it and behave exactly as before.)
class GwSerialTee : public Stream {
 public:
  explicit GwSerialTee(HWCDC& real) : real_(real) {}
  void begin(unsigned long baud) { real_.begin(baud); }
  void setTxTimeoutMs(uint32_t ms) { real_.setTxTimeoutMs(ms); }
  size_t setRxBufferSize(size_t n) { return real_.setRxBufferSize(n); }
  int available() override { return real_.available(); }
  int read() override { return real_.read(); }
  int peek() override { return real_.peek(); }
  void flush() override { real_.flush(); }
  size_t write(uint8_t c) override { gwlog::feed(&c, 1); return real_.write(c); }
  size_t write(const uint8_t* b, size_t n) override { gwlog::feed(b, n); return real_.write(b, n); }
  using Print::write;

 private:
  HWCDC& real_;
};
extern GwSerialTee gwSerial;

#ifndef GW_NO_SERIAL_MACRO
#define Serial gwSerial
#endif
