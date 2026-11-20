#pragma once
#include <cstdint>
void xmasStatus(const uint8_t frame[67]);
// Poll on Arduino loop. Returns true while local play owns the board.
bool xmasPoll(bool standalone,bool masqueradeReady,bool online,bool external,uint32_t now);
bool xmasOwnsBoard();
bool xmasLedOverride(uint64_t& mask);
