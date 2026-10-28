#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>

namespace certabo { namespace spectrum {
constexpr size_t kCommandBytes=247;
constexpr uint8_t kRed=204; // 80% of the 8-bit red channel, green/blue off.

// RGB_Leds_chessboard_commands.pdf: LED 1 at H1's outer corner, LED 9
// at A1's outer corner, LED 73 at H8 and LED 81 at A8. Each square lights
// its four surrounding corners; shared corners are written once, not added.
inline void encode(const uint8_t mask[8], uint8_t out[kCommandBytes], uint8_t red=kRed, uint8_t green=0, uint8_t blue=0) {
  memset(out,0,kCommandBytes);
  out[0]=255; out[1]=85; out[245]=13; out[246]=10;
  for (int row=0;row<8;++row) for (int file=0;file<8;++file) {
    if (!(mask[row] & (1u<<file))) continue;
    const int corner=(7-row)*9+(7-file);
    const int offsets[4]={0,1,9,10};
    for (int offset:offsets) {
      const int index=2+(corner+offset)*3;
      out[index]=red; out[index+1]=green; out[index+2]=blue;
    }
  }
  // Reserved start marker must not occur in RGB payload, including LED boundaries.
  for(size_t i=2;i<244;++i) if(out[i]==255 && out[i+1]==85) out[i]=254;
}
}}
