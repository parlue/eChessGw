#include "certabo_king_echo.h"
#include <cassert>
int main() {
  auto bit=[](int s){return uint64_t(1)<<s;};
  const int e2=12,e4=28,c6=42,c7=50,f5=37,e6=44,e5=36,e3=20;
  certabo::MoveEcho echo;

  // First move after connecting: nothing was suggested beforehand, the King
  // replays e2/e4 ~140 ms after the scan.
  echo.arrived(bit(e2)|bit(e4),2,1000);
  assert(echo.isEcho(bit(e2)|bit(e4),1144));
  assert(echo.isEcho(bit(e4),1144));                        // subset is still the replay
  assert(!echo.isEcho(bit(c7)|bit(c6),1200));               // the real reply is never filtered
  assert(!echo.isEcho(bit(e2)|bit(e4)|bit(e3),1144));       // an extra square is not a pure replay
  assert(!echo.isEcho(0,1144));                             // empty frame is not an echo

  // Next move begins: the King's lift frame of the piece that just arrived
  // (e4) must show.
  echo.lifted();
  assert(!echo.isEcho(bit(e4),1500));

  // Safety net: a stale mask stops filtering.
  echo.arrived(bit(e2)|bit(e4),2,1000);
  assert(echo.isEcho(bit(e2)|bit(e4),1000+certabo::MoveEcho::kWindowMs-1));
  assert(!echo.isEcho(bit(e2)|bit(e4),1000+certabo::MoveEcho::kWindowMs));
  assert(echo.isEcho(bit(e2)|bit(e4),0xFFFFFFF0u+0u) == false);  // clock far ahead
  echo.arrived(bit(e2)|bit(e4),2,0xFFFFFF00u);                   // millis() wrap
  assert(echo.isEcho(bit(e2)|bit(e4),0x40u));

  // En passant (f5xe6, captured pawn on e5): King lights f5/e6 only.
  echo.arrived(bit(f5)|bit(e6)|bit(e5),3,2000);
  assert(echo.isEcho(bit(f5)|bit(e6),2100));

  // A single changed square or a whole-board setup is not one move.
  echo.arrived(bit(e4),1,3000);
  assert(!echo.isEcho(bit(e4),3100));
  echo.arrived(bit(e2)|bit(e4)|bit(c6)|bit(c7)|bit(f5),5,3000);
  assert(!echo.isEcho(bit(e2)|bit(e4),3100));

  echo.arrived(bit(e2)|bit(e4),2,4000);
  echo.reset();
  assert(!echo.isEcho(bit(e2)|bit(e4),4100));
}
