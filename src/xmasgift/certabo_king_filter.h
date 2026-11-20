#pragma once
#include <stdint.h>
namespace certabo {
// Same geometric test as Chessnut, plus the observed uniform-FF pawn echo.
// The latter is deliberately limited to a double pawn move and an empty
// middle square; never discard a rook/king destination during castling.
inline bool kingGhost(uint8_t square, uint64_t fields, bool uniform,
                      const char board[64]) {
  const int file = square % 8, rank = square / 8;
  const bool vertical = rank > 0 && rank < 7 &&
      (fields & (uint64_t(1) << (square-8))) &&
      (fields & (uint64_t(1) << (square+8)));
  const bool horizontal = file > 0 && file < 7 &&
      (fields & (uint64_t(1) << (square-1))) &&
      (fields & (uint64_t(1) << (square+1)));
  if ((vertical || horizontal) && !uniform) return true;
  if (!vertical || !board || board[square] != '.') return false;
  return (rank == 2 && ((board[square-8]=='P' && board[square+8]=='.') ||
                        (board[square-8]=='.' && board[square+8]=='P'))) ||
         (rank == 5 && ((board[square-8]=='p' && board[square+8]=='.') ||
                        (board[square-8]=='.' && board[square+8]=='p')));
}
}
