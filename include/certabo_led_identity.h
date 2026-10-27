#pragma once
#include "certabo_protocol.h"

namespace certabo {
// Explicit user choice through live setup: d3/d6 = mono, e3/e6 = Spectrum.
// No packet size, marker, MAC or piece-ID prefix can select this driver.
class LedIdentity {
 public:
  void reset() { selected_=LedType::Unknown; }
  void observe(const Calibration& calibration) {
    if(selected_==LedType::Unknown && calibration.ready() && calibration.count()==34)
      selected_=calibration.setupType();
  }
  LedType type() const { return selected_; }
  bool spectrum() const { return selected_==LedType::Rgb; }
 private:
  LedType selected_=LedType::Unknown;
};
}
