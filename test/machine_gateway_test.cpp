#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <array>
#include <vector>
#include <thread>
#include <utility>
#include <string>
#include <cctype>
#define CHESSLINK_ENABLE_MACHINE 1
#include "../include/machine/selection.h"
#include "../include/machine/castle_motion.h"
#include "../include/machine/delivery_gate.h"
#include "../include/machine/pending_leds.h"
#include "../include/machine/players.h"
#include "../include/machine/cable_echo.h"
#include "../include/cable_status_policy.h"
#include "../include/cable_raw_trace.h"
#include "../include/machine/virtual_operator.h"
#include "../include/machine/physical_promotion.h"
#include "../.pio/machine_gateway_rules.inc"
#include "../.pio/machine_gateway_protocol.inc"

uint32_t fakeNow = 1000;
uint32_t millis() { return fakeNow; }
void delay(unsigned) {}
constexpr int LOW = 0, INPUT_PULLUP = 2;
void pinMode(int, int) {}
int digitalRead(int) { return 1; }
struct Log { template<class... T> void printf(const char*, T...) {} void println(const char*) {} } Serial;
int __builtin_popcountll(uint64_t n) { int c = 0; while (n) { n &= n - 1; ++c; } return c; }
bool subscribed = true;
unsigned offers = 0, stopped = 0, stoppedScans = 0;
void machineGatewayStopBoardSearch() { ++stoppedScans; }
void machineGatewayCancelBoardCommand() {}
bool moveWriteOk=true;
unsigned moveTargetCalls=0;
char moveTarget[64] = {};
bool chessnutmoveSetTargetPosition(const char* board) {
  ++moveTargetCalls; memcpy(moveTarget,board,64); return moveWriteOk;
}
unsigned promotionCalls=0;int promotionFrom=-1,promotionTo=-1;char promotionPiece=0;
bool promotionWriteOk=true;
bool testCynusPromotionDriver(int,int,char);
bool cynusExecuteMachinePromotion(int from,int to,char piece) {
  ++promotionCalls;promotionFrom=from;promotionTo=to;promotionPiece=piece;return testCynusPromotionDriver(from,to,piece);
}
void clearBoardLeds(BoardType) {}
using Frame = std::array<uint8_t, 67>;
std::vector<Frame> notifications, cableStatuses, completePositions;
std::vector<uint32_t> cableStatusTimes;
Frame sentStatus{};
void chesslinkServerStart() { ++offers; }
void chesslinkServerStopMachineOffer() { ++stopped; }
bool chesslinkServerMachineSubscribed() { return subscribed; }
bool chesslinkServerMachineStatusSent(const uint8_t* f) { return subscribed && memcmp(f, sentStatus.data(), 67) == 0; }
void chesslinkServerPublishStatus(const uint8_t* f) {
  if (subscribed) { memcpy(sentStatus.data(), f, 67); notifications.push_back(sentStatus); }
}
void pgnRecorderOnBoardStatus(const uint8_t* f) { Frame v; memcpy(v.data(), f, 67); completePositions.push_back(v); }
void machineSaveAndExport() {}
unsigned recorderRestarts = 0;
void machineRecorderRestart() { ++recorderRestarts; }
void machineGatewayResetGame();
bool testCableWriteOk=true;
size_t machineGatewayWriteCable(const uint8_t* f, size_t n) {
  if(!testCableWriteOk) return 0;
  if (n == 67 && f[0] == 's') {
    Frame v; memcpy(v.data(), f, n); cableStatuses.push_back(v);
    cableStatusTimes.push_back(fakeNow);
  }
  return n;
}
#include "../.pio/machine_gateway_runtime.inc"
unsigned physicalLedCalls = 0;
uint8_t physicalLedFrame[167] = {};
BoardType testBoardType = BoardType::Chessnut;
BoardType currentBoardType() { return testBoardType; }
// Real Cynus decoder; transport and plausibility use deterministic doubles.
namespace CynusDecoder {
enum class MoveCycle { WaitHumanMove, WaitRobotPosition };
MoveCycle moveCycle=MoveCycle::WaitHumanMove;
std::string lastSentMoveUci;
std::vector<std::string> motorWrites;
bool engineMayMoveNow() { return moveCycle!=MoveCycle::WaitRobotPosition; }
void setMoveCycle(MoveCycle value) {moveCycle=value;}
bool sendCynus(const std::string& line) {motorWrites.push_back(line);return promotionWriteOk;}
void cynusDisplay(const std::string&) {}
std::string moveDisplayText(const std::string& uci) {return uci;}
enum class EngineSide { Unknown, White, Black };
bool firstMoveOrientationLocked = false;
EngineSide engineSide = EngineSide::Unknown, sideToMoveNow = EngineSide::White;
char board64[65] = {};
uint8_t ledGrid[81] = {};
uint8_t ledValue(int f, int r) { return ledGrid[f*9+r]; }
std::string squareName(int f, int r) { return std::string(1,char('a'+f))+char('8'-r); }
uint8_t dominantSquarePattern(int, int) { return 0; }
bool extractPhasedMoveFromLCommand(std::string&) { return false; } // Generated uniform frames.
bool plausibleBoardMove(const char*, int from, int to, EngineSide) {
  char target[64]; return machinePreview(from ^ 56, to ^ 56, target);
}
#include "../.pio/machine_cynus_decoder.inc"
#include "../.pio/machine_cynus_promotion.inc"
void verify(const uint8_t* frame) {
  char current[64]; machineBoard(current);
  for (int i=0;i<64;++i) board64[i]=current[i ^ 56];
  sideToMoveNow=machineWhiteToMove()?EngineSide::White:EngineSide::Black;
  for(int i=0;i<81;++i) {
    const auto hex=[](char c) { return c>='A'?c-'A'+10:c-'0'; };
    ledGrid[i]=uint8_t(hex(frame[3+2*i])*16+hex(frame[4+2*i]));
  }
  std::string uci;
  assert(extractMoveFromLCommand(uci));
  const std::string expected=squareName(candidateFrom%8,7-candidateFrom/8)+squareName(candidateTo%8,7-candidateTo/8);
  assert(uci==expected);
}
}
bool testCynusPromotionDriver(int from,int to,char figure) {
  char current[64];machineBoard(current);
  for(int i=0;i<64;++i) CynusDecoder::board64[i]=current[i ^ 56];
  CynusDecoder::sideToMoveNow=machineWhiteToMove()?CynusDecoder::EngineSide::White:CynusDecoder::EngineSide::Black;
  return CynusDecoder::cynusExecuteMachinePromotion(from,to,figure);
}
void dispatchLedFrameToBoard(BoardType type, const uint8_t* f) {
  ++physicalLedCalls; memcpy(physicalLedFrame, f, 167);
  if(type==BoardType::Cynus) CynusDecoder::verify(f);
}
size_t writeFrame(const uint8_t*, size_t n) { return n; }
#include "../.pio/machine_gateway_ble_route.inc"
#include "../.pio/machine_gateway_ble_reset.inc"

int sq(const char* s) { return (s[1] - '1') * 8 + s[0] - 'a'; }
char piece(const Frame& f, int s) { return char(f[1 + modeBStatusWireIndex(s % 8, s / 8 + 1)]); }
void cableIdle() {
  uint8_t f[167]; encodeLedFrame(nullptr, 0, f, false);
  machineGatewayCommand(f, 167);
}
void pump(unsigned ms) {
  for (unsigned i = 0; i < ms; i += 100) {
    fakeNow += 100;
    cableIdle();
    machineGatewayPoll();
  }
}
void boot() {
  machineGatewayCableActivity();
  assert(offers == 1 && notifications.empty());
  assert(machineGatewayAcceptSoftware());
  machineGatewayServiceSelection();
  assert(machineGatewayActive() && completePositions.size() == 1);
  assert(machineGatewayWantsBoard());
  pump(1500);
}
void suggest(bool software, const char* from, const char* to) {
  uint8_t f[167]; const uint8_t squares[] = {uint8_t(sq(from)), uint8_t(sq(to))};
  encodeLedFrame(squares, 2, f, false);
  if (software) serverLed(f, 167); else machineGatewayCommand(f, 167);
}
void move(bool software, const char* from, const char* to, unsigned stages = 2) {
  const size_t begin = notifications.size(), pgnBegin = completePositions.size();
  const char moving = virtualGame.board[sq(from)];
  suggest(software, from, to);
  pump(6500);
  assert(!halted && !lifting);
  assert(notifications.size() == begin + stages);
  assert(completePositions.size() == pgnBegin + 1); // No lift recorded as a move.
  assert(piece(notifications[begin], sq(from)) == '.');
  assert(piece(notifications.back(), sq(to)) == moving);
  assert(virtualGame.board[sq(from)] == '.' && virtualGame.board[sq(to)] == moving);
  assert(cableStatuses.back() == notifications.back());
  assert(physicalLedCalls == 0);
}
void boardReport(const char board[64]) {
  uint8_t frame[67] = {'s'};
  for (int i = 0; i < 64; ++i) frame[1 + modeBStatusWireIndex(i % 8, i / 8 + 1)] = board[i];
  computeModeBChecksumHex(frame + 65, frame, 65, false);
  machineGatewayBoardStatus(frame);
}
void attachBoard() {
  assert(machineGatewaySelectPhysical());
  machineGatewayBoardLink(true);
  boardReport(kStartBoard);
  assert(boardSynchronized);
  pump(1500);
}
void physicalMove(bool software, const char* from, const char* to) {
  const size_t n = notifications.size();
  suggest(software, from, to); pump(400);
  assert(lifting && notifications.size() == n); // No synthetic lift.
  char actual[64], target[64]; machineBoard(actual);
  assert(machinePreview(sq(from), sq(to), target));
  actual[sq(from)] = '.'; boardReport(actual);
  assert(lifting && notifications.size() == n + 1);
  pump(2000); assert(lifting); // Time alone never completes the move.
  boardReport(target); assert(!lifting && !halted);
  assert(memcmp(virtualGame.board, target, 64) == 0);
  pump(1500);
}
void robotMove(bool software, const char* from, const char* to, size_t stages = 2) {
  const size_t n = notifications.size();
  suggest(software, from, to); pump(4000);
  assert(lifting && notifications.size() == n);
  char target[64]; assert(machinePreview(sq(from), sq(to), target));
  if (testBoardType==BoardType::ChessnutMove) {
    assert(moveTargetCalls>0 && !memcmp(moveTarget,target,64));
    assert(physicalLedCalls==0); // Full target, never converted back into LED hints.
  }
  if (stages == 4) {
    char partial[64], before[64]; machineBoard(before);
    assert(machineCastleStage(before,partial,sq(from),sq(to),1));
    boardReport(partial); assert(lifting && !robotConfirmedReplay);
    assert(!memcmp(virtualGame.board,before,64));
    memcpy(partial,before,64);
    const int base=sq(from)/8*8;
    const int rookFrom=base+(sq(to)>sq(from)?7:0);
    const int rookTo=base+(sq(to)>sq(from)?5:3);
    partial[rookTo]=partial[rookFrom];partial[rookFrom]='.';
    boardReport(partial); assert(lifting && !robotConfirmedReplay);
    assert(!memcmp(virtualGame.board,before,64));
  }
  if(stages==3 && machineCapturedSquare(sq(from),sq(to))!=sq(to)) {
    const int victim=machineCapturedSquare(sq(from),sq(to));assert(victim>=0);
    char before[64],partial[64];machineBoard(before);memcpy(partial,before,64);
    partial[sq(to)]=partial[sq(from)];partial[sq(from)]='.';
    boardReport(partial);assert(lifting && !robotConfirmedReplay);
    assert(!memcmp(virtualGame.board,before,64)); // Captured pawn must be gone too.
    memcpy(partial,before,64);partial[victim]='.';
    boardReport(partial);assert(lifting && !robotConfirmedReplay);
    assert(!memcmp(virtualGame.board,before,64));
  }
  const size_t replayBegin = notifications.size();
  boardReport(target); assert(robotConfirmedReplay);
  pump(6500); assert(!lifting && notifications.size() == replayBegin + stages);
  assert(memcmp(virtualGame.board, target, 64) == 0);
}
void selectionTests() {
  MachineSelection standalone;
  assert(standalone.physicalConnected());
  assert(standalone.softwareConnected() && !standalone.machine());
  for (int i = 0; i < 200; ++i) {
    MachineSelection race; assert(race.cableActivity());
    bool board = false, software = false;
    std::thread a([&] { board = race.physicalConnected(); });
    std::thread b([&] { software = race.softwareConnected(); });
    a.join(); b.join();
    assert(board);
    assert(!software || race.board());
    assert(!race.cableActivity());
    assert(race.physicalConnected() && race.softwareConnected() == software);
    MachineSelection cutoff; cutoff.cableActivity(); assert(cutoff.softwareConnected());
    bool accepted = false, locked = false;
    std::thread c([&] { accepted = cutoff.physicalConnected(); });
    std::thread d([&] { locked = cutoff.lockVirtual(); });
    c.join(); d.join(); assert(accepted != locked);
    assert(cutoff.physicalConnected() == accepted);
  }
}
int main(int argc, char** argv) {
  assert(argc == 2);
  const char* scenario = argv[1];
  if (!strcmp(scenario, "raw_trace")) {
    CableRawTrace trace;
    for (unsigned i = 0; i < 4096; ++i) trace.push(uint8_t(i));
    trace.push(0x51); trace.push(0x71); // Overflow must be explicit, never silent.
    assert(trace.received == 4098 && trace.dropped == 2);
    uint8_t bytes[2000]; assert(trace.take(bytes, sizeof(bytes)) == sizeof(bytes));
    for (unsigned i = 0; i < sizeof(bytes); ++i) assert(bytes[i] == uint8_t(i));
    for (unsigned i = 4096; i < 5096; ++i) trace.push(uint8_t(i));
    unsigned offset = 2000;
    while (trace.count) {
      const size_t n = trace.take(bytes, sizeof(bytes));
      for (size_t i = 0; i < n; ++i) assert(bytes[i] == uint8_t(offset++));
    }
    assert(offset == 5096 && trace.received == 5098 && trace.dropped == 2);
    assert(trace.take(bytes, sizeof(bytes)) == 0);
  }
  else if (!strcmp(scenario, "selection")) { selectionTests(); }
  else if (!strcmp(scenario, "standalone")) {
    assert(machineGatewaySelectPhysical());
    assert(machineGatewayAcceptSoftware());
    uint8_t f[167]; for (int i = 0; i < 167; ++i) f[i] = uint8_t(i);
    f[0] = 'L'; serverLed(f, 167);
    assert(physicalLedCalls == 1 && !machineGatewayActive());
    // Since the verified 2026-10-11 orientation fix, ordinary BLE LED
    // commands pass through unchanged; the old rotation expectation was stale.
    assert(memcmp(physicalLedFrame, f, sizeof(f)) == 0);
    assert(offers == 0 && notifications.empty());
  }
  else if (!strcmp(scenario, "physical")) {
    machineGatewayCableActivity(); assert(offers == 1);
    assert(machineGatewaySelectPhysical());
    machineGatewayServiceSelection(); assert(stopped == 1);
    assert(!machineGatewayAcceptSoftware());
    machineGatewayCableActivity(); machineGatewayPoll();
    assert(offers == 1 && notifications.empty() && cableStatuses.empty() && completePositions.empty());
    assert(machineGatewaySelectPhysical()); // Reconnect does not open machine mode.
  } else {
    boot();
    if (!strcmp(scenario, "cynus_stale_oneshot") || !strcmp(scenario, "move_stale_oneshot")) {
      testBoardType = !strcmp(scenario, "cynus_stale_oneshot") ? BoardType::Cynus : BoardType::ChessnutMove;
      attachBoard();
      robotMove(true, "d2", "d3"); robotMove(false, "d7", "d5");
      robotMove(true, "c1", "d2"); robotMove(false, "g8", "f6");
      robotMove(true, "e2", "e4");
      suggest(false, "d5", "e4"); pump(4000);
      assert(lifting && !robotConfirmedReplay);
      suggest(true, "e4", "e4"); // Host echoes previous capture while robot runs.
      char target[64]; assert(machinePreview(sq("d5"), sq("e4"), target));
      boardReport(target); assert(robotConfirmedReplay);
      for (int i=0; i<100 && (lifting || pendingLeds.count); ++i) pump(100);
      assert(!lifting && !halted && !pendingLeds.count);
      assert(hints == (uint64_t(1) << sq("e4")));
      const unsigned dispatched = physicalLedCalls + moveTargetCalls;
      suggest(true, "d2", "b4"); // Fresh M5 command is sent only once.
      assert(candidateFrom == sq("d2") && candidateTo == sq("b4"));
      pump(1500);
      assert(lifting && physicalLedCalls + moveTargetCalls > dispatched);
      assert(machinePreview(sq("d2"), sq("b4"), target));
      boardReport(target); pump(6500);
      assert(!lifting && !halted && virtualGame.board[sq("b4")] == 'B');
    }
    if (!strcmp(scenario, "phoenix_live_stale_echo")) {
      // Actual live game 2026-11-02: Phoenix White / M5 Black.
      timing.observe(false, true);
      move(false, "d2", "d4"); move(true, "g8", "f6");
      move(false, "c2", "c3"); move(true, "d7", "d5");
      move(false, "g1", "f3"); move(true, "b8", "c6");
      move(false, "b1", "d2"); move(true, "c8", "f5");
      move(false, "d1", "b3"); move(true, "g7", "g6");
      move(false, "b3", "b7", 3); move(true, "f5", "d7");
      move(false, "f3", "e5"); move(true, "c6", "e5", 3);
      suggest(false, "d4", "e5"); pump(400); assert(lifting);
      suggest(true, "d4", "d4"); // M5 echoes the lifted white pawn.
      while (lifting && movementStage == 0) pump(100);
      suggest(true, "d4", "e5"); // M5 echoes the capture endpoints.
      while (lifting) pump(100);
      pump(1400); // Queued partial d4 is still a possible black pawn destination.
      assert(hints == (uint64_t(1) << sq("d4")));
      assert(virtualGame.board[sq("e5")] == 'P' && machineWhiteToMove() == false);
      const size_t complete = completePositions.size();
      suggest(true, "a8", "b8"); // One-shot actual M5 reply, no repeats.
      assert(candidateFrom == sq("a8") && candidateTo == sq("b8"));
      pump(6500);
      assert(!halted && !lifting && completePositions.size() == complete + 1);
      assert(virtualGame.board[sq("a8")] == '.' && virtualGame.board[sq("b8")] == 'r');
      printf("Live regression: stale d4 echo cannot contaminate one-shot Ra8-b8.\n");
    }
    if (!strcmp(scenario, "phoenix_quiet_f8d6") || !strcmp(scenario, "phoenix_quiet_f8d6_pause")) {
      timing.observe(false, true);
      move(false, "e2", "e4"); move(true, "e7", "e6");
      move(false, "d2", "d4");
      char before[64]; machineBoard(before);
      const size_t notifyStart = notifications.size(), cableStart = cableStatuses.size();
      const size_t pgnStart = completePositions.size();
      suggest(true, "f8", "d6");
      for (int i = 0; i < 30 && !lifting; ++i) pump(100);
      assert(lifting && movementStage == 0 && !halted);
      assert(piece(notifications.back(), sq("f8")) == '.');
      assert(piece(notifications.back(), sq("d6")) == '.');
      if (!strcmp(scenario, "phoenix_quiet_f8d6_pause")) {
        // Elapsed time and BLE delivery alone must not place the bishop.
        cableIdle(); cableIdle();
        assert(delivery.sends == 2);
        for (int i = 0; i < 100; ++i) { fakeNow += 100; machineGatewayPoll(); }
        assert(lifting && notifications.size() == notifyStart + 1);
        assert(completePositions.size() == pgnStart);
      }
      pump(6500);
      assert(!lifting && !halted && notifications.size() == notifyStart + 2);
      assert(completePositions.size() == pgnStart + 1);
      std::vector<Frame> stages;
      std::vector<uint32_t> firstAt;
      std::vector<unsigned> sends;
      for (size_t i = cableStart; i < cableStatuses.size(); ++i) {
        const Frame& f = cableStatuses[i];
        if (stages.empty() && piece(f, sq("f8")) == 'b') continue;
        if (stages.empty() || stages.back() != f) {
          stages.push_back(f); firstAt.push_back(cableStatusTimes[i]); sends.push_back(0);
        }
        ++sends.back();
      }
      assert(stages.size() == 2 && sends[0] >= 3 && sends[1] >= 3);
      assert(firstAt[1] - firstAt[0] >= 500);
      if (!strcmp(scenario, "phoenix_quiet_f8d6"))
        assert(firstAt[1] - firstAt[0] < 1000); // Extra pacing has been removed.
      before[sq("f8")] = '.';
      for (size_t step = 0; step < 2; ++step) {
        if (step) before[sq("d6")] = 'b';
        assert(stages[step] == notifications[notifyStart + step]);
        for (int field = 0; field < 64; ++field) assert(piece(stages[step], field) == before[field]);
      }
      printf("Bf8-d6: lift then placement, all 64 fields checked on BLE/UART; lift held %ums, reports=%u.\n",
        unsigned(firstAt[1] - firstAt[0]), sends[0]);
    }
    if (!strcmp(scenario, "phoenix_photo_capture") || !strcmp(scenario, "phoenix_photo_capture_pause")) {
      // Phoenix photo, before Virgin's Ne4xc5. Rank 1 first.
      const char position[] =
        "R.BQ.RK." "P.PP.PP." ".P.....P" "....N..."
        "..b.q..." "p.p....." ".pp..ppp" "r.b..rk.";
      static_assert(sizeof(position) == 65, "full board");
      memcpy(virtualGame.board, position, 64);
      virtualGame.whiteToMove = true;
      virtualGame.whiteCastleK = virtualGame.whiteCastleQ = false;
      virtualGame.blackCastleK = virtualGame.blackCastleQ = false;
      players.assignFirst(true);
      timing.observe(false, true); // Actual Phoenix convention: only reply slots.
      publish(virtualGame.board);
      pump(1500);
      const size_t notifyStart = notifications.size();
      const size_t cableStart = cableStatuses.size();
      const size_t pgnStart = completePositions.size();
      suggest(true, "e4", "c5");
      if (!strcmp(scenario, "phoenix_photo_capture_pause")) {
        for (int i = 0; i < 30 && !(lifting && movementStage == 1); ++i) pump(100);
        assert(lifting && movementStage == 1 && !halted);
        assert(delivery.sends == 0 && !timing.allowActiveGoStatus());
        // Two requests do not satisfy the three-report delivery gate.
        cableIdle(); cableIdle();
        assert(delivery.sends == 2);
        const size_t held = notifications.size();
        for (int i = 0; i < 200; ++i) { fakeNow += 100; machineGatewayPoll(); }
        assert(lifting && movementStage == 1 && !halted);
        assert(notifications.size() == held && completePositions.size() == pgnStart);
        assert(piece(notifications.back(), sq("e4")) == '.');
        assert(piece(notifications.back(), sq("c5")) == '.');
        printf("Ne4xc5: victim-removed stage preserved for 20s with only two cable reports; no placement or commit.\n");
      }
      pump(6500);
      assert(!halted && !lifting && notifications.size() == notifyStart + 3);
      assert(completePositions.size() == pgnStart + 1);
      char expected[64]; memcpy(expected, position, 64);
      std::vector<Frame> stages;
      std::vector<unsigned> sends;
      std::vector<uint32_t> firstAt, lastAt;
      for (size_t i = cableStart; i < cableStatuses.size(); ++i) {
        const Frame& f = cableStatuses[i];
        if (stages.empty() && piece(f, sq("e4")) == 'N') continue;
        if (stages.empty() || stages.back() != f) {
          stages.push_back(f); sends.push_back(0);
          firstAt.push_back(cableStatusTimes[i]); lastAt.push_back(cableStatusTimes[i]);
        }
        ++sends.back(); lastAt.back() = cableStatusTimes[i];
      }
      assert(stages.size() == 3);
      for (size_t step = 0; step < 3; ++step) {
        if (step == 0) expected[sq("e4")] = '.';
        if (step == 1) expected[sq("c5")] = '.';
        if (step == 2) expected[sq("c5")] = 'N';
        assert(stages[step] == notifications[notifyStart + step]);
        for (int field = 0; field < 64; ++field)
          assert(piece(stages[step], field) == expected[field]);
        assert(sends[step] >= 3);
        if (step < 2) {
          assert(firstAt[step + 1] - firstAt[step] >= 500);
          if (!strcmp(scenario, "phoenix_photo_capture"))
            assert(firstAt[step + 1] - firstAt[step] < 1000);
        }
        printf("Ne4xc5 step %u: e4=%c c5=%c, cable reports=%u, first=%ums last=%ums\n",
          unsigned(step + 1), piece(stages[step], sq("e4")), piece(stages[step], sq("c5")),
          sends[step], unsigned(firstAt[step]), unsigned(lastAt[step]));
      }
      assert(memcmp(virtualGame.board, expected, 64) == 0);
    }
    if (!strcmp(scenario, "phoenix_echo")) {
      // Exact photo game from 2026-11-02: Hiarcs ...Bg4-h5, Virgin Bc1-e3.
      move(true, "e2", "e4"); move(false, "e7", "e5");
      move(true, "b1", "c3"); move(false, "b8", "c6");
      move(true, "b2", "b3"); move(false, "g8", "f6");
      move(true, "g1", "f3"); move(false, "f8", "c5");
      move(true, "f1", "b5"); move(false, "e8", "g8", 4);
      move(true, "b5", "c6", 3); move(false, "d7", "c6", 3);
      move(true, "e1", "g1", 4); move(false, "c8", "g4");
      move(true, "d2", "d3"); move(false, "f6", "d7");
      move(true, "h2", "h3"); move(false, "g4", "h5");
      const size_t complete = completePositions.size();
      // Think longer than the former five-second STOP threshold.
      for (int i = 0; i < 80; ++i) {
        fakeNow += 100; suggest(false, "g4", "h5"); machineGatewayPoll();
      }
      assert(!halted && !lifting && completePositions.size() == complete);
      suggest(true, "c1", "e3");
      // Continuous echo through execution, including alternate OFF phases.
      for (int i = 0; i < 100; ++i) {
        fakeNow += 100;
        if (i % 2) cableIdle(); else suggest(false, "g4", "h5");
        machineGatewayPoll();
      }
      assert(!halted && !lifting && completePositions.size() == complete + 1);
      assert(virtualGame.board[sq("c1")] == '.' && virtualGame.board[sq("e3")] == 'B');
      // The old LED pair must NOT execute a phantom Bh5-g4 on Black's turn.
      assert(virtualGame.board[sq("h5")] == 'b' && virtualGame.board[sq("g4")] == '.');
      // A new field set is a normal, accepted next command.
      move(false, "a7", "a6");
    }
    if (!strcmp(scenario, "phoenix_echo_return")) {
      move(true, "e2", "e4"); move(false, "g8", "f6");
      move(true, "a2", "a3");
      // Blink-OFF and status polls cannot rearm an old pair, but explicit X
      // on the cable's new turn permits an intentional Nf6-g8 return move.
      uint8_t clear[] = {'X', 0, 0}; computeModeBChecksumHex(clear + 1, clear, 1, false);
      machineGatewayCommand(clear, sizeof(clear));
      move(false, "f6", "g8");
      move(true, "a3", "a4");
      // An opponent-move indication also distinguishes fresh intent from the
      // old pair: the same knight endpoints may then be used again without X.
      suggest(false, "a3", "a4"); pump(2500);
      move(false, "g8", "f6");
      machineGatewayResetGame(); assert(cableEcho.completedFields == 0);
      move(false, "e2", "e4");
    }
    if (!strcmp(scenario, "phoenix_opponent_echo")) {
      move(true, "e2", "e4");
      for (int i = 0; i < 80; ++i) {
        fakeNow += 100; suggest(false, "e2", "e4"); machineGatewayPoll();
      }
      assert(!halted && !lifting); // Displaying the opponent's last move is not an error either.
      pump(2500); move(false, "e7", "e5");
    }
    if (!strcmp(scenario, "phoenix_echo_white")) {
      move(false, "e2", "e4"); move(true, "e7", "e5");
      move(false, "g1", "f3");
      const size_t complete = completePositions.size();
      for (int i = 0; i < 80; ++i) {
        fakeNow += 100; suggest(false, "g1", "f3"); machineGatewayPoll();
      }
      assert(!halted && !lifting && completePositions.size() == complete);
      suggest(true, "b8", "c6");
      for (int i = 0; i < 100; ++i) {
        fakeNow += 100;
        if (i % 2) cableIdle(); else suggest(false, "g1", "f3");
        machineGatewayPoll();
      }
      assert(!halted && !lifting && completePositions.size() == complete + 1);
      assert(virtualGame.board[sq("f3")] == 'N' && virtualGame.board[sq("c6")] == 'n');
      move(false, "f1", "c4");
    }
    if (!strcmp(scenario, "board_setup")) {
      assert(machineGatewaySelectPhysical()); machineGatewayBoardLink(true);
      char wrong[64]; memcpy(wrong, kStartBoard, 64); wrong[sq("a2")] = '.';
      boardReport(wrong); assert(!boardSynchronized);
      suggest(false, "d2", "d4"); pump(3000);
      assert(!lifting && candidateFrom == sq("d2"));
      boardReport(kStartBoard); pump(400); assert(lifting);
      char target[64]; machinePreview(sq("d2"), sq("d4"), target);
      boardReport(target); assert(!lifting);
    }
    if(!strcmp(scenario,"robot_capture") || !strcmp(scenario,"move_robot_capture")) {
      testBoardType=!strcmp(scenario,"robot_capture")?BoardType::Cynus:BoardType::ChessnutMove;
      attachBoard();robotMove(true,"e2","e4");robotMove(false,"d7","d5");
      robotMove(true,"e4","d5",3);robotMove(false,"d8","d5",3);
      assert(virtualGame.board[sq("d5")]=='q' && virtualGame.board[sq("e4")]=='.');
    }
    if (!strcmp(scenario,"robot_ep") || !strcmp(scenario,"move_robot_ep")) {
      testBoardType = !strcmp(scenario,"move_robot_ep") ? BoardType::ChessnutMove : BoardType::Cynus;
      attachBoard();
      for(bool white: {true,false}) for(bool left: {true,false}) {
        machineGatewayResetGame(); boardReport(startingBoard); pump(1500);
        if(white) {
          robotMove(true,"e2","e4");robotMove(false,"a7","a6");
          robotMove(true,"e4","e5");robotMove(false,left?"d7":"f7",left?"d5":"f5");
          robotMove(true,"e5",left?"d6":"f6",3);
          assert(virtualGame.board[sq(left?"d5":"f5")]=='.');
          assert(virtualGame.board[sq(left?"d6":"f6")]=='P');
        } else {
          robotMove(true,"a2","a3");robotMove(false,left?"d7":"f7",left?"d5":"f5");
          robotMove(true,"a3","a4");robotMove(false,left?"d5":"f5",left?"d4":"f4");
          robotMove(true,"e2","e4");robotMove(false,left?"d4":"f4","e3",3);
          assert(virtualGame.board[sq("e4")]=='.' && virtualGame.board[sq("e3")]=='p');
        }
      }
    }
    if (!strcmp(scenario, "robot_castle_long") || !strcmp(scenario,"move_robot_castle_long")) {
      testBoardType = !strcmp(scenario,"move_robot_castle_long") ? BoardType::ChessnutMove : BoardType::Cynus;
      attachBoard();
      robotMove(true, "d2", "d4"); robotMove(false, "d7", "d5");
      robotMove(true, "b1", "c3"); robotMove(false, "b8", "c6");
      robotMove(true, "c1", "f4"); robotMove(false, "c8", "f5");
      robotMove(true, "d1", "d2"); robotMove(false, "d8", "d7");
      robotMove(true, "e1", "c1", 4); robotMove(false, "e8", "c8", 4);
      assert(virtualGame.board[sq("c1")]=='K' && virtualGame.board[sq("d1")]=='R');
      assert(virtualGame.board[sq("c8")]=='k' && virtualGame.board[sq("d8")]=='r');
    }
    if (!strcmp(scenario, "robot_castle") || !strcmp(scenario,"move_robot_castle")) {
      testBoardType = !strcmp(scenario,"move_robot_castle") ? BoardType::ChessnutMove : BoardType::Cynus; attachBoard();
      robotMove(true, "e2", "e4"); robotMove(false, "e7", "e5");
      robotMove(true, "g1", "f3"); robotMove(false, "b8", "c6");
      robotMove(true, "f1", "c4"); robotMove(false, "g8", "f6");
      robotMove(true, "e1", "g1", 4);
      robotMove(false, "f8", "e7"); robotMove(true, "d2", "d3");
      robotMove(false, "e8", "g8", 4);
      if (testBoardType==BoardType::ChessnutMove) assert(moveTargetCalls>0 && physicalLedCalls==0);
      else assert(moveTargetCalls==0);
    }
    if (!strcmp(scenario, "robot")) {
      testBoardType = BoardType::Cynus; attachBoard();
      suggest(false, "d2", "d4"); const size_t n = notifications.size();
      pump(5000); assert(lifting && notifications.size() == n);
      // Actual Cynus grid convention: file ascending, rank 8 first.
      // d2/d4 must illuminate file corners 3/4 and rank corners 6/7,4/5.
      for (int file = 0; file < 9; ++file) for (int rank = 0; rank < 9; ++rank) {
        const bool lit = (file == 3 || file == 4) && rank >= 4 && rank <= 7;
        const int offset = 3 + 2 * (file * 9 + rank);
        assert(physicalLedFrame[offset] == (lit ? 'F' : '0'));
        assert(physicalLedFrame[offset + 1] == (lit ? 'F' : '0'));
      }
      char target[64]; assert(machinePreview(sq("d2"), sq("d4"), target));
      boardReport(target); assert(lifting && robotConfirmedReplay);
      assert(virtualGame.board[sq("d2")] == 'P');
      pump(4000); assert(!lifting && !robotConfirmedReplay && virtualGame.board[sq("d4")] == 'P');
      assert(notifications.size() == n + 2);
      // Phoenix stops polling while the robot executes BearChess's reply.
      timing.observe(false, true);
      suggest(true, "g8", "f6"); pump(4000);
      assert(lifting && !robotConfirmedReplay);
      assert(machinePreview(sq("g8"), sq("f6"), target));
      const size_t cableBegin = cableStatuses.size();
      for (int i = 0; i < 20; ++i) { fakeNow += 100; machineGatewayPoll(); }
      assert(cableStatuses.size() == cableBegin); // No invented robot completion.
      boardReport(target);
      for (int i = 0; i < 40; ++i) { fakeNow += 100; machineGatewayPoll(); }
      assert(!lifting && !robotConfirmedReplay);
      assert(cableStatuses.size() == cableBegin + 6); // Three sends per stage.
      assert(piece(cableStatuses[cableBegin], sq("g8")) == '.');
      assert(piece(cableStatuses[cableBegin], sq("f6")) == '.');
      assert(piece(cableStatuses.back(), sq("f6")) == 'n');
      assert(cableStatuses.back() == notifications.back());
    }
    if (!strcmp(scenario, "board")) {
      attachBoard();
      physicalMove(false, "d2", "d4"); physicalMove(true, "g8", "f6");
      boardReport(kStartBoard);
      assert(!players.assigned() && !awaitingStart && boardSynchronized);
      assert(recorderRestarts == 1 && machineWhiteToMove());
      pump(1500); physicalMove(true, "e2", "e4");
      machineGatewayResetGame(); assert(awaitingStart);
      suggest(false, "d2", "d4"); pump(2000); assert(!lifting);
      boardReport(kStartBoard); assert(!awaitingStart);
      pump(1500); physicalMove(false, "e2", "e4");
      assert(stoppedScans == 0);
    }
    if (!strcmp(scenario, "board_disconnect")) {
      attachBoard(); suggest(false, "d2", "d4"); pump(400); assert(lifting);
      machineGatewayBoardLink(false); const size_t n = notifications.size();
      pump(8000); assert(lifting && notifications.size() == n && machineGatewayWantsBoard());
      assert(machineGatewaySelectPhysical()); machineGatewayBoardLink(true);
      char target[64]; assert(machinePreview(sq("d2"), sq("d4"), target));
      boardReport(target); assert(!lifting && virtualGame.board[sq("d4")] == 'P');
      pump(1500); physicalMove(true, "g8", "f6");
    }
    if (!strcmp(scenario, "virtual_cutoff")) {
      move(false, "d2", "d4"); assert(stoppedScans == 1);
      assert(!machineGatewayWantsBoard() && !machineGatewaySelectPhysical());
      // A disconnect alone must not unlock a running virtual game.
      machineGatewayBoardLink(false);
      assert(!machineGatewayWantsBoard());
      machineGatewayResetGame(); assert(machineGatewayWantsBoard());
      assert(!memcmp(virtualGame.board, startingBoard, 64));
      // Repeated ground-position resets remain open for discovery.
      machineGatewayResetGame(); assert(machineGatewayWantsBoard());
      assert(machineGatewaySelectPhysical()); machineGatewayBoardLink(true);
      testBoardType = BoardType::Cynus;
      boardReport(startingBoard);
      assert(boardSynchronized && selection.board());
      pump(1500);
      robotMove(false, "e2", "e4");
      machineGatewayResetGame();
      assert(selection.board() && boardOnline && awaitingStart);
      boardReport(startingBoard);
      assert(boardSynchronized && !awaitingStart);
    }
    if (!strcmp(scenario, "position_newgame")) {
      move(false,"d2","d4");move(true,"g8","f6");
      registers[4]=7;
      uint8_t frame[67]={'s'};
      for(int sq=0;sq<64;++sq) frame[1+modeBStatusWireIndex(sq%8,sq/8+1)]=startingBoard[sq];
      computeModeBChecksumHex(frame+65,frame,65,false);
      frame[65]^=1;
      assert(!machineGatewaySoftwarePosition(frame));
      assert(virtualGame.board[sq("d4")]=='P');
      frame[65]^=1;
      assert(machineGatewaySoftwarePosition(frame));
      assert(newgameHold && !lifting && !players.assigned() && subscribed);
      assert(registers[4]==7 && !memcmp(virtualGame.board,startingBoard,64));
      assert(machineGatewayWantsBoard() && !selection.board());
      assert(!machineGatewayNewgamePublished());
      const size_t firstStart = cableStatuses.size()-1;
      // Phoenix need not send another query: the complete starting board
      // must be repeated until its minimum delivery/settle gate passes.
      for(int i=0;i<6;++i) { fakeNow+=100;machineGatewayPoll(); }
      assert(cableStatuses.back()==notifications.back() && machineGatewayNewgamePublished());
      assert(cableStatuses.size()>=firstStart+3);
      for(size_t i=firstStart;i<cableStatuses.size();++i)
        for(int sq=0;sq<64;++sq) assert(piece(cableStatuses[i],sq)==startingBoard[sq]);
      assert(!machineGatewaySoftwarePosition(frame)); // No overlapping restart.
      machineGatewayNewgameFinish();
      machineGatewayBoardLink(true);
      assert(!machineGatewaySoftwarePosition(frame)); // Real board owns its position.
    }
    if (!strcmp(scenario, "soft_newgame")) {
      move(true, "e2", "e4"); move(false, "e7", "e5");
      registers[4] = 7; registers[2] = 3;
      const unsigned saved = recorderRestarts;
      const unsigned cancelled = stoppedScans;
      assert(machineGatewayNewgameAllowed());
      assert(machineGatewaySoftNewgame());
      assert(newgameHold && !players.assigned() && !lifting && !halted);
      assert(machineGatewayWantsBoard() && !selection.board());
      assert(registers[4] == 7 && registers[2] == 3);
      assert(recorderRestarts == saved + 1 && stoppedScans == cancelled);
      assert(virtualGame.board[sq("e2")] == 'P');
      assert(virtualGame.board[sq("e7")] == 'p');
      assert(!cableStatuses.empty() && cableStatuses.back() == notifications.back());
      suggest(true, "d2", "d4"); assert(hints == 0);
      pump(600);
      assert(machineGatewayNewgamePublished());
      machineGatewayNewgameFinish(); assert(!newgameHold);
      subscribed = false;
      assert(!machineGatewayNewgameAllowed() && !machineGatewaySoftNewgame());
      subscribed = true;
      machineGatewayBoardLink(true);
      assert(!machineGatewayNewgameAllowed() && !machineGatewaySoftNewgame());
    }
    if (!strcmp(scenario, "reset")) {
      move(false, "d2", "d4");
      suggest(true, "g8", "f6"); pump(400); assert(lifting);
      uint8_t reset[] = {'T', 0, 0};
      computeModeBChecksumHex(reset + 1, reset, 1, false);
      Frame before; memcpy(before.data(), machineGatewayStatus(), 67);
      const size_t sent = notifications.size(), cableSent = cableStatuses.size();
      registers[4] = 7;
      assert(serverResetLeavesTransportUntouched());
      assert(recorderRestarts == 0 && lifting && players.assigned());
      assert(registers[4] == 7 && !memcmp(before.data(), machineGatewayStatus(), 67));
      assert(notifications.size() == sent && cableStatuses.size() == cableSent);
      machineGatewayCommand(reset, sizeof(reset));
      assert(recorderRestarts == 1 && !lifting && !halted && !players.assigned());
      assert(!pendingLeds.count && !hints && candidateFrom == -1);
      assert(registers[4] == 0x0f);
      assert(machineWhiteToMove() && memcmp(virtualGame.board, kStartBoard, 64) == 0);
      assert(notifications.back() == cableStatuses.back());
      assert(piece(notifications.back(), sq("d2")) == 'P');
      assert(piece(notifications.back(), sq("d4")) == '.');
      machineGatewayCommand(reset, sizeof(reset));
      assert(recorderRestarts == 1);
      pump(1500);
      move(true, "e2", "e4");
      subscribed = false;
      machineGatewayResetGame();
      assert(recorderRestarts == 2 && piece(cableStatuses.back(), sq("e2")) == 'P');
      assert(memcmp(machineGatewayStatus(), cableStatuses.back().data(), 67) == 0);
    }
    if (!strcmp(scenario, "white")) { move(true, "e2", "e4"); move(false, "e7", "e5"); }
    if (!strcmp(scenario, "black")) { move(false, "d2", "d4"); move(true, "g8", "f6"); }
    if (!strcmp(scenario, "early")) {
      suggest(false, "d2", "d4"); pump(400); assert(lifting);
      suggest(true, "g8", "f6"); assert(pendingLeds.count == 1);
      pump(6500);
      assert(!halted && !lifting && !pendingLeds.count);
      assert(virtualGame.board[sq("d4")] == 'P' && virtualGame.board[sq("f6")] == 'n');
      assert(completePositions.size() == 3 && notifications.size() == 5);
    }
    if (!strcmp(scenario, "capture")) {
      move(true, "e2", "e4"); move(false, "d7", "d5");
      const size_t n = notifications.size(); move(true, "e4", "d5", 3);
      assert(piece(notifications[n + 1], sq("d5")) == '.');
    }
    if (!strcmp(scenario, "ep")) {
      move(false, "e2", "e4"); move(true, "a7", "a6");
      move(false, "e4", "e5"); move(true, "d7", "d5");
      const size_t n = notifications.size(); move(false, "e5", "d6", 3);
      assert(piece(notifications[n + 1], sq("d5")) == '.');
    }
    if (!strcmp(scenario, "castle")) {
      move(true, "e2", "e4"); move(false, "e7", "e5");
      move(true, "g1", "f3"); move(false, "b8", "c6");
      move(true, "f1", "c4"); move(false, "g8", "f6");
      size_t n = notifications.size(); move(true, "e1", "g1", 4);
      assert(piece(notifications[n + 1], sq("g1")) == 'K');
      assert(piece(notifications[n + 1], sq("h1")) == 'R');
      assert(piece(notifications[n + 2], sq("h1")) == '.');
      move(false, "f8", "e7"); move(true, "d2", "d3");
      n = notifications.size(); move(false, "e8", "g8", 4);
      assert(piece(notifications[n + 1], sq("g8")) == 'k');
      assert(piece(notifications[n + 1], sq("h8")) == 'r');
    }
    if (!strcmp(scenario, "splash") || !strcmp(scenario, "reconnect")) {
      suggest(false, "d2", "d4"); pump(400); assert(lifting);
      const size_t n = notifications.size();
      if (!strcmp(scenario, "splash")) {
        uint8_t all[167]; memset(all, 'C', sizeof(all)); all[0] = 'L'; all[1] = all[2] = '2';
        serverLed(all, 167);
        assert(!halted && lifting && pendingLeds.count == 0);
      } else {
        subscribed = false; pump(2000); assert(lifting && notifications.size() == n);
        assert(machineGatewayAcceptSoftware() && !machineGatewaySelectPhysical());
        subscribed = true; chesslinkServerPublishStatus(machineGatewayStatus());
      }
      pump(3000); assert(!halted && !lifting && virtualGame.board[sq("d4")] == 'P');
      move(true, "g8", "f6");
    }
  }
  printf("Machine gateway test passed: %s\n", scenario);
  return 0;
}
