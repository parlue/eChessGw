#pragma once
#include <cstring>

// Input is the unchanged legal position before castling; stages 0..3
// reproduce lifting/placing the king, then lifting/placing the rook.
inline bool machineCastleStage(const char before[64], char out[64], int from, int to, int stage) {
  if (from < 0 || from >= 64 || to < 0 || to >= 64 || stage < 0 || stage > 3) return false;
  const char king = before[from];
  if ((king != 'K' && king != 'k') || from / 8 != to / 8 ||
      (to - from != 2 && from - to != 2)) return false;
  const int base = from / 8 * 8;
  const int rookFrom = base + (to > from ? 7 : 0);
  const int rookTo = base + (to > from ? 5 : 3);
  memcpy(out, before, 64);
  out[from] = '.';
  if (stage >= 1) out[to] = king;
  if (stage >= 2) out[rookFrom] = '.';
  if (stage >= 3) out[rookTo] = before[rookFrom];
  return true;
}
