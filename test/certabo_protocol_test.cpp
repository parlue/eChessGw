#include "certabo_startup.h"
#include "certabo_led_window.h"
#include "certabo_move_lights.h"
#include "certabo_protocol.h"

#include <assert.h>
#include <cstring>
#include <stdio.h>
#include <string>
#include <vector>

using namespace certabo;
const char* start = "rnbqkbnrpppppppp................................PPPPPPPPRNBQKBNR";

RawBoard initial(bool queens = true) {
  RawBoard board{};
  for (size_t i = 0; i < 64; ++i) {
    if (start[i] != '.' || (queens && (i == 19 || i == 43)))
      board[i] = PieceId{{3, 0, 84, static_cast<uint8_t>(i), 255}};
  }
  return board;
}
std::string message(const RawBoard& board) {
  std::string out = ":";
  for (const auto& id : board) for (auto b : id) out += std::to_string(b) + " ";
  return out + "\r\n";
}
size_t feed(Parser& parser, const std::string& text, std::vector<RawBoard>* boards = nullptr) {
  size_t count = 0;
  for (auto c : text) if (parser.feed(static_cast<uint8_t>(c)) == ParseResult::Position) {
    ++count;
    if (boards) boards->push_back(parser.board());
  }
  return count;
}
void learn(Calibration& c, const RawBoard& board) {
  for (int i = 0; i < 6; ++i) assert(!c.observe(board));
  assert(c.observe(board));
  assert(c.ready());
}
std::string fen(const uint8_t frame[67]) {
  assert(frame[0] == 's');
  uint8_t check = 0;
  for (int i = 0; i < 65; ++i) check ^= frame[i];
  const char* hex = "0123456789ABCDEF";
  assert(frame[65] == hex[check >> 4] && frame[66] == hex[check & 15]);
  std::string out;
  for (int row = 0; row < 8; ++row) {
    int blanks = 0;
    for (int col = 0; col < 8; ++col) {
      char p = static_cast<char>(frame[64 - (row * 8 + col)]);
      if (p == '.') ++blanks;
      else { if (blanks) out += std::to_string(blanks); blanks = 0; out += p; }
    }
    if (blanks) out += std::to_string(blanks);
    if (row != 7) out += '/';
  }
  return out;
}

int main() {
  {
    MoveLights lights;char board[64];memset(board,'.',64);board[62]='n';board[26]='B';
    uint8_t stale=26,source=62,target=45;
    lights.add(&stale,1,board);lights.add(&source,1,board);lights.add(&target,1,board);
    const uint64_t pair=(uint64_t(1)<<62)|(uint64_t(1)<<45);
    assert(lights.bits()==pair);
    for(int i=0;i<8;++i) {lights.add(nullptr,0,board);lights.add(&source,1,board);lights.add(&target,1,board);assert(lights.bits()==pair);}
    board[62]='.';assert(!lights.observe(board));board[45]='n';assert(lights.observe(board));
  }
  {
    // Completed human move, destination echo first (opposite to source-first).
    MoveLights lights;char board[64];memset(board,'.',64);board[5]='B';
    lights.observe(board);board[5]='.';board[12]='B';lights.observe(board);
    uint8_t to=12,from=5;
    lights.add(&to,1,board);lights.observe(board);
    lights.add(&from,1,board);assert(lights.observe(board));assert(!lights.bits());
    lights.add(&to,1,board);lights.add(&from,1,board);assert(!lights.bits());
  }
  {
    MoveLights lights;char board[64];memset(board,'.',64);board[5]='B';
    lights.observe(board);
    board[5]='.';lights.observe(board); // Lift precedes source LED.
    uint8_t from=5,to=12;lights.add(&from,1,board);
    board[12]='B';assert(lights.observe(board));assert(!lights.bits());
    lights.add(&to,1,board);lights.add(&from,1,board);assert(!lights.bits());
    // Entire move precedes both LED phases.
    lights.reset();board[12]='.';board[5]='B';lights.observe(board);
    board[5]='.';board[12]='B';lights.observe(board);
    lights.add(&from,1,board);assert(lights.observe(board));assert(!lights.bits());
  }
  {
    // e2-e4 followed by a black capture on e4: target arrives first.
    MoveLights lights;char board[64];memset(board,'.',64);
    board[12]='P';board[36]='q';uint8_t from=12,to=28;
    lights.add(&from,1,board);lights.add(&to,1,board);
    board[12]='.';board[28]='P';assert(lights.observe(board));
    lights.add(&to,1,board);assert(!lights.bits());
    from=36;lights.add(&from,1,board);
    const uint64_t pair=(uint64_t(1)<<36)|(uint64_t(1)<<28);
    assert(lights.bits()==pair);
    board[28]='.';assert(!lights.observe(board));assert(lights.bits()==pair);
    board[36]='.';assert(!lights.observe(board));
    board[28]='q';assert(lights.observe(board));assert(!lights.bits());
  }
  {
    MoveLights lights;char board[64];memset(board,'.',64);board[52]='p';
    uint8_t from=52,to=36;const uint64_t pair=(uint64_t(1)<<52)|(uint64_t(1)<<36);
    lights.add(&from,1,board);lights.add(nullptr,0,board);lights.add(&to,1,board);
    for(int i=0;i<20;++i) {lights.add(nullptr,0,board);assert(lights.bits()==pair);}
    board[52]='.';assert(!lights.observe(board));assert(lights.bits()==pair);
    board[36]='p';assert(lights.observe(board));assert(!lights.bits());
    lights.add(&from,1,board);lights.add(&to,1,board);assert(!lights.bits());
    uint8_t next=51;board[51]='p';lights.add(&next,1,board);assert(lights.bits()==(uint64_t(1)<<51));
    lights.reset();assert(!lights.bits());
  }
  {
    LedWindow window;
    uint8_t from[8],to[8],off[8]{},out[8];
    const uint8_t e7=52,e5=36;
    ledMask(&e7,1,from);ledMask(&e5,1,to);
    window.add(from);window.add(to);window.add(off);
    window.snapshot(off,out);
    assert(out[1]==16 && out[3]==16);
    window.delivered();window.snapshot(off,out);
    for(auto b:out) assert(b==0);
  }
  auto board = initial();
  const auto text = message(board);
  // Exercise every possible BLE split, including the CR/LF boundary.
  for (size_t split = 0; split <= text.size(); ++split) {
    Parser parser;
    size_t count = feed(parser, text.substr(0, split));
    count += feed(parser, text.substr(split));
    assert(count == 1 && parser.board() == board);
  }
  Parser parser;
  assert(feed(parser, "noise without a colon\r\n" + text + text) == 2);
  // Bare line wrapping inside decimal tokens is present in cer2nut tests.
  std::string wrapped;
  for (size_t i = 0; i < text.size() - 2; ++i) {
    wrapped += text[i];
    if (i % 17 == 0) wrapped += '\n';
  }
  assert(feed(parser, wrapped + "D\r\n") == 1);
  assert(parser.board() == board && parser.leds == LedType::Rgb);
  feed(parser, "L\r\n");
  assert(parser.leds == LedType::Mono);
  for (const std::string& bad : std::vector<std::string>{":256 ", ":-1 ", ":3X ", ":999999 ", ":\r\n",
                               ":255 255\r\n", ":" + std::string(3000, ' ') + "\r\n"}) {
    assert(feed(parser, bad) == 0);
    assert(feed(parser, text) == 1 && parser.board() == board);
  }
  assert(feed(parser, text.substr(0, text.size() - 2) + "1 \r\n") == 0);  // 321 values
  assert(feed(parser, ":0 0 0 ") == 0);
  parser.resetStream();  // dropped notification: discard incomplete old record
  assert(feed(parser, "0 0 0\r\n" + text) == 1);
  parser.reset();
  ParseResult last = ParseResult::None;
  for (auto c : std::string(":255 255 0 0 0 0 255 255\r\n")) last = parser.feed(c);
  assert(last == ParseResult::OccupancyOnly && parser.leds == LedType::Unknown);

  Calibration calibration;
  learn(calibration, board);
  assert(calibration.count() == 34);
  uint8_t frame[67]{};
  board[19] = board[43] = PieceId{};
  assert(calibration.decode(board, frame));
  assert(fen(frame) == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR");
  const auto pawn = board[52];  // e2
  board[52] = PieceId{};
  assert(calibration.decode(board, frame));
  assert(fen(frame) == "rnbqkbnr/pppppppp/8/8/8/8/PPPP1PPP/RNBQKBNR");
  board[36] = pawn;  // e4
  assert(calibration.decode(board, frame));
  assert(fen(frame) == "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR");
  board[11] = PieceId{};  // ... d5 followed by exd5
  board[36] = PieceId{};
  board[27] = pawn;
  assert(calibration.decode(board, frame));
  assert(fen(frame) == "rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR");
  auto invalid = board;
  invalid[30] = PieceId{{1, 2, 3, 4, 5}};
  uint8_t sentinel[67]; memset(sentinel, 0xa5, sizeof(sentinel));
  assert(!calibration.decode(invalid, sentinel));
  for (auto b : sentinel) assert(b == 0xa5);  // no partial position escapes
  invalid[30] = pawn;
  assert(!calibration.decode(invalid, frame));  // same ID on two squares

  // Verify orientation for every square, including all four corners.
  for (size_t i = 0; i < 64; ++i) {
    RawBoard single{}; single[i] = pawn;
    assert(calibration.decode(single, frame));
    for (size_t j = 0; j < 64; ++j) assert(frame[64 - j] == (i == j ? 'P' : '.'));
  }
  RawBoard promotion{};
  promotion[0] = initial()[43];  // learned spare white queen on a8
  assert(calibration.decode(promotion, frame));
  assert(fen(frame) == "Q7/8/8/8/8/8/8/8");

  // Majority accepts three transient bad scans out of seven.
  Calibration noisy;
  auto clean = initial(false);
  auto missing = clean; missing[0] = PieceId{};
  for (int i = 0; i < 3; ++i) assert(!noisy.observe(missing));
  for (int i = 0; i < 3; ++i) assert(!noisy.observe(clean));
  assert(noisy.observe(clean) && noisy.count() == 32);
  Calibration duplicate;
  auto dup = clean; dup[1] = dup[0];
  for (int i = 0; i < 10; ++i) assert(!duplicate.observe(dup));
  Calibration wrongSetup;
  auto wrong = clean; wrong[20] = initial()[43];
  for (int i = 0; i < 10; ++i) assert(!wrongSetup.observe(wrong));
  Calibration interrupted;
  for (int i = 0; i < 6; ++i) assert(!interrupted.observe(clean));
  interrupted.resetWindow();
  assert(!interrupted.observe(clean));

  uint8_t blob[Calibration::kBlobSize]{};
  assert(calibration.save(blob));
  Calibration loaded;
  assert(loaded.load(blob, sizeof(blob)) && loaded.count() == 34);
  assert(loaded.decode(board, frame));
  assert(fen(frame) == "rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR");
  assert(!loaded.load(blob, sizeof(blob) - 1));
  for (size_t i = 0; i < sizeof(blob); ++i) {
    blob[i] ^= 1;
    assert(!loaded.load(blob, sizeof(blob)));
    blob[i] ^= 1;
  }
  loaded.reset();
  assert(!loaded.ready() && !loaded.decode(board, frame));

  uint8_t mask[8], rgb[247];
  const uint8_t corners[] = {0, 7, 56, 63};
  ledMask(corners, 4, mask);
  assert(mask[0] == 0x81 && mask[7] == 0x81);
  for (int i = 1; i < 7; ++i) assert(mask[i] == 0);
  for (uint8_t square = 0; square < 64; ++square) {
    ledMask(&square, 1, mask);
    rgbCommand(mask, rgb);
    assert(rgb[0] == 255 && rgb[1] == 85 && rgb[245] == 13 && rgb[246] == 10);
    size_t lit = 0;
    for (int i = 0; i < 81; ++i) {
      assert(rgb[2 + i * 3] == 0 && rgb[3 + i * 3] == 0);
      if (rgb[4 + i * 3]) { assert(rgb[4 + i * 3] == 0x40); ++lit; }
    }
    assert(lit == 4);
  }
  uint8_t a1 = 0;
  ledMask(&a1, 1, mask); rgbCommand(mask, rgb);
  for (int index : {7, 8, 16, 17}) assert(rgb[4 + index * 3] == 0x40);
  ledMask(nullptr, 0, mask); rgbCommand(mask, rgb);
  for (int i = 2; i < 245; ++i) assert(rgb[i] == 0);
  Calibration required;
  for(int i=0;i<12;++i) assert(!required.observe(initial(false), true));
  for(int i=0;i<7;++i) required.observe(initial(), true);
  assert(required.count()==34);
  Startup startup;
  startup.mask(0,initial(false),mask);
  for(auto b:mask) assert(b==0); // Existing startup debounce needs two distinct scans.
  auto missingQueens=initial(false); missingQueens[8][4]=254;
  startup.mask(1,missingQueens,mask);
  assert(mask[2]==8 && mask[5]==8);
  startup.reset();
  auto incomplete=initial();incomplete[5]={};incomplete[24]=incomplete[0];
  startup.mask(0,incomplete,mask);
  for(auto b:mask) assert(b==0);
  incomplete[8][4]=254;
  startup.mask(1,incomplete,mask);
  assert(mask[0]==33 && mask[3]==1); // Missing f8, duplicate a8/a5.
  startup.mask(0,initial(),mask);
  for(auto b:mask) assert(b==0);
  startup.learned();
  auto setup=initial();
  startup.mask(10000,setup,mask);  // No successful LED send: no skipped flashes.
  assert(mask[3]==0x18 && mask[4]==0x18);
  assert(required.decode(setup,frame) && !startup.finish(frame));
  uint32_t now=0;
  for(int phase=0;phase<6;++phase) {
    startup.mask(now,setup,mask);
    assert(mask[3]==(phase%2==0 ? 0x18 : 0));
    assert(mask[4]==(phase%2==0 ? 0x18 : 0));
    startup.sent(now);
    now+=700;
  }
  startup.mask(now,setup,mask);
  assert(mask[2]==8 && mask[5]==8);
  assert(!startup.finish(frame));
  setup[43]={}; startup.mask(now,setup,mask);
  assert(mask[2]==8 && mask[5]==0);
  setup[19]={}; startup.mask(now,setup,mask);
  for(auto b:mask) assert(b==0);
  auto swapped=setup;std::swap(swapped[0],swapped[1]);
  startup.mask(now,swapped,mask,&required);assert(mask[0]==3);
  assert(required.decode(setup,frame));
  assert(startup.finish(frame) && !startup.active());
  startup.reset(); assert(startup.active() && !startup.finish(frame));
  puts("PASS: Certabo framing, required queens, calibration, persistence, positions, LED mapping and startup sequence");
}
