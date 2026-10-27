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
int main(int argc,char** argv) {
  certabo::LedIdentity id;
  assert(id.type()==LedType::Unknown);
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
  certabo::RawBoard rgb{};
  certabo::Calibration notLearned;
  id.observe(notLearned); assert(id.type()==LedType::Unknown);
  id.reset(); assert(id.type()==LedType::Unknown);
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
  assert(rgbValues[0]==0 && rgbValues[1]==0 && rgbValues[2]==204);
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
  puts("PASS: LED encoding, blue80 settings, legacy quiet startup, settings gestures, stale scan guard and reserved RGB bytes");
}
