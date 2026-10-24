#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
namespace certabo { namespace spectrum {
// Same gesture positions as iChessOne, with Spectrum's 80% default retained.
class Settings {
 public:
  uint8_t color=0, brightness=204;
  void resetGesture() { candidate_=-1; applied_=false; }
  // a1..h8 piece array: only a full starting position + one extra white queen.
  bool observe(const char board[64], uint32_t now) {
    const int next=detect(board);
    if(next!=candidate_ || uint32_t(now-lastScan_)>1500) {candidate_=next;since_=now;applied_=false;}
    lastScan_=now;
    return next>=0;
  }
  bool tick(uint32_t now) {
    if(candidate_<0 || applied_ || uint32_t(now-lastScan_)>1500 || uint32_t(now-since_)<1000) return false;
    applied_=true;
    const int index=candidate_/8-2;
    if(candidate_%8==6)color=static_cast<uint8_t>(index);
    else {const uint8_t levels[]={64,128,191,255};brightness=levels[index];}
    return true;
  }
  void rgb(uint8_t out[3]) const {
    out[0]=out[1]=out[2]=0;
    if(color==1)out[2]=brightness;
    else if(color==2)out[1]=brightness;
    else {out[0]=brightness;if(color==3)out[1]=brightness/2;}
  }
  static int detect(const char b[64]) {
    constexpr char start[]="RNBQKBNRPPPPPPPP................................pppppppprnbqkbnr";
    int extra=-1;
    for(int i=0;i<64;++i) if(b[i]!=start[i]) {
      if(extra>=0 || start[i]!='.' || b[i]!='Q' || i/8<2 || i/8>5 || i%8<6) return -1;
      extra=i;
    }
    return extra;
  }
 private:
  int candidate_=-1;
  uint32_t since_=0,lastScan_=0;
  bool applied_=false;
};
}}
