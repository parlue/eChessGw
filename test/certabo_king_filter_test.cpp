#include "certabo_king_filter.h"
#include <cassert>
#include <cstring>
int main() {
  char b[64]; memset(b,'.',64);
  auto bit=[](int s){return uint64_t(1)<<s;};
  b[28]='P'; // e2-e4 already played, echo is uniform FF
  assert(certabo::kingGhost(20,bit(12)|bit(20)|bit(28),true,b));
  b[20]='N';
  assert(!certabo::kingGhost(20,bit(12)|bit(20)|bit(28),true,b));
  memset(b,'.',64); b[36]='p'; // e7-e5
  assert(certabo::kingGhost(44,bit(36)|bit(44)|bit(52),true,b));
  assert(!certabo::kingGhost(5,bit(4)|bit(5)|bit(6),true,b)); // castling
  assert(certabo::kingGhost(5,bit(4)|bit(5)|bit(6),false,b)); // Chessnut mixed corners
  assert(!certabo::kingGhost(20,bit(12)|bit(20),false,b));
}
