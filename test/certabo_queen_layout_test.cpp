#include "certabo_protocol.h"
#include "certabo_led_identity.h"
#include "certabo_startup.h"
#include "certabo_spectrum_leds.h"
#include "certabo_spectrum_settings.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>

using namespace certabo;
const char* opening="rnbqkbnrpppppppp................................PPPPPPPPRNBQKBNR";
RawBoard setup(bool spectrum, uint8_t prefix=93) {
  RawBoard b{};
  for(size_t i=0;i<64;++i)if(opening[i]!='.')b[i]={{prefix,0,21,uint8_t(i),77}};
  // The EXACT same two extra chips are used in both setup layouts.
  b[spectrum?20:19]={{prefix,0,21,90,77}};
  b[spectrum?44:43]={{prefix,0,21,91,77}};
  return b;
}
std::string wire(const RawBoard& b) {
  std::string s=":";
  for(const auto& id:b)for(auto byte:id)s+=std::to_string(byte)+" ";
  return s+"\r\n";
}
void cannotLearn(const RawBoard& b) {
  Calibration c;LedIdentity id;
  for(int i=0;i<14;++i) {assert(!c.observe(b,true,true));id.observe(c);assert(id.type()==LedType::Unknown);}
  assert(!c.ready());
}
void checkStartup(Calibration& c,RawBoard b,bool rgb) {
  Startup startup;startup.reset();startup.learned();
  uint8_t mask[8]{},expected[8]{},frame[67]{},command[247]{};
  const uint8_t center[]={27,28,35,36};
  for(unsigned phase=0;phase<6;++phase) {
    const uint32_t now=phase*700;
    startup.mask(now,b,mask,&c,false);
    ledMask(center,phase%2==0?4:0,expected);
    assert(memcmp(mask,expected,8)==0);
    if(rgb) {
      spectrum::encode(mask,command,0,0,204);
      unsigned lit=0;
      for(int i=0;i<81;++i) {
        assert(command[2+3*i]==0 && command[3+3*i]==0);
        assert(command[4+3*i]==0 || command[4+3*i]==204);
        if(command[4+3*i])++lit;
      }
      assert(lit==(phase%2==0?9u:0u));
    }
    startup.sent(now); // each phase advances only after a successful write
    assert(c.decode(b,frame) && !startup.finish(frame));
  }
  startup.mask(4200,b,mask,&c,false);
  const uint8_t queens[]={uint8_t(rgb?20:19),uint8_t(rgb?44:43)}; // a1..h8
  ledMask(queens,2,expected);assert(memcmp(mask,expected,8)==0);
  b[rgb?44:43]={};
  assert(c.decode(b,frame) && !startup.finish(frame));
  startup.mask(4300,b,mask,&c,false);
  ledMask(queens+1,1,expected);assert(memcmp(mask,expected,8)==0);
  b[rgb?20:19]={};
  assert(c.decode(b,frame) && startup.finish(frame));
  startup.mask(4400,b,mask,&c,false);
  for(auto byte:mask)assert(byte==0);
  b[32]={{7,7,7,7,7}};assert(!c.decode(b,frame)); // unknown is never empty
}
int main() {
  for(bool rgb:{false,true})for(uint8_t prefix:{uint8_t(2),uint8_t(14),uint8_t(93),uint8_t(94)})
    for(size_t payload:{size_t(1),size_t(20),size_t(64),size_t(253)}) {
      Calibration c;LedIdentity id;Parser parser;
      const auto b=setup(rgb,prefix);
      // Deliberately contradictory protocol markers; layout is the user choice.
      const std::string message=std::string(rgb?"L\r\n":"D\r\n")+wire(b);
      for(int scan=0;scan<7;++scan) {
        unsigned complete=0;
        for(size_t offset=0;offset<message.size();offset+=payload)
          for(size_t j=0;j<std::min(payload,message.size()-offset);++j)
            if(parser.feed(uint8_t(message[offset+j]))==ParseResult::Position) {
              ++complete;c.observe(parser.board(),true,true);id.observe(c);
            }
        assert(complete==1);
        if(scan<6)assert(!c.ready() && id.type()==LedType::Unknown);
      }
      assert(c.count()==34 && id.type()==(rgb?LedType::Rgb:LedType::Mono));
      checkStartup(c,b,rgb);
      // Changing layout mid-session must never change an already chosen driver.
      for(int i=0;i<9;++i){c.observe(setup(!rgb,prefix),true,true);id.observe(c);}
      assert(id.type()==(rgb?LedType::Rgb:LedType::Mono));
      uint8_t archive[Calibration::kBlobSize];assert(c.save(archive));
      Calibration archived;assert(archived.load(archive,sizeof(archive)));
      LedIdentity fresh;fresh.observe(archived);assert(fresh.type()==LedType::Unknown);
      c.reset();id.reset();assert(!c.ready() && id.type()==LedType::Unknown);
      for(int i=0;i<7;++i){c.observe(setup(!rgb,prefix),true,true);id.observe(c);}
      assert(c.ready() && id.type()==(!rgb?LedType::Rgb:LedType::Mono));
    }
  auto mixed=setup(false);mixed[20]=mixed[19];mixed[19]={};cannotLearn(mixed);
  auto missing=setup(true);missing[44]={};cannotLearn(missing);
  auto duplicate=setup(false);duplicate[43]=duplicate[19];cannotLearn(duplicate);
  auto extra=setup(false);extra[20]={{1,2,3,4,5}};cannotLearn(extra);
  auto wrong=setup(true);wrong[32]=wrong[0];wrong[0]={};cannotLearn(wrong);
  auto empty=setup(false);empty[19]={};empty[43]={};cannotLearn(empty);
  // Three classic, three Spectrum and one mixed scan cannot select a model.
  Calibration ambiguous;
  for(int i=0;i<3;++i)assert(!ambiguous.observe(setup(false),true,true));
  for(int i=0;i<3;++i)assert(!ambiguous.observe(setup(true),true,true));
  assert(!ambiguous.observe(mixed,true,true) && !ambiguous.ready());
  // A transient bad chip is handled by the existing seven-frame majority.
  Calibration noisy;
  for(int i=0;i<6;++i)assert(!noisy.observe(i==2?duplicate:setup(false),true,true));
  assert(noisy.observe(setup(false),true,true) && noisy.count()==34);
  spectrum::Settings settings;uint8_t color[3];settings.rgb(color);
  assert(color[0]==0 && color[1]==0 && color[2]==204);
  puts("PASS: d3/d6 mono, e3/e6 RGB, same chips, all fragmentation, 34-ID gate, blue80 blink, queen removal, rejection, archive isolation and reconnect");
}
