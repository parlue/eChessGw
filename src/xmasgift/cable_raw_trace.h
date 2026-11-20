#pragma once
#include <cstddef>
#include <cstdint>

// Optional diagnostic only: preserve bytes before parity stripping/parser
// decisions. A nonzero dropped count makes an incomplete capture explicit.
struct CableRawTrace {
  uint8_t bytes[4096] = {};
  size_t head = 0, count = 0;
  uint32_t received = 0, dropped = 0;
  void push(uint8_t value) {
    ++received;
    if (count == sizeof(bytes)) { ++dropped; return; }
    bytes[(head + count) % sizeof(bytes)] = value;
    ++count;
  }
  size_t take(uint8_t* out, size_t limit) {
    const size_t n = count < limit ? count : limit;
    for (size_t i = 0; i < n; ++i) out[i] = bytes[(head + i) % sizeof(bytes)];
    head = (head + n) % sizeof(bytes); count -= n;
    return n;
  }
};
