#include "certabo_startup.h"
#include "certabo_led_identity.h"
#include "certabo_spectrum_leds.h"
#include "certabo_spectrum_settings.h"
#define certabo v11_certabo
#include "certabo_startup_v11.h"
#undef certabo
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

uint32_t clockMs=0;
uint32_t millis(){return clockMs;}
struct SerialStub {
  template<typename... A>void printf(const char*,A...){}
  void println(const char*){} void println(){}
} Serial;
struct ResetStub{void reset(){}} moveLights,moveEcho;
certabo::Calibration calibration;
v11_certabo::Calibration oldCalibration;
certabo::Parser parser;
certabo::LedIdentity ledIdentity;
certabo::spectrum::Settings spectrumSettings;
void loadSpectrumSettings(){assert(false && "Mono must not load RGB settings");}
struct StartupSwitch {
  bool old=false;
  certabo::Startup current;
  v11_certabo::Startup frozen;
  bool active(){return old?frozen.active():current.active();}
  void sent(uint32_t t){if(old)frozen.sent(t);else current.sent(t);}
  void reset(){current.reset();frozen.reset();}
  void learned(){if(old)frozen.learned();else current.learned();}
  void mask(const certabo::RawBoard& b,uint8_t* out){
    if(old)frozen.mask(clockMs,b,out,&oldCalibration);
    else current.mask(clockMs,b,out,&calibration,false);
  }
  void finish(const uint8_t* frame){if(old)frozen.finish(frame);else current.finish(frame);}
} startup;
std::atomic<bool> ready{true};
bool kingMode=false,havePosition=false,haveSentLeds=false,spectrumPreview=false;
bool kingPulseWritten=true;
uint32_t kingPulseUntil=0,kingPulseHoldMs=350,lastLedSentAt=0,spectrumPreviewAt=0;
uint8_t wantedLeds[8]{},kingPulse[8]{},sentLeds[8]{},ledCandidateHits[64]{};
uint32_t ledSeenAt[64]{};
uint64_t ledHints=0;
char lightBoard[64]{};
certabo::LedType sentLedType=certabo::LedType::Unknown;
struct Event {
  uint32_t time;std::vector<uint8_t> bytes;bool response,success;
  bool operator==(const Event& b)const{return time==b.time && bytes==b.bytes && response==b.response && success==b.success;}
};
std::vector<Event> events;
int failAt=-1;
struct Client {
  uint16_t mtu=256;bool connected=true;
  uint16_t getMTU(){return mtu;}
  void disconnect(){connected=false;}
} clientObject;
Client* client=&clientObject;
bool certaboIsConnected(){return ready.load() && client->connected;}
struct Writer {
  bool writeValue(const uint8_t* b,size_t n,bool response){
    assert(n==8 && !response);
    bool ok=int(events.size())!=failAt;
    events.push_back({clockMs,std::vector<uint8_t>(b,b+n),response,ok});return ok;
  }
} writerObject;
Writer* writer=&writerObject;
#include "actual_send_functions.h"

certabo::RawBoard initial(){
  const char* start="rnbqkbnrpppppppp................................PPPPPPPPRNBQKBNR";
  certabo::RawBoard b{};
  for(size_t i=0;i<64;++i)if(start[i]!='.' || i==19 || i==43)b[i]={{93,0,21,uint8_t(i),77}};
  return b;
}
std::vector<Event> run(bool old,bool king,uint16_t mtu,int fail){
  events.clear();failAt=fail;clockMs=0;ready.store(true);client->connected=true;client->mtu=mtu;
  startup.old=old;startup.reset();calibration.reset();oldCalibration.reset();ledIdentity.reset();
  auto b=initial();
  for(int i=0;i<7;++i){calibration.observe(b,true,true);oldCalibration.observe(b,true);}
  assert(calibration.ready() && oldCalibration.ready());ledIdentity.observe(calibration);
  assert(ledIdentity.type()==certabo::LedType::Mono);parser.leds=certabo::LedType::Mono;
  kingMode=king;havePosition=haveSentLeds=false;sentLedType=certabo::LedType::Unknown;
  spectrumPreview=false;kingPulseUntil=lastLedSentAt=0;kingPulseWritten=true;
  memset(wantedLeds,0,8);memset(sentLeds,0,8);memset(lightBoard,0,64);
  startup.learned();
  for(clockMs=0;clockMs<=5500 && certaboIsConnected();clockMs+=50){
    if(clockMs==4700)b[43]={};
    if(clockMs==4900)b[19]={};
    uint8_t frame[67];assert(calibration.decode(b,frame));startup.finish(frame);
    if(!startup.active()){
      havePosition=true;const char* normal="RNBQKBNRPPPPPPPP................................pppppppprnbqkbnr";
      memcpy(lightBoard,normal,64);
    }
    startup.mask(b,wantedLeds);
    if(old)sendV11();else sendCurrent();
  }
  return events;
}
int main(){
  for(bool king:{false,true})for(uint16_t mtu:{uint16_t(23),uint16_t(256)})for(int fail:{-1,2}){
    const auto old=run(true,king,mtu,fail),current=run(false,king,mtu,fail);
    assert(old==current);
    if(fail<0){
      const std::vector<uint8_t> center={0,0,0,0x18,0x18,0,0,0};
      const std::vector<uint8_t> queens={0,0,8,0,0,8,0,0};
      const std::vector<uint8_t> blackOnly={0,0,8,0,0,0,0,0},off(8,0);
      assert(current.front().bytes==center);
      bool both=false,black=false,clear=false;
      for(const auto& e:current){
        if(e.time>=4200 && e.bytes==queens)both=true;
        if(e.time>=4700 && e.bytes==blackOnly)black=true;
        if(e.time>=4900 && e.bytes==off)clear=true;
      }
      assert(both && black && clear);
    }
    std::printf("PASS identical V11/current mono writes: King=%u MTU=%u failAt=%d writes=%u\n",king,mtu,fail,unsigned(current.size()));
  }
}
