#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
namespace certabo {
class MoveLights {
 public:
  void reset() { count_=0; suppressed_=0; deferred_=0; haveObserved_=false; haveLift_=false; }
  void add(const uint8_t* squares,size_t n,const char board[64]) {
    uint64_t incoming=0;for(size_t i=0;i<n;++i) incoming|=uint64_t(1)<<squares[i];
    if(!incoming) return;
    if(suppressed_ && !(incoming & ~suppressed_)) { deferred_=incoming; return; }
    // A capture can reuse the previous move's destination. Phoenix may send
    // that destination before the new source; retain this suppressed phase.
    uint8_t captureTarget=64;
    if(suppressed_ && deferred_ && !(deferred_ & (deferred_-1)) && n==1) {
      for(uint8_t s=0;s<64;++s) if(deferred_ & (uint64_t(1)<<s)) {
        char target=board[s],source=board[squares[0]];
        if(target!='.' && source!='.' &&
           ((target>='A'&&target<='Z')!=(source>='A'&&source<='Z'))) captureTarget=s;
      }
    }
    suppressed_=0;
    deferred_=0;
    for(size_t i=0;i<n;++i) {
      uint8_t s=squares[i];bool known=false;
      for(size_t j=0;j<count_;++j) if(squares_[j]==s) known=true;
      if(known) continue;
      if(count_==2) {
        // Drop the oldest phase, not both: a stale previous destination
        // followed by the new source/target must converge without losing source.
        squares_[0]=squares_[1];
        memcpy(baseline_,board,64);
        before_[0]=baseline_[squares_[0]];count_=1;
      }
      if(count_==0) {
        memcpy(baseline_,board,64);
        // Human moves reach us as a board update before Phoenix's LED echo.
        if(haveLift_ && board[s]!=lift_[s]) memcpy(baseline_,lift_,64);
      }
      squares_[count_]=s;before_[count_]=baseline_[s];++count_;
    }
    if(captureTarget<64 && count_==1 && squares_[0]!=captureTarget) {
      squares_[1]=captureTarget;before_[1]=baseline_[captureTarget];count_=2;
    }
  }
  bool observe(const char board[64]) {
    if(haveObserved_) {
      for(size_t i=0;i<64;++i) if(observed_[i]!='.' && board[i]=='.') {
        memcpy(lift_,observed_,64);haveLift_=true;break;
      }
    }
    memcpy(observed_,board,64);haveObserved_=true;
    // Do not wait for the delayed second blink phase to recognise completion.
    if(count_==1 && before_[0]!='.' && board[squares_[0]]=='.') {
      int destination=-1;
      for(int i=0;i<64;++i) if(board[i]!=baseline_[i] && board[i]==before_[0]) {
        if(destination>=0) {destination=-1;break;}
        destination=i;
      }
      if(destination>=0) {
        squares_[1]=static_cast<uint8_t>(destination);before_[1]=baseline_[destination];count_=2;
      }
    }
    if(count_!=2)return false;
    for(int a=0;a<2;++a) {
      int b=1-a;char from=before_[a],to=board[squares_[b]];
      if(from!='.' && board[squares_[a]]=='.' && to!='.' && to!=before_[b] &&
         ((from>='A'&&from<='Z')==(to>='A'&&to<='Z'))) {
        suppressed_=bits();deferred_=0;count_=0;return true;
      }
    }
    return false;
  }
  uint64_t bits() const {uint64_t result=0;for(size_t i=0;i<count_;++i)result|=uint64_t(1)<<squares_[i];return result;}
  bool isSuppressed() const { return suppressed_ != 0; }
 private:
  uint8_t squares_[2]{};char before_[2]{},baseline_[64]{};size_t count_=0;uint64_t suppressed_=0,deferred_=0;
  char observed_[64]{},lift_[64]{};bool haveObserved_=false,haveLift_=false;
};
}
