#include "certabo_castling.h"
#include "certabo_king_filter.h"
#include <cassert>
#include <cstring>
#include <set>
#include <string>
#include <vector>

// Offline reproduction of the King's LED corner grid (h..a reversed 9x9, shared
// corners OR together) and a faithful copy of decodeKingLedFrame()'s corner test,
// so the castling frames can be run through the same decode + ghost filter that
// board_driver.cpp applies on the King one-shot path.
struct Lit { int square; char role; bool uniform; };
static std::vector<Lit> decode(const uint8_t g[81]) {
  std::vector<Lit> out;
  for (int file = 0; file < 8; ++file)
    for (int rt = 0; rt < 8; ++rt) {
      const uint8_t c[4] = {g[file*9+rt], g[(file+1)*9+rt], g[file*9+rt+1], g[(file+1)*9+rt+1]};
      if (!c[0] || !c[1] || !c[2] || !c[3]) continue;
      const bool src = (c[0]&0x0F)&&(c[1]&0x0F)&&(c[2]&0x0F)&&(c[3]&0x0F);
      const bool dst = (c[0]&0xF0)&&(c[1]&0xF0)&&(c[2]&0xF0)&&(c[3]&0xF0);
      if (!src && !dst) continue;
      const int f = 7 - file;
      out.push_back({rt*8+f, src&&!dst ? 'S' : (dst&&!src ? 'D' : 'G'),
                     c[0]==c[1] && c[0]==c[2] && c[0]==c[3]});
    }
  return out;
}
static void light(uint8_t g[81], int square, uint8_t value) {
  const int file = 7 - square % 8, rt = square / 8;
  g[file*9+rt] |= value; g[(file+1)*9+rt] |= value;
  g[file*9+rt+1] |= value; g[(file+1)*9+rt+1] |= value;
}
// What certaboSetOneShotLedSquares() finally receives for one frame.
static uint64_t shown(const std::vector<std::pair<int,uint8_t>>& squares, const char board[64]) {
  uint8_t g[81] = {};
  for (auto& s : squares) light(g, s.first, s.second);
  const std::vector<Lit> lit = decode(g);
  uint64_t fields = 0;
  for (auto& l : lit) fields |= uint64_t(1) << l.square;
  const uint64_t castle = certabo::castlingMask(fields, board);
  if (castle) return castle;
  uint64_t kept = 0;
  for (auto& l : lit)
    if (!(l.role == 'G' && certabo::kingGhost(l.square, fields, l.uniform, board)))
      kept |= uint64_t(1) << l.square;
  return kept;
}
static uint64_t bits(std::initializer_list<int> l) { uint64_t m = 0; for (int s : l) m |= uint64_t(1) << s; return m; }

int main() {
  const int a1=0,c1=2,d1=3,e1=4,f1=5,g1=6,h1=7, a8=56,b8=57,c8=58,d8=59,e8=60,f8=61,g8=62,h8=63;
  const uint8_t S=0x0F, D=0xF0, G=0xFF;
  char board[64]; memset(board,'.',64);
  board[a1]='R'; board[e1]='K'; board[h1]='R'; board[a8]='r'; board[e8]='k'; board[h8]='r';
  const uint64_t blackLong = bits({a8,c8,d8,e8}), blackShort = bits({e8,g8,h8,f8});
  const uint64_t whiteLong = bits({a1,c1,d1,e1});

  // Black O-O-O in every plausible King encoding: exactly the four squares, never b8.
  assert(shown({{a8,S},{e8,S},{c8,D},{d8,D}}, board) == blackLong);   // pure roles
  assert(shown({{a8,G},{e8,G},{c8,G},{d8,G}}, board) == blackLong);   // all generic: b8 ghost
  assert(shown({{e8,S},{c8,D},{a8,G},{d8,G}}, board) == blackLong);   // king roles, rook generic
  assert(shown({{e8,G},{c8,G},{a8,S},{d8,D}}, board) == blackLong);   // king generic, rook roles
  assert(shown({{e8,S},{c8,D},{a8,D},{d8,S}}, board) == blackLong);   // swapped rook roles
  // White O-O-O (b1 ghost) and both short castlings (f1/f8 must never be dropped).
  assert(shown({{a1,G},{e1,G},{c1,G},{d1,G}}, board) == whiteLong);
  assert(shown({{e8,G},{g8,G},{h8,G},{f8,G}}, board) == blackShort);
  assert(shown({{e8,S},{g8,D},{h8,S},{f8,D}}, board) == blackShort);

  // Not castling: a lone king step or rook step is shown exactly as the King sent it.
  assert(shown({{e8,S},{c8,D}}, board) == bits({e8,c8}));
  char afterKing[64]; memcpy(afterKing,board,64); afterKing[e8]='.'; afterKing[c8]='k';
  assert(shown({{a8,S},{d8,D}}, afterKing) == bits({a8,d8}));
  // Same four squares but the board does not allow castling: untouched by the rule.
  char blocked[64]; memcpy(blocked,board,64); blocked[b8]='n';
  assert(certabo::castlingMask(bits({a8,b8,c8,d8,e8}), blocked) == 0);
  char moved[64]; memcpy(moved,board,64); moved[e8]='.';
  assert(certabo::castlingMask(bits({a8,c8,d8,e8}), moved) == 0);
  // Ordinary suggestions on the back rank are unaffected.
  assert(certabo::castlingMask(bits({e8,d8}), board) == 0);
  assert(certabo::castlingMask(bits({a8,e8}), board) == 0);
  assert(certabo::castlingMask(bits({d8,e8,f8}), board) == 0);   // only 2 castling squares (king + f8 is not rook/kingTo)
  assert(certabo::castlingMask(0, board) == 0);
  assert(certabo::castlingMask(blackLong, nullptr) == 0);
}
