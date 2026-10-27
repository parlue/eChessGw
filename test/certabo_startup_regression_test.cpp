#include "certabo_protocol.h"
#include "certabo_led_identity.h"
#include <algorithm>
#include <cstdio>
#include <string>

int main() {
  const char* start="rnbqkbnrpppppppp................................PPPPPPPPRNBQKBNR";
  certabo::RawBoard board{};
  for(size_t i=0;i<64;++i)
    if(start[i]!='.' || i==19 || i==43)
      board[i]={{93,0,21,static_cast<uint8_t>(i),42}};
  std::string frame=":";
  for(const auto& piece:board)for(auto byte:piece)frame+=std::to_string(byte)+" ";
  frame+="\r\n";
  certabo::Parser parser;
  certabo::LedIdentity identity;
  certabo::Calibration baseline, current;

  size_t parsed=0,delivered=0;
  // Valid, complete records fragmented into small original BLE notifications.
  // The same records calibrated in V11; negotiated MTU need not equal payload.
  for(int scan=0;scan<10;++scan) {
    for(size_t offset=0;offset<frame.size();offset+=20) {
      const size_t n=std::min(size_t(20),frame.size()-offset);

      for(size_t i=0;i<n;++i) {
        const auto byte=static_cast<uint8_t>(frame[offset+i]);

        if(parser.feed(byte)==certabo::ParseResult::Position) {
          ++parsed;
          baseline.observe(parser.board(),true);
          ++delivered;
          current.observe(parser.board(),true,true);
          identity.observe(current);
        }
      }
    }
  }
  std::printf("valid_frames=%u V11_calibrated=%u beta_delivered=%u beta_calibrated=%u type_unknown=%u\n",
      unsigned(parsed),baseline.ready(),unsigned(delivered),current.ready(),
      identity.type()==certabo::LedType::Unknown);
  if(parsed!=10 || !baseline.ready() || delivered!=10 || !current.ready() || identity.type()!=certabo::LedType::Mono)return 1;
  return 0;
}
