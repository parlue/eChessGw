#include "certabo_led_identity.h"
#include "certabo_spectrum_leds.h"
#include "certabo_startup.h"
#include "certabo_spectrum_settings.h"
#include <algorithm>
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
  (void)argc; (void)argv;
  // Identical figures on either bridge: neither 2,0 nor 93/94 nor any other
  // prefix is consulted. Exercise empty, mixed and arbitrary raw IDs as well.
  certabo::RawBoard rgb{};
  for(int prefix : {0,2,7,93,94,128,255}) {
    for(size_t i=0;i<64;++i) rgb[i]={{static_cast<uint8_t>(prefix),0,1,0,static_cast<uint8_t>(i)}};
    for(size_t length : {size_t(64),size_t(253)}) {
      id.reset(); feed(id,"L\r\n");
      certabo::RawBoard replay{};
      for(int n=0;n<3;++n) {
        id.notification(length,':'); id.observe(rgb);
        if(n<2) { assert(id.type()==LedType::Unknown); assert(!id.pop(replay)); }
      }
      assert(id.type()==(length==64 ? LedType::Rgb : LedType::Mono));
      assert(id.buffered()==3);
      for(int n=0;n<3;++n) { assert(id.pop(replay)); assert(replay==rgb); }
      assert(!id.pop(replay));
      id.streamGap(); assert(id.type()==LedType::Unknown && !id.positionSeen());
      assert(!id.pop(replay));
    }
  }
  // A first stream must not be mistaken for three internal queue chunks.
  id.reset(); id.notification(253,':');
  for(int i=0;i<3;++i) { id.notification(0,':'); id.observe(rgb); }
  assert(id.type()==LedType::Unknown);
  // Unknown profiles stay dark; the bounded buffer retains the last 3 frames.
  id.reset();
  for(int i=0;i<10;++i) { rgb[0][4]=static_cast<uint8_t>(i); id.notification(20,':'); id.observe(rgb); }
  assert(id.buffered()==3 && id.type()==LedType::Unknown);
  feed(id,"D\r\n"); certabo::RawBoard replay{};
  for(int i=7;i<10;++i) { assert(id.pop(replay)); assert(replay[0][4]==i); }
  assert(!id.pop(replay));
  // Standalone D is RGB evidence; L may be emitted by Spectrum as well.
  id.reset(); feed(id,"L\r\nD\r\nL\r\n");
  id.observe(rgb); assert(id.spectrum());
  id.notification(253,':'); assert(id.conflict() && id.type()==LedType::Unknown);
  assert(!id.pop(replay));
  // Mixed transport profiles fail closed, regardless of marker or IDs.
  id.reset(); id.notification(64,':'); id.notification(253,':');
  for(int i=0;i<3;++i) id.observe(rgb);
  assert(id.conflict() && id.type()==LedType::Unknown);
  id.reset(); for(int i=0;i<4;++i) {id.notification(64,'2'); id.observe(rgb);}
  assert(id.type()==LedType::Unknown);
  // Exercise the real decimal stream parser and actual original notification
  // boundaries, using exactly the same piece bytes for both bridge profiles.
  std::string wire=":";
  for(const auto& piece:rgb) for(auto value:piece) wire+=std::to_string(value)+" ";
  wire+="\r\n";
  for(size_t payload : {size_t(64),size_t(253)}) {
    id.reset(); certabo::Parser parser; size_t delivered=0;
    for(int scan=0;scan<4;++scan) {
      for(size_t offset=0;offset<wire.size();offset+=payload) {
        const size_t n=std::min(payload,wire.size()-offset);
        id.notification(n,static_cast<uint8_t>(wire[offset]));
        for(size_t j=0;j<n;++j) {
          if(parser.feed(static_cast<uint8_t>(wire[offset+j]))==certabo::ParseResult::Position) {
            id.observe(parser.board());
            if(scan<2) assert(id.type()==LedType::Unknown);
            while(id.pop(replay)) { assert(replay==rgb); ++delivered; }
          }
        }
      }
    }
    assert(delivered==4);
    assert(id.type()==(payload==64 ? LedType::Rgb : LedType::Mono));
  }
  id.reset();id.observe(rgb);id.discardBuffered();feed(id,"D\r\n");
  assert(!id.pop(replay)); // malformed frames cannot replay stale samples
  puts("PASS: ID-independent bridge profiles, initial-frame replay, bounds, gaps, ambiguous streams and reconnect");
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
  puts("PASS: quiet startup, settings gestures, stale scan guard and reserved RGB bytes");
}
