#pragma once
#include <stdint.h>
namespace certabo {
// Castling on a King's LED frame cannot be told apart from its own corner
// overlap by the corner data alone. The King lights four squares (king and rook,
// from and to); queenside they span a1/a8 .. e1/e8, and the four lit squares
// light the corner columns of the empty square between rook and king as well, so
// the decode reports b1/b8 as a fifth, ghost square (reproduced offline for every
// King encoding except pure source/destination roles, 2026-10-11 -- the user's
// "b8 leuchtet zu viel"). kingGhost() cannot remove it: it needs non-uniform
// corners, and a square that carries a role is never filtered at all. Worse, the
// same filter may drop the real rook destination (d1/d8, f1/f8) instead.
//
// The board knows what the frame can only mean: king on e1/e8, rook on its corner,
// the squares between them empty. If the decoded squares lie inside the run
// between rook and king, include the king and at least two other castling squares,
// the frame IS that castling and the four squares are exactly known.
// Square indices a1=0 .. h8=63; board is a1..h8 ('K','R' white, 'k','r' black).
// Returns the four squares as a mask (king from/to, rook from/to) or 0.
inline int popcount64(uint64_t v) { int n = 0; while (v) { v &= v - 1; ++n; } return n; }

inline uint64_t castlingMask(uint64_t lit, const char board[64]) {
  struct Side { int king, rook, kingTo, rookTo; char kingPiece, rookPiece; };
  static const Side sides[4] = {
      {4, 0, 2, 3, 'K', 'R'},     // white O-O-O: e1, a1 -> c1, d1
      {4, 7, 6, 5, 'K', 'R'},     // white O-O:   e1, h1 -> g1, f1
      {60, 56, 58, 59, 'k', 'r'}, // black O-O-O: e8, a8 -> c8, d8
      {60, 63, 62, 61, 'k', 'r'}, // black O-O:   e8, h8 -> g8, f8
  };
  if (!board) return 0;
  const uint64_t one = 1;
  for (const Side& s : sides) {
    const int lo = s.rook < s.king ? s.rook : s.king;
    const int hi = s.rook < s.king ? s.king : s.rook;
    uint64_t run = 0;
    for (int q = lo; q <= hi; ++q) run |= one << q;
    const uint64_t castle = (one << s.king) | (one << s.rook) | (one << s.kingTo) | (one << s.rookTo);
    if (!(lit & (one << s.king))) continue;        // the king's own square is always lit
    if (lit & ~run) continue;                      // something outside the back-rank run: not castling
    if (popcount64(lit & castle) < 3) continue;    // a lone king step / rook move is shown as is
    if (board[s.king] != s.kingPiece || board[s.rook] != s.rookPiece) continue;
    bool clear = true;
    for (int q = lo + 1; q < hi; ++q) if (board[q] != '.') clear = false;
    if (!clear) continue;
    return castle;
  }
  return 0;
}
}
