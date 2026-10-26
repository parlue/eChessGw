#pragma once
#include "certabo_protocol.h"

namespace certabo {
// Only protocol/transport evidence identifies a board. Figure IDs are shared.
// 64-byte position-start notifications fingerprint the observed Spectrum bridge;
// larger ones fingerprint the classic bridge at our unchanged MTU request 256.
// This is a bridge firmware fingerprint, not a universal BLE guarantee.
class LedIdentity {
 public:
  void reset() {
    marker_=LedType::Unknown; positions_=shortStarts_=wideStarts_=0;
    buffered_=0; resetPending();
  }
  void notification(size_t length, uint8_t first) {
    if(first!=':') return;
    if(length==64 && shortStarts_<3) ++shortStarts_;
    else if(length>64 && wideStarts_<3) ++wideStarts_;
  }
  // Bounded initial-position buffer. IDs are only replayed to calibration;
  // classification never examines their values.
  void observe(const RawBoard& board) {
    if(positions_<3) ++positions_;
    if(buffered_==pending_.size()) {
      for(size_t i=1;i<buffered_;++i) pending_[i-1]=pending_[i];
      --buffered_;
    }
    pending_[buffered_++]=board;
  }
  bool pop(RawBoard& board) {
    if(type()==LedType::Unknown || !buffered_) return false;
    board=pending_[0];
    for(size_t i=1;i<buffered_;++i) pending_[i-1]=pending_[i];
    --buffered_; return true;
  }
  size_t buffered() const { return buffered_; }
  void discardBuffered() { buffered_=0; }
  bool positionSeen() const { return positions_!=0; }
  bool spectrum() const { return type()==LedType::Rgb; }
  void resetPending() { token_=0; length_=0; }
  void streamGap() { reset(); }
  void feed(uint8_t byte) {
    if(byte=='\r' || byte=='\n' || byte==':') {
      if(length_==1 && (token_=='L' || token_=='D')) {
        // Spectrum also emits L; it is not an exclusive mono identifier.
        if(token_=='D') marker_=LedType::Rgb;
        else if(marker_==LedType::Unknown) marker_=LedType::Mono;
      }
      resetPending();
      if(byte==':') length_=2; // no markers inside position payloads
      return;
    }
    if(length_==0) token_=byte;
    if(length_<2) ++length_;
  }
  LedType type() const {
    if(conflict()) return LedType::Unknown;
    if(marker_==LedType::Rgb) return LedType::Rgb;
    if(positions_<3) return LedType::Unknown;
    if(shortStarts_>=3) return LedType::Rgb;
    if(wideStarts_>=3) return LedType::Mono;
    return LedType::Unknown;
  }
  bool conflict() const {
    return (shortStarts_ && wideStarts_) ||
        (marker_==LedType::Rgb && wideStarts_);
  }
 private:
  LedType marker_=LedType::Unknown;
  uint8_t positions_=0, shortStarts_=0, wideStarts_=0;
  uint8_t token_=0, length_=0;
  std::array<RawBoard,3> pending_{};
  size_t buffered_=0;
};
}
