#include "certabo_led_identity.h"
#include "certabo_spectrum_leds.h"
#include "certabo_spectrum_input.h"
#include "certabo_startup.h"
#include "certabo_spectrum_settings.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

using certabo::LedType;
void feed(certabo::LedIdentity& id,const std::string& s) {
  for (unsigned char c:s) id.feed(c);
}
int main(int argc,char** argv) {
  certabo::LedIdentity id;
  assert(id.type()==LedType::Unknown);
  feed(id,"DEBUG\r\nLED\r\n:1 D 0 0\r\n");
  assert(id.type()==LedType::Unknown);
  feed(id,"L"); assert(id.type()==LedType::Unknown);
  feed(id,"\r\n"); assert(id.type()==LedType::Mono);
  feed(id,":1 2 3\r\nL\r\n"); assert(id.type()==LedType::Mono);
  feed(id,"D\r\n"); assert(id.conflict() && id.type()==LedType::Unknown);
  feed(id,"L\r\n"); assert(id.type()==LedType::Unknown);
  id.reset(); feed(id,"D:"); assert(id.type()==LedType::Rgb);
  id.reset(); feed(id,"D"); id.resetPending(); feed(id,"\r\n");
  assert(id.type()==LedType::Unknown);
  const std::string sample=":1 2 3\r\nD\r\n:4 5 6\r\n";
  for(size_t split=0;split<=sample.size();++split) {
    id.reset(); feed(id,sample.substr(0,split)); feed(id,sample.substr(split));
    assert(id.type()==LedType::Rgb);
  }
  uint8_t mask[8]{}, out[certabo::spectrum::kCommandBytes+1];
  for(int square=0;square<64;++square) {
    memset(mask,0,8); mask[7-square/8]=uint8_t(1u<<(square%8));
    memset(out,0xa5,sizeof(out)); certabo::spectrum::encode(mask,out);
    assert(out[0]==255 && out[1]==85 && out[245]==13 && out[246]==10 && out[247]==0xa5);
    int lit=0;
    for(int led=0;led<81;++led) {
      assert(out[3+led*3]==0 && out[4+led*3]==0);
      if(out[2+led*3]) {assert(out[2+led*3]==204); ++lit;}
    }
    assert(lit==4);
    // Independently check the four named board corners from the PDF.
    if(square==0) for(int led:{8,9,17,18}) assert(out[2+(led-1)*3]==204);
    if(square==7) for(int led:{1,2,10,11}) assert(out[2+(led-1)*3]==204);
    if(square==56) for(int led:{71,72,80,81}) assert(out[2+(led-1)*3]==204);
    if(square==63) for(int led:{64,65,73,74}) assert(out[2+(led-1)*3]==204);
  }
  memset(mask,255,8); certabo::spectrum::encode(mask,out);
  for(int led=0;led<81;++led) assert(out[2+led*3]==204); // no summed brightness
  memset(mask,0,8); certabo::spectrum::encode(mask,out);
  for(int i=2;i<245;++i) assert(out[i]==0);
  if(argc>1) {
    std::ifstream f(argv[1],std::ios::binary); assert(f.good());
    id.reset(); char c; while(f.get(c)) id.feed(static_cast<uint8_t>(c));
    assert(id.type()==LedType::Mono && !id.conflict());
  }
  puts("PASS: Spectrum 247-byte red80 frames, all 64 squares, PDF corners, passive identity, reconnect/conflict and mono capture");
  certabo::RawBoard rgb{};
  for(size_t i=0;i<8;++i) rgb[i]={{2,0,1,0,static_cast<uint8_t>(i+1)}};
  id.reset();feed(id,"L\r\n");assert(!id.positionSeen());
  id.observe(rgb);assert(id.positionSeen() && id.spectrum() && id.type()==LedType::Rgb);
  feed(id,"D\r\nL\r\n");assert(id.type()==LedType::Rgb && !id.conflict());
  certabo::RawBoard mono=rgb;
  for(auto& p:mono) if(p!=certabo::PieceId{})p[0]=93;
  id.observe(mono);assert(id.spectrum()); // locked during connection
  id.reset();feed(id,"D\r\n");id.observe(mono);assert(id.type()==LedType::Mono && !id.spectrum());
  for(auto& p:mono) if(p!=certabo::PieceId{})p[0]=94;
  id.reset();id.observe(mono);assert(id.type()==LedType::Mono);
  auto mixed=rgb;mixed[8]={{93,0,0,1,2}};
  id.reset();feed(id,"L\r\n");id.observe(mixed);assert(id.type()==LedType::Unknown);
  auto sparse=rgb;sparse[7]={};id.observe(sparse);assert(id.type()==LedType::Unknown);
  id.observe(rgb);assert(id.spectrum());
  id.reset();assert(!id.spectrum() && !id.positionSeen() && id.type()==LedType::Unknown);
  certabo::spectrum::InputFilter filter;
  auto raw=rgb;raw[20]={{128,0,0,0,0}};
  assert(filter.apply(raw)[20]==certabo::PieceId{});
  raw[0]={{252,0,0,0,0}};
  for(int n=0;n<3;++n)assert(filter.apply(raw)[0]==rgb[0]);
  assert(filter.apply(raw)[0]==certabo::PieceId{});
  raw[0]=rgb[0];assert(filter.apply(raw)[0]==rgb[0]);
  raw[0]={};assert(filter.apply(raw)[0]==certabo::PieceId{}); // true empty immediately
  raw[0]=rgb[0];filter.apply(raw);filter.reset();raw[0]={{128,0,0,0,0}};
  assert(filter.apply(raw)[0]==certabo::PieceId{}); // reconnect cannot retain a tag
  raw[0]={{2,0,99,99,99}};assert(filter.apply(raw)[0]==raw[0]); // unknown tag stays unknown
  certabo::Startup quiet;quiet.learned();
  for(uint32_t t=0;t<4200;t+=700) {
    quiet.mask(t,rgb,mask,nullptr,true);
    for(auto b:mask)assert(b==0); // no send callback required to advance
  }
  quiet.mask(4200,rgb,mask,nullptr,true);
  uint8_t startFrame[67]{};
  const char* start="rnbqkbnrpppppppp................................PPPPPPPPRNBQKBNR";
  for(int i=0;i<64;++i)startFrame[64-i]=start[i];
  assert(quiet.finish(startFrame));
  puts("PASS: app tag detection overrides markers, mixed/sparse/reconnect, Spectrum noise hold and quiet startup");
  id.reset();certabo::RawBoard foreign{};
  for(int i=0;i<8;++i)foreign[i]={{7,0,0,0,static_cast<uint8_t>(i+1)}};
  for(int i=0;i<2;++i){id.notification(64,':');id.observe(foreign);assert(!id.spectrum());}
  id.notification(64,':');id.observe(foreign);assert(id.packetFallback() && !id.spectrumTags());
  id.observe(mono);assert(id.type()==LedType::Mono && !id.packetFallback());
  // Mono boards with unfamiliar tags must not become RGB due to packet size.
  id.reset();feed(id,"L\r\n");
  for(int i=0;i<8;++i){id.notification(64,':');id.observe(foreign);}
  assert(id.type()==LedType::Mono && !id.spectrum());
  // A late explicit marker also supersedes an earlier packet-only guess.
  id.reset();for(int i=0;i<3;++i){id.notification(64,':');id.observe(foreign);}
  assert(id.packetFallback());feed(id,"L\r\n");
  assert(id.type()==LedType::Mono && !id.spectrum());
  id.observe(rgb);assert(id.spectrumTags() && id.type()==LedType::Rgb);
  id.reset();feed(id,"L\r\nD\r\n");
  for(int i=0;i<3;++i){id.notification(64,':');id.observe(foreign);}
  assert(id.conflict() && id.type()==LedType::Unknown);
  id.reset();for(int i=0;i<4;++i){id.notification(128,':');id.observe(foreign);}assert(!id.spectrum());
  id.reset();for(int i=0;i<4;++i){id.notification(64,'2');id.observe(foreign);}assert(!id.spectrum());
  certabo::spectrum::Settings settings;uint8_t rgbValues[3];settings.rgb(rgbValues);
  assert(rgbValues[0]==204 && rgbValues[1]==0 && rgbValues[2]==0);
  char gesture[65]="RNBQKBNRPPPPPPPP................................pppppppprnbqkbnr";
  assert(!settings.observe(gesture,0));gesture[30]='Q'; // g4 blue
  assert(settings.observe(gesture,0));assert(!settings.tick(999));
  assert(settings.observe(gesture,1000));assert(settings.tick(1000));assert(!settings.tick(1001));
  settings.rgb(rgbValues);assert(rgbValues[2]==204 && rgbValues[0]==0);
  gesture[30]='.';gesture[47]='Q'; // h6 full brightness
  settings.observe(gesture,2000);settings.observe(gesture,3000);assert(settings.tick(3000));
  settings.rgb(rgbValues);assert(rgbValues[2]==255);
  gesture[12]='.';assert(!settings.observe(gesture,4000));assert(!settings.tick(6000));
  gesture[12]='P';settings.observe(gesture,6000);assert(!settings.tick(8000)); // stale scan
  memset(mask,255,8);certabo::spectrum::encode(mask,out,255,85,255);
  for(int i=2;i<244;++i)assert(!(out[i]==255 && out[i+1]==85));
  assert(out[0]==255 && out[1]==85 && out[245]==13 && out[246]==10);
  puts("PASS: notification fallback with tag priority, settings gestures, stale scan guard and reserved RGB bytes");
}
