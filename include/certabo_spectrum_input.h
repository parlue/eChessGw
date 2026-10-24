#pragma once
#include "certabo_protocol.h"

namespace certabo { namespace spectrum {
// Port of eChessTactics denoise(): never apply to mono Certabo IDs.
// Valid-looking but unknown/duplicate tags are still rejected by Calibration.
class InputFilter {
 public:
  void reset() { previous_={}; holds_={}; }
  RawBoard apply(const RawBoard& raw) {
    RawBoard out=raw;
    for(size_t i=0;i<64;++i) {
      if(raw[i]==PieceId{} || tag(raw[i])) holds_[i]=0;
      else if(tag(previous_[i]) && holds_[i]<3) {
        ++holds_[i]; out[i]=previous_[i];
      } else { holds_[i]=3; out[i]={}; }
    }
    previous_=out;
    return out;
  }
 private:
  static bool tag(const PieceId& id) { return id[0]==2 && id[1]==0; }
  RawBoard previous_{};
  std::array<uint8_t,64> holds_{};
};
}}
