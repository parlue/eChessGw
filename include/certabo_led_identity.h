#pragma once
#include "certabo_protocol.h"

namespace certabo {
// Explicit user choice through live setup: d3/d6 = mono, e3/e6 = Spectrum.
// No packet size, marker, MAC or piece-ID prefix can select this driver.
class LedIdentity {
 public:
  void reset() { selected_=LedType::Unknown; resetSetupWindow(); }
  void resetSetupWindow() { candidate_=LedType::Unknown; previous_={}; hits_={}; }
  // Three consecutive complete scans with the same unique chip on at least
  // one choice square. Chip values only establish stability, never model type.
  void observeSetup(const RawBoard& board) {
    if(selected_!=LedType::Unknown) return;
    const bool d=board[19]!=PieceId{} || board[43]!=PieceId{};
    const bool e=board[20]!=PieceId{} || board[44]!=PieceId{};
    if(d==e) {resetSetupWindow();return;}
    const LedType next=d?LedType::Mono:LedType::Rgb;
    if(next!=candidate_)resetSetupWindow();
    candidate_=next;
    for(size_t i=0;i<2;++i) {
      const size_t square=(d?19:20)+24*i;
      const auto& chip=board[square];
      bool unique=chip!=PieceId{};
      if(unique)for(size_t j=0;j<64;++j)if(j!=square && board[j]==chip)unique=false;
      if(!unique) {hits_[i]=0;previous_[i]={};continue;}
      if(previous_[i]!=chip)hits_[i]=0;
      previous_[i]=chip;
      if(hits_[i]<3)++hits_[i];
      if(hits_[i]>=3)selected_=candidate_;
    }
  }
  bool conflictingSetup(const RawBoard& board) const {
    if(selected_==LedType::Unknown)return false;
    const size_t opposite=selected_==LedType::Mono?20:19;
    return board[opposite]!=PieceId{} || board[opposite+24]!=PieceId{};
  }
  void observe(const Calibration& calibration) {
    if(selected_==LedType::Unknown && calibration.ready() && calibration.count()==34)
      selected_=calibration.setupType();
  }
  LedType type() const { return selected_; }
  bool spectrum() const { return selected_==LedType::Rgb; }
 private:
  LedType selected_=LedType::Unknown;
  LedType candidate_=LedType::Unknown;
  std::array<PieceId,2> previous_{};
  std::array<uint8_t,2> hits_{};
};
}
