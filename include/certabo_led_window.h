#pragma once
#include <stdint.h>
#include <cstring>
namespace certabo {
// Keep every highlight seen between physical writes. Phoenix alternates
// source/destination/blank faster than a mono board may be written.
class LedWindow {
 public:
  void add(const uint8_t mask[8]) { for(int i=0;i<8;++i) pending_[i]|=mask[i]; }
  void snapshot(const uint8_t latest[8],uint8_t result[8]) const {
    for(int i=0;i<8;++i) result[i]=latest[i]|pending_[i];
  }
  void delivered() { memset(pending_,0,sizeof(pending_)); }
 private:
  uint8_t pending_[8]{};
};
}
