#pragma once
#include "certabo_protocol.h"
#include <cstring>

namespace certabo {
// Poll on the loop thread; advance flashes only after an LED command was sent.
class Startup {
 public:
  void reset() { phase_ = 0; waiting_ = true; timed_=false; haveLastBoard_=false; memset(mismatchCount_,0,sizeof(mismatchCount_)); }
  void learned() { phase_ = 1; waiting_ = true; timed_=false; }
  bool active() const { return waiting_; }
  void sent(uint32_t now) { if (phase_ && phase_ <= 6 && !timed_) { at_=now; timed_=true; } }
  void mask(uint32_t now, const RawBoard& board, uint8_t out[8], const Calibration* calibration=nullptr) {
    if (phase_ && phase_ <= 6 && timed_ && uint32_t(now-at_)>=700) { ++phase_; timed_=false; }
    if (phase_==0) {
      // Debounce: a square must look mismatched on two consecutive DISTINCT
      // scans before it lights up. A single mid-placement scan can misread
      // a piece ID (duplicate/transient), causing a brief stray flash;
      // requiring persistence across a real scan change (not just another
      // poll of the same unchanged board) filters that out while still
      // clearing instantly once a square is placed correctly.
      const bool isNewScan = !haveLastBoard_ || board != lastBoard_;
      if (isNewScan) { lastBoard_ = board; haveLastBoard_ = true; }
      uint8_t squares[64];size_t count=0;
      for(uint8_t i=0;i<64;++i) {
        const bool expected=i<16 || i>=48 || i==19 || i==43;
        const bool occupied=board[i]!=PieceId{};
        bool duplicate=false;
        if(occupied) for(uint8_t j=0;j<64;++j)
          if(i!=j && board[i]==board[j]) duplicate=true;
        const bool mismatch = expected!=occupied || duplicate;
        if (isNewScan) {
          if(mismatch) { if(mismatchCount_[i]<3) ++mismatchCount_[i]; }
          else mismatchCount_[i]=0;
        }
        if(mismatchCount_[i]>=2) squares[count++]=i^56;
      }
      ledMask(squares,count,out);
    } else if (phase_<=6) {
      const uint8_t center[]={27,28,35,36}; ledMask(center,phase_%2 ? 4 : 0,out);
    } else {
      constexpr char start[]="rnbqkbnrpppppppp................................PPPPPPPPRNBQKBNR";
      uint8_t squares[64]; size_t count=0;
      for(uint8_t i=0;i<64;++i) {
        bool mismatch=(board[i]!=PieceId{})!=(start[i]!='.');
        if(calibration) {
          RawBoard single{};single[i]=board[i];uint8_t frame[67]{};
          mismatch=!calibration->decode(single,frame) || frame[64-i]!=start[i];
        }
        if(mismatch) squares[count++]=i^56;
      }
      ledMask(squares,count,out);
    }
  }
  bool finish(const uint8_t frame[67]) {
    constexpr char start[]="rnbqkbnrpppppppp................................PPPPPPPPRNBQKBNR";
    if(phase_<=6) return false;
    for(size_t i=0;i<64;++i) if(frame[64-i]!=start[i]) return false;
    waiting_=false; return true;
  }
 private:
  unsigned phase_=0;
  bool waiting_=true, timed_=false;
  uint32_t at_=0;
  bool haveLastBoard_=false;
  RawBoard lastBoard_{};
  uint8_t mismatchCount_[64]{};
};
}
