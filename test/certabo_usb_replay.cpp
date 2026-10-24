#include "certabo_protocol.h"
#include <fstream>
#include <cstdio>
#include <cassert>
int main(int argc, char** argv) {
  std::ifstream in(argc>1 ? argv[1] : "tools/certabo-usb-raw.bin", std::ios::binary);
  assert(in.good());
  certabo::Parser parser;
  certabo::Calibration calibration;
  unsigned frames=0;
  char chunk[64];
  while(in.read(chunk,sizeof(chunk)) || in.gcount()) {
    for(std::streamsize i=0;i<in.gcount();++i) {
      if(parser.feed(static_cast<uint8_t>(chunk[i]))!=certabo::ParseResult::Position) continue;
      ++frames;
      calibration.observe(parser.board(),true);
    }
  }
  printf("Parsed USB positions: %u; learned IDs: %u\n",frames,unsigned(calibration.count()));
  assert(frames>=7);
  if (!calibration.ready()) {
    assert(parser.board()[5]==certabo::PieceId{});
    puts("PASS: complete USB frames parsed; calibration correctly withheld because f8 is empty");
    return 0;
  }
  assert(calibration.count()==34);
  // Exercise special-move board states with the actual learned physical IDs.
  const auto initial=parser.board();
  auto check=[&](const certabo::RawBoard& board,const char* expected) {
    uint8_t frame[67];assert(calibration.decode(board,frame));
    for(unsigned i=0;i<64;++i) assert(frame[64-i]==expected[i]);
  };
  for(int black=0;black<2;++black) {
    const int row=black?0:56;
    for(int longCastle=0;longCastle<2;++longCastle) {
      certabo::RawBoard board{};char expected[65];
      for(int i=0;i<64;++i) expected[i]='.';expected[64]=0;
      board[row+4]=initial[row+4];board[row+(longCastle?0:7)]=initial[row+(longCastle?0:7)];
      expected[row+4]=black?'k':'K';expected[row+(longCastle?0:7)]=black?'r':'R';check(board,expected);
      board[row+4]={};expected[row+4]='.';check(board,expected);
      board[row+(longCastle?2:6)]=initial[row+4];expected[row+(longCastle?2:6)]=black?'k':'K';check(board,expected);
      board[row+(longCastle?0:7)]={};expected[row+(longCastle?0:7)]='.';check(board,expected);
      board[row+(longCastle?3:5)]=initial[row+(longCastle?0:7)];expected[row+(longCastle?3:5)]=black?'r':'R';check(board,expected);
    }
    for(int kind=0;kind<4;++kind) {
      certabo::RawBoard board{};char expected[65];
      for(int i=0;i<64;++i) expected[i]='.';expected[64]=0;
      int idSquare=kind==0?(black?19:43):row+(kind==1?0:kind==2?2:1);
      int target=black?56:0;
      board[target]=initial[idSquare];expected[target]=(black?"qrbn":"QRBN")[kind];check(board,expected);
    }
    // e5xd6 ep (white), e4xd3 ep (black), including separate lifts.
    certabo::RawBoard board{};char expected[65];
    for(int i=0;i<64;++i) expected[i]='.';expected[64]=0;
    int from=black?36:28,captured=black?35:27,to=black?43:19;
    board[from]=initial[black?12:52];board[captured]=initial[black?51:11];
    expected[from]=black?'p':'P';expected[captured]=black?'P':'p';check(board,expected);
    board[from]={};expected[from]='.';check(board,expected);
    board[captured]={};expected[captured]='.';check(board,expected);
    board[to]=initial[black?12:52];expected[to]=black?'p':'P';check(board,expected);
  }
  puts("PASS: both colors, both castles, Q/R/B/N promotions and en passant with real IDs");
  uint8_t blob[certabo::Calibration::kBlobSize];assert(calibration.save(blob));
  std::ofstream out("tools/certabo-usb-calibration.log");
  out<<"[CERTABO_EXPORT] d0cf134f8a1a ";
  for(auto b:blob){char hex[3];sprintf(hex,"%02X",b);out<<hex;}
  out<<"\n";
}
