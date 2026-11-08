#include <cassert>
#include <cstdio>
#include <array>
#include "Arduino.h"
uint32_t fakeNow=1000;
FakeLog Serial;
#include "../src/chessnutmove_board.cpp"
bool phoenixHasActiveLedSuggestion=false;
std::vector<std::array<uint8_t,67>> reports;
void onBoardStatusFrame(const uint8_t* p) {
  std::array<uint8_t,67> frame; memcpy(frame.data(),p,67); reports.push_back(frame);
}
void armVerboseCableLog() {}
void resetKingLedFrameBaseline() {}
void dispatchLedFrameToBoard(BoardType,const uint8_t*) {}
int sq(const char* name) { return (name[1]-'1')*8+name[0]-'a'; }
uint64_t fields(const char* a,const char* b) { return (uint64_t(1)<<sq(a))|(uint64_t(1)<<sq(b)); }
void scan(const char* board) {
  uint8_t frame[38]={1,0x24};
  // Independent reference mapping: h8,a8,...,h1,a1; low nibble first.
  const char* codes=".qkbpnRPrBNQK";
  for (int r=0;r<8;++r) for (int f=0;f<8;++f) {
    int raw=r*8+f, s=(7-r)*8+7-f;
    frame[2+raw/2]|=uint8_t((strchr(codes,board[s])-codes)<<(4*(raw%2)));
  }
  handleBoardDataPacket(frame,38);
}
void boot() {
  assert(chessnutmoveConnect(NimBLEAddress{}));
  reports.clear(); writeChar->writes.clear();
  scan(motion.position.board);
  assert(reports.size()==1 && motion.haveGame);
  writeChar->writes.clear();
}
void hint(const char* a,const char* b) {
  SquareHighlight h[2]={{uint8_t(sq(a)),SquareHighlightRole::Generic},{uint8_t(sq(b)),SquareHighlightRole::Generic}};
  chessnutmoveExecuteHighlightedMove(h,2); fakeNow+=250; chessnutmovePoll();
}
void completed(const char* a,const char* b) {
  char next[64]; assert(motion.hintTarget(fields(a,b),next));
  assert(chessnutmoveSetTargetPosition(next)); scan(next);
}
int main() {
  boot();
  assert(chessnutmoveSetTargetPosition(motion.actual));
  assert(writeChar->writes.empty() && reports.size()==2);
  boot();
  uint8_t packed[35]; assert(chessnutMoveEncodeTarget(motion.actual,packed));
  // h8 rook8, g8 knight5; f8 bishop3,e8 king2; d8 queen1,c8 bishop3.
  assert(packed[2]==0x58 && packed[3]==0x23 && packed[4]==0x31 && packed[5]==0x85);
  assert(packed[30]==0xa6 && packed[31]==0xc9 && packed[32]==0x9b && packed[33]==0x6a);
  char decoded[64]; assert(chessnutMoveDecodeTarget(packed,decoded)); assert(!memcmp(decoded,motion.actual,64));
  for(int s=0;s<64;++s) {
    uint8_t leds[34]; chessnutMoveEncodeLeds(uint64_t(1)<<s,leds);
    for(int raw=0;raw<64;++raw) assert(((leds[2+raw/2]>>(4*(raw%2)))&15)==(raw==63-s?2:0));
  }
  // Separately blinking endpoints are joined only within the Move adapter.
  SquareHighlight first={uint8_t(sq("e2")),SquareHighlightRole::Generic};
  chessnutmoveExecuteHighlightedMove(&first,1); fakeNow+=250; chessnutmovePoll();
  assert(motion.phase==ChessnutMoveControl::Idle);
  chessnutmoveExecuteHighlightedMove(nullptr,0);
  first.squareIndex=uint8_t(sq("e4")); chessnutmoveExecuteHighlightedMove(&first,1);
  fakeNow+=250; chessnutmovePoll(); assert(chessnutmoveIsBusyMoving());
  scan(motion.target); boot();
  // Motor starts only after compatible source+destination, not readiness LEDs.
  SquareHighlight ready[4]={{27,SquareHighlightRole::Generic},{28,SquareHighlightRole::Generic},{35,SquareHighlightRole::Generic},{36,SquareHighlightRole::Generic}};
  chessnutmoveSetHighlightedSquares(ready,4); fakeNow+=250; chessnutmovePoll();
  assert(motion.phase==ChessnutMoveControl::Idle);
  hint("e2","e4"); assert(chessnutmoveIsBusyMoving());
  auto writes=writeChar->writes.size();
  assert(writeChar->writes[writes-2].size()==34 && writeChar->writes.back().size()==35);
  char target[64]; memcpy(target,motion.target,64);
  assert(chessnutmoveSetTargetPosition(target)); assert(writeChar->writes.size()==writes);
  char partial[64]; memcpy(partial,motion.actual,64); partial[sq("e2")]='.';
  scan(partial); assert(reports.size()==1);
  fakeNow+=5000; chessnutmovePoll(); assert(chessnutmoveIsBusyMoving());
  chessnutmoveSetHighlightedSquares(nullptr,0); assert(writeChar->writes.size()==writes);
  scan(target); assert(!chessnutmoveIsBusyMoving() && reports.size()==2 && !motion.position.whiteToMove);
  hint("e2","e4"); assert(!chessnutmoveIsBusyMoving()); // echo never moves back
  clearHints();
  // Human move updates rules from real sensors; no motor echo.
  memcpy(target,motion.actual,64); target[sq("e7")]='.'; target[sq("e5")]='p';
  writes=writeChar->writes.size(); scan(target); hint("e7","e5");
  assert(!chessnutmoveIsBusyMoving());
  for(size_t i=writes;i<writeChar->writes.size();++i) assert(writeChar->writes[i][0]!=0x42);
  assert(!motion.hintTarget(fields("e2","e4"),decoded));
  // Both colours and a real capture use exact complete targets.
  completed("g1","f3"); completed("b8","c6");
  assert(!motion.hintTarget(fields("f3","g1"),decoded)); completed("f1","c4"); completed("g8","f6");
  completed("e1","g1"); assert(motion.actual[sq("f1")]=='R' && motion.actual[sq("h1")]=='.');
  completed("f6","e4"); assert(motion.actual[sq("e4")]=='n' && motion.actual[sq("f6")]=='.');
  // All four castlings: real driver motor packets carry king AND rook.
  // Neither king-only nor rook-only sensor reports complete the movement.
  for(bool white: {true,false}) for(bool kingSide: {true,false}) {
    boot();
    memset(motion.position.board,'.',64);
    motion.position.board[sq("e1")]='K'; motion.position.board[sq("e8")]='k';
    const int base=white?0:56, kingFrom=base+4,kingTo=base+(kingSide?6:2);
    const int rookFrom=base+(kingSide?7:0),rookTo=base+(kingSide?5:3);
    motion.position.board[rookFrom]=white?'R':'r';
    motion.position.whiteToMove=white;
    motion.position.whiteCastleK=motion.position.whiteCastleQ=white;
    motion.position.blackCastleK=motion.position.blackCastleQ=!white;
    scan(motion.position.board);
    char before[64];memcpy(before,motion.actual,64);
    assert(motion.hintTarget((uint64_t(1)<<kingFrom)|(uint64_t(1)<<kingTo),target));
    const size_t reportBegin=reports.size();writeChar->writes.clear();
    assert(chessnutmoveSetTargetPosition(target));
    assert(writeChar->writes.size()==2);
    assert(writeChar->writes[0].size()==34 && writeChar->writes[1].size()==35);
    assert(chessnutMoveDecodeTarget(writeChar->writes.back().data(),decoded));
    assert(!memcmp(decoded,target,64));
    assert(decoded[kingFrom]=='.' && decoded[rookFrom]=='.');
    assert(decoded[kingTo]==(white?'K':'k') && decoded[rookTo]==(white?'R':'r'));
    assert(chessnutmoveSetTargetPosition(target) && writeChar->writes.size()==2);
    memcpy(partial,before,64);partial[kingFrom]='.';partial[kingTo]=white?'K':'k';
    scan(partial);assert(chessnutmoveIsBusyMoving() && reports.size()==reportBegin);
    memcpy(partial,before,64);partial[rookFrom]='.';partial[rookTo]=white?'R':'r';
    scan(partial);assert(chessnutmoveIsBusyMoving() && reports.size()==reportBegin);
    scan(target);assert(!chessnutmoveIsBusyMoving() && reports.size()==reportBegin+1);
    assert(!memcmp(motion.actual,target,64) && motion.position.whiteToMove!=white);
  }
  // Both colors and both EP diagonals: target packet removes the victim.
  for(bool white: {true,false}) for(bool left: {true,false}) {
    boot();memset(motion.position.board,'.',64);
    motion.position.board[sq("e1")]='K';motion.position.board[sq("e8")]='k';
    const int from=white?sq("e5"):(left?sq("d4"):sq("f4"));
    const int to=white?(left?sq("d6"):sq("f6")):sq("e3");
    const int victim=white?(left?sq("d5"):sq("f5")):sq("e4");
    motion.position.board[from]=white?'P':'p';motion.position.board[victim]=white?'p':'P';
    motion.position.whiteToMove=white;motion.position.enPassantSquare=to;
    motion.position.whiteCastleK=motion.position.whiteCastleQ=motion.position.blackCastleK=motion.position.blackCastleQ=false;
    scan(motion.position.board);
    char before[64];memcpy(before,motion.actual,64);
    assert(motion.hintTarget((uint64_t(1)<<from)|(uint64_t(1)<<to),target));
    writeChar->writes.clear();const size_t reportBegin=reports.size();
    assert(chessnutmoveSetTargetPosition(target) && writeChar->writes.size()==2);
    assert(chessnutMoveDecodeTarget(writeChar->writes.back().data(),decoded));
    assert(decoded[from]=='.' && decoded[victim]=='.' && decoded[to]==(white?'P':'p'));
    assert(!memcmp(decoded,target,64));
    memcpy(partial,before,64);partial[to]=partial[from];partial[from]='.';
    scan(partial);assert(chessnutmoveIsBusyMoving() && reports.size()==reportBegin);
    memcpy(partial,before,64);partial[victim]='.';
    scan(partial);assert(chessnutmoveIsBusyMoving() && reports.size()==reportBegin);
    assert(chessnutmoveSetTargetPosition(target) && writeChar->writes.size()==2);
    scan(target);assert(!chessnutmoveIsBusyMoving() && reports.size()==reportBegin+1);
    assert(motion.position.whiteToMove!=white && !memcmp(motion.actual,target,64));
  }
  // En passant removes the actual victim, then returning to the initial
  // full position resets both colours without reconnecting.
  boot(); completed("e2","e4"); completed("a7","a6"); completed("e4","e5"); completed("d7","d5");
  completed("e5","d6"); assert(motion.actual[sq("d5")]=='.' && motion.actual[sq("d6")]=='P');
  ChessnutMoveControl initial; initial.reset();
  assert(chessnutmoveSetTargetPosition(initial.position.board)); scan(initial.position.board);
  assert(motion.haveGame && motion.position.whiteToMove);
  char invalid[64]; memcpy(invalid,motion.actual,64); invalid[4]='.';
  assert(!chessnutmoveSetTargetPosition(invalid));
  // Bad data must not become a sensor confirmation.
  uint8_t corrupt[38]={1,0x24,0xff}; auto n=reports.size(); handleBoardDataPacket(corrupt,38); assert(reports.size()==n);
  // Every exact promotion piece is preserved; endpoint-only choices are refused.
  for(char piece: {'Q','R','B','N'}) for(bool white: {true,false}) for(bool capture: {false,true}) {
    boot(); memset(motion.position.board,'.',64); motion.position.board[4]='K'; motion.position.board[60]='k';
    int from=white?sq(capture?"a7":"b7"):sq(capture?"a2":"b2"),to=white?sq("b8"):sq("b1");
    motion.position.board[from]=white?'P':'p';
    if(capture) motion.position.board[to]=white?'r':'R';
    motion.position.whiteToMove=white;
    motion.position.whiteCastleK=motion.position.whiteCastleQ=motion.position.blackCastleK=motion.position.blackCastleQ=false;
    scan(motion.position.board); assert(!motion.hintTarget((uint64_t(1)<<from)|(uint64_t(1)<<to),decoded));
    memcpy(target,motion.actual,64); target[from]='.'; target[to]=white?piece:char(piece+32);
    assert(chessnutmoveSetTargetPosition(target));
    assert(chessnutMoveDecodeTarget(writeChar->writes.back().data(),decoded)); assert(decoded[to]==target[to]);
    memcpy(partial,motion.actual,64);partial[from]='.';partial[to]=white?'P':'p';
    const size_t reportBegin=reports.size();scan(partial);
    assert(chessnutmoveIsBusyMoving() && reports.size()==reportBegin);
    scan(target); assert(motion.phase==ChessnutMoveControl::Idle && reports.size()==reportBegin+1);
    assert(motion.position.whiteToMove!=white && motion.haveGame);
  }
  boot(); assert(motion.hintTarget(fields("d2","d4"),target));
  bleClient->mtu=23; assert(!chessnutmoveSetTargetPosition(target)); assert(motion.phase==ChessnutMoveControl::Idle);
  bleClient->mtu=255; writeChar->fail=true;
  assert(!chessnutmoveSetTargetPosition(target)); assert(motion.phase==ChessnutMoveControl::Fault);
  writeChar->fail=false; assert(!chessnutmoveSetTargetPosition(target));
  ClientCallbacks callback; callback.onDisconnect(bleClient,0); assert(!motion.haveSensor);
  boot(); assert(motion.hintTarget(fields("d2","d4"),target));
  writeChar->failAfter=1; // LED succeeded, motor write failed: never retry motors.
  assert(!chessnutmoveSetTargetPosition(target)); assert(motion.phase==ChessnutMoveControl::Fault);
  writeChar->failAfter=-1; assert(!chessnutmoveSetTargetPosition(target));
  boot(); assert(motion.hintTarget(fields("d2","d4"),target)); assert(chessnutmoveSetTargetPosition(target));
  fakeNow+=120000; chessnutmovePoll(); assert(motion.phase==ChessnutMoveControl::Fault);
  assert(!chessnutmoveSetTargetPosition(target));
  boot(); assert(motion.phase==ChessnutMoveControl::Idle && !hintFields);
  puts("Move: packets, LEDs, physical confirmation, echo, human/robot, castle, capture, promotion, MTU, failures, timeout, reconnect PASS");
}
