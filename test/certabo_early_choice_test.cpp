#include "certabo_led_identity.h"
#include "certabo_startup.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace certabo;
RawBoard setup(bool rgb) {
 RawBoard b{}; for(int i=0;i<64;++i)if(i<16||i>=48)b[i]={{93,0,0,0,uint8_t(i+1)}};
 b[rgb?20:19]={{93,0,0,0,90}};b[rgb?44:43]={{93,0,0,0,91}};return b;
}
bool lit(const uint8_t mask[8],int raw) {uint8_t expected[8];uint8_t square=uint8_t(raw^56);ledMask(&square,1,expected);for(int i=0;i<8;++i)if(mask[i]&expected[i])return true;return false;}
int main(){
 for(bool rgb:{false,true})for(int anchor=0;anchor<2;++anchor){
  auto b=setup(rgb);int black=rgb?20:19,white=black+24;
  b[anchor?black:white]={};b[0]={}; // only ONE extra queen, plus a missing rook
  LedIdentity id;Startup start;start.reset();Calibration c;uint8_t mask[8]{};
  for(int i=0;i<2;++i){id.observeSetup(b);assert(id.type()==LedType::Unknown);}
  id.observeSetup(b);assert(id.type()==(rgb?LedType::Rgb:LedType::Mono));
  assert(!c.observe(b,true,true));
  start.observeSetup(b,id.type());
  for(int i=0;i<50;++i)start.mask(i,b,mask);
  for(auto v:mask)assert(v==0); // polling must NEVER fabricate scan persistence
  start.observeSetup(b,id.type());start.mask(50,b,mask);
  assert(lit(mask,0)&&lit(mask,anchor?black:white)); // repeated identical scans count
  auto good=setup(rgb);start.observeSetup(good,id.type());start.mask(51,good,mask);
  for(auto v:mask)assert(v==0); // correction clears immediately
  auto noisy=good;noisy[32]=good[1];
  start.observeSetup(noisy,id.type());start.mask(52,noisy,mask);for(auto v:mask)assert(v==0);
  start.observeSetup(good,id.type());start.mask(53,good,mask);for(auto v:mask)assert(v==0);
  start.observeSetup(noisy,id.type());start.observeSetup(noisy,id.type());start.mask(54,noisy,mask);assert(lit(mask,1)&&lit(mask,32));
  start.resetSetupWindow();start.mask(55,noisy,mask);for(auto v:mask)assert(v==0);
  auto opposite=setup(!rgb);for(int i=0;i<10;++i)id.observeSetup(opposite);
  assert(id.type()==(rgb?LedType::Rgb:LedType::Mono)&&id.conflictingSetup(opposite));
  id.resetSetupWindow();assert(id.type()==(rgb?LedType::Rgb:LedType::Mono));
  c.reset();for(int i=0;i<6;++i)assert(!c.observe(good,true,true));assert(c.observe(good,true,true));assert(c.count()==34);
  start.learned();start.mask(100,good,mask,&c);assert(lit(mask,27)&&lit(mask,28)&&lit(mask,35)&&lit(mask,36));
  id.reset();for(int i=0;i<3;++i)id.observeSetup(opposite);assert(id.type()==(!rgb?LedType::Rgb:LedType::Mono));
 }
 LedIdentity id;auto b=setup(false);b[43]={};
 for(int i=0;i<9;++i){b[19][4]=uint8_t(90+i);id.observeSetup(b);}assert(id.type()==LedType::Unknown);
 id.reset();b=setup(false);b[43]={};b[19]=b[0];for(int i=0;i<9;++i)id.observeSetup(b);assert(id.type()==LedType::Unknown);
 id.reset();b=setup(false);b[20]={{1,2,3,4,5}};for(int i=0;i<9;++i)id.observeSetup(b);assert(id.type()==LedType::Unknown);
 id.reset();b=setup(false);id.observeSetup(b);id.observeSetup(b);id.resetSetupWindow();id.observeSetup(b);assert(id.type()==LedType::Unknown);
 puts("PASS: early driver choice from either single queen, debounce, scan-only mismatch votes, correction, duplicate/noise rejection, 34-ID gate, reconnect");
}
