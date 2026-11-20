#include "machine_gateway.h"
#include "chessnutmove_board.h"
#include "cynus_board.h"
#ifdef CHESSLINK_ENABLE_MACHINE
#include "machine/machine.h"
#include "machine/castle_motion.h"
#include "machine/delivery_gate.h"
#include "machine/pending_leds.h"
#include "machine/players.h"
#include "machine/cable_echo.h"
#include "machine/physical_promotion.h"
#include "chesslink_server.h"
#include "pgn_recorder.h"
#include "cable_status_policy.h"
#include "gw_serial.h"  // keep last: Serial -> log-recording wrapper

namespace {
MachineSelection selection;
bool advertisingOwned = false;
bool runtimeStarted = false;
bool boardOnline = false, boardSynchronized = false, awaitingStart = false;
bool gameAdvanced = false;
bool physicalAtPosition = false;
bool robotConfirmedReplay = false;
char physicalExpected[64] = {};
char startingBoard[64] = {};
uint8_t physicalLed[167] = {};
uint32_t physicalLedAt = 0;
uint8_t statusFrame[67] = {};
uint32_t lastStatusAt = 0, lastPositionAt = 0;
uint8_t registers[256] = {};
bool cableSeen = false, halted = false;
bool newgameHold = false;
CableStatusPolicy timing;
uint64_t hints = 0;
uint32_t fieldSeenAt[64] = {};
uint32_t hintAt = 0, candidateAt = 0;
int candidateFrom = -1, candidateTo = -1;
bool lifting = false;
uint32_t liftedAt = 0;
int movementStage = 0;
MachinePendingLeds pendingLeds;
MachinePlayers players;
MachineDeliveryGate delivery;
bool softwareStepSent = false;
uint32_t softwareStepSentAt = 0;
bool castlingMotion = false;
MachineCableEcho cableEcho;
MachinePhysicalPromotion physicalPromotion;
MachineVirtualOperator virtualOperator;
const uint8_t* originalVirtualCableStatus() {
  return !selection.board() && virtualOperator.cableOverride ? virtualOperator.sourceFrame : statusFrame;
}

const uint8_t* virtualCableStatus() {
  if (physicalPromotion.active() && physicalPromotion.phase!=MachinePhysicalPromotion::Blocked &&
      !physicalPromotion.sourceSoftware && !robotConfirmedReplay) return physicalPromotion.frame;
  return originalVirtualCableStatus();
}

size_t sendCable(const uint8_t* frame, size_t n) {
  const size_t written = machineGatewayWriteCable(frame, n);
  lastStatusAt = millis();
  if (physicalPromotion.negotiating() && !physicalPromotion.sourceSoftware &&
      n==67 && written==n && !memcmp(frame,physicalPromotion.frame,n))
    physicalPromotion.delivery.sent(millis());
  if (!selection.board() && virtualOperator.cableOverride && n == 67 && written == n &&
      !memcmp(frame, virtualOperator.sourceFrame, n)) virtualOperator.sourceDelivery.sent(millis());
  if (n == 67 && frame[0] == 's' && written == n && memcmp(frame, statusFrame, n) == 0) {
    if (!delivery.sends) Serial.printf("[MACHINE TX] stage=%d status=%.*s\r\n", movementStage, int(n), frame);
    delivery.sent(millis());
  }
  return written;
}

uint32_t validCommands = 0, invalidCommands = 0;
uint32_t lastStateLogAt = 0;
uint64_t lastSoftwareFields = 0;
uint32_t lastSoftwareLedAt = 0;

void logRuntimeState() {
  const uint32_t now = millis();
  if (now - lastStateLogAt < 10000) return;
  lastStateLogAt = now;
  Serial.printf("[MACHINE STATE] turn=%s expects=%s halted=%d lifting=%d stage=%d candidate=%c%d-%c%d pending=%u cableReports=%u softwareSent=%d positionAge=%lu cableCommands=%lu invalid=%lu softwareLedAge=%lu fields=%08lX%08lX\r\n",
    machineWhiteToMove() ? "white" : "black",
    players.accepts(true, machineWhiteToMove()) ? "software" : "cable",
    halted, lifting, movementStage,
    candidateFrom >= 0 ? 'a' + candidateFrom % 8 : '-', candidateFrom >= 0 ? candidateFrom / 8 + 1 : 0,
    candidateTo >= 0 ? 'a' + candidateTo % 8 : '-', candidateTo >= 0 ? candidateTo / 8 + 1 : 0,
    pendingLeds.count, delivery.sends, softwareStepSent,
    static_cast<unsigned long>(now - lastPositionAt), static_cast<unsigned long>(validCommands),
    static_cast<unsigned long>(invalidCommands), static_cast<unsigned long>(now - lastSoftwareLedAt),
    static_cast<unsigned long>(lastSoftwareFields >> 32), static_cast<unsigned long>(uint32_t(lastSoftwareFields)));
  if (selection.board()) {
    Serial.printf("[MACHINE STATE PHYSICAL] type=%u online=%d synchronized=%d awaitingStart=%d atPosition=%d robotReplay=%d target=%.*s\r\n",
      unsigned(currentBoardType()), boardOnline, boardSynchronized, awaitingStart,
      physicalAtPosition, robotConfirmedReplay, lifting ? 64 : 0, physicalExpected);
  }
  if (physicalPromotion.active()) {
    Serial.printf("[PROMOTION STATE] phase=%u source=%s choice=%u figure=%c sent=%u clear=%d evidence=%u paused=%d\r\n",
      unsigned(physicalPromotion.phase),physicalPromotion.sourceSoftware?"software":"cable",
      physicalPromotion.choice,physicalPromotion.count?physicalPromotion.plan().promotion:'-',
      physicalPromotion.delivery.sends,physicalPromotion.ack.clearSeen,physicalPromotion.ack.evidence,physicalPromotion.paused);
  }
  char board[64]; machineBoard(board);
  Serial.printf("[MACHINE STATE BOARD] committed-a1-first=%.*s published-wire=%.*s\r\n",
    64, board, 67, statusFrame);
  if (virtualOperator.active()) {
    const auto& op = virtualOperator;
    Serial.printf("[VBOARD STATE] phase=%u step=%u source=%s choice=%c clear=%d evidence=%u responsive=%d next=%d\r\n",
      unsigned(op.phase), op.step, op.sourceSoftware ? "software" : "cable",
      op.planCount && op.plan().promotion ? op.plan().promotion : '-',
      op.ack.clearSeen, op.ack.evidence, op.ack.responsive(now), op.nextFound);
  }
}

void logLed(bool white, const uint8_t* frame, const char* reason,
            uint64_t mask, int matches) {
  static uint32_t previous[2] = {}, loggedAt[2] = {};
  uint32_t fingerprint = 2166136261u;
  for (size_t i = 0; i < 167; ++i) fingerprint = (fingerprint ^ frame[i]) * 16777619u;
  for (const char* p = reason; *p; ++p) fingerprint = (fingerprint ^ *p) * 16777619u;
  fingerprint ^= uint32_t(mask) ^ uint32_t(mask >> 32) ^ uint32_t(matches);
  const int side = white ? 1 : 0;
  const uint32_t now = millis();
  if (previous[side] == fingerprint || (loggedAt[side] && now - loggedAt[side] < 200)) return;
  previous[side] = fingerprint; loggedAt[side] = now;
  char fields[193] = {}; size_t pos = 0;
  for (int sq = 0; sq < 64; ++sq) if (mask & (uint64_t(1) << sq)) {
    fields[pos++] = 'a' + sq % 8; fields[pos++] = '1' + sq / 8; fields[pos++] = ' ';
  }
  Serial.printf("[MACHINE LED] %s turn=%s reason=%s matches=%d fields=%s raw=%.*s\r\n",
    white ? "software" : "cable", machineWhiteToMove() ? "white" : "black",
    reason, matches, fields, 167, reinterpret_cast<const char*>(frame));
}

void resetHints() {
  hints = 0;
  memset(fieldSeenAt, 0, sizeof(fieldSeenAt));
  candidateFrom = candidateTo = -1;
}
void publish(const char board[64], bool complete = true) {
  delivery.reset();
  softwareStepSent = false;
  statusFrame[0] = 's';
  for (int sq = 0; sq < 64; ++sq)
    statusFrame[1 + modeBStatusWireIndex(sq % 8, sq / 8 + 1)] = board[sq];
  computeModeBChecksumHex(statusFrame + 65, statusFrame, 65, false);
  chesslinkServerPublishStatus(statusFrame);
  // Long physical intermediate steps are not completed chess positions.
  if (complete) pgnRecorderOnBoardStatus(statusFrame);
  if (cableSeen && timing.allowActiveGoStatus()) sendCable(statusFrame, 67);
  lastPositionAt = millis();
}
void reply(const uint8_t* payload, size_t n) {
  uint8_t frame[167];
  memcpy(frame, payload, n);
  computeModeBChecksumHex(frame + n, frame, n, false);
  sendCable(frame, n + 2);
}
int nibble(uint8_t c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
#include "machine/virtual_operator_runtime.inc"
#include "machine/physical_promotion_runtime.inc"

void led(bool white, const uint8_t* frame) {
  if (newgameHold) return;
  if (physicalPromotionLed(white,frame)) return;
  // After an explicit New Game, discard old host highlights until the actual
  // board has been rebuilt. Initial board discovery still retains an opening.
  if (awaitingStart) return;
  SquareHighlight feedback[64];
  const size_t feedbackCount = decodeKingLedFrame(frame, feedback, 64);
  uint64_t feedbackMask = 0;
  for (size_t i = 0; i < feedbackCount; ++i) feedbackMask |= uint64_t(1) << feedback[i].squareIndex;
  if (white) { lastSoftwareFields = feedbackMask; lastSoftwareLedAt = millis(); }
  if (virtualLed(white, frame, feedbackMask)) {
    logLed(white, frame, "virtual-feedback", feedbackMask, virtualOperator.nextFound ? 1 : 0);
    return;
  }
  if (feedbackMask == UINT64_MAX) {
    logLed(white, frame, "all-board-initialization", feedbackMask, -1);
    return;
  }
  if (!selection.board() && !white && cableEcho.ignores(feedbackMask)) {
    logLed(white, frame, "completed-cable-move-echo", feedbackMask, -1);
    return;
  }
  if (!halted && lifting && chesslinkServerMachineSubscribed() && !players.accepts(white, machineWhiteToMove())) {
    if (feedbackCount > 4 || !pendingLeds.add(feedbackMask)) {
      halted = true;
      Serial.printf("[MACHINE] STOP: pending LED buffer overflow or reset pattern\r\n");
    } else {
      logLed(white, frame, "queued-until-move-complete", feedbackMask, -1);
    }
    return;
  }
  if (halted || lifting || !chesslinkServerMachineSubscribed() ||
      !players.accepts(white, machineWhiteToMove())) {
    logLed(white, frame, halted ? "halted" : lifting ? "lifting" :
      !chesslinkServerMachineSubscribed() ? "no-subscription" : "other-turn", feedbackMask, -1);
    return;
  }
  SquareHighlight squares[64];
  const size_t n = decodeKingLedFrame(frame, squares, 64);
  // Empty blink phases preserve the pair briefly. Reset/error splashes do not.
  uint64_t mask = 0;
  for (size_t i = 0; i < n; ++i) mask |= uint64_t(1) << squares[i].squareIndex;
  if (n == 0) { logLed(white, frame, "empty", mask, -1); return; }
  if (n > 4) { logLed(white, frame, "too-many-fields", mask, -1); resetHints(); players.clearOpening(white); return; }
  const uint32_t now = millis();
  if (!players.assigned()) {
    const uint64_t opening = players.accumulate(white, mask, now);
    int from = -1, to = -1;
    const int matches = virtualFindMove(opening, from, to);
    logLed(white, frame, "opening", opening, matches);
    if (matches != 1) return;
    players.assignFirst(white);
    resetHints();
    mask = opening;
    Serial.printf("[MACHINE] colours: software=%s cable=%s\r\n",
      white ? "white" : "black", white ? "black" : "white");
  }
#ifdef CHESSLINK_VIRTUAL_OPERATOR_V2
  if (!selection.board()) {
    char board[64]; machineBoard(board);
    auto ownFields = [&](uint64_t fields) {
      uint64_t own = 0;
      for (int sq = 0; sq < 64; ++sq) if (fields & (uint64_t(1) << sq)) {
        const char piece = board[sq];
        if (machineWhiteToMove() ? (piece >= 'A' && piece <= 'Z') : (piece >= 'a' && piece <= 'z'))
          own |= uint64_t(1) << sq;
      }
      return own;
    };
    int f = -1, t = -1;
    if (virtualFindMove(mask, f, t) == 1) {
      // An atomic command also supersedes partial cable-side corrections.
      if (hints & ~mask) resetHints();
    } else {
      if (now - hintAt > 2000) resetHints();
      const uint64_t source = ownFields(mask), previousSource = ownFields(hints);
      if (source && source != previousSource) resetHints();
      if (!source && !previousSource) {
        logLed(white, frame, "destination-before-fresh-source", mask, 0);
        return;
      }
    }
  }
#endif
  if (white) {
    // M5 sends a complete move once, unlike Phoenix's blinking endpoints.
    // A queued echo of the previous move may still contain a square that
    // happens to be usable by the new side (e.g. d4 for ...d5-d4). Never
    // merge it into an independently valid new software move, including
    // when a physical robot executes the duel: there may
    // be no second LED frame to expire the old square and recover.
    int from = -1, to = -1;
    if ((hints & ~mask) && virtualFindMove(mask, from, to) == 1) {
      logLed(white, frame, "fresh-software-move-replaces-stale-fields", mask, 1);
      resetHints();
    }
  }
  if (now - hintAt > 2000) resetHints();
  hintAt = now;
  // Expire each square independently. Phoenix first echoes the opponent's
  // move, then blinks its own endpoints; ongoing traffic must not keep the
  // old opponent squares alive forever.
  for (int sq = 0; sq < 64; ++sq) {
    const uint64_t bit = uint64_t(1) << sq;
    if (mask & bit) { fieldSeenAt[sq] = now; hints |= bit; }
    else if ((hints & bit) && now - fieldSeenAt[sq] > 2000) hints &= ~bit;
  }
  if (__builtin_popcountll(hints) > 4) {
    resetHints(); hints = mask;
    for (int sq = 0; sq < 64; ++sq) if (mask & (uint64_t(1) << sq)) fieldSeenAt[sq] = now;
  }
  int from = -1, to = -1;
  const int matches = virtualFindMove(hints, from, to);
  logLed(white, frame, matches == 1 ? "candidate" : "no-unique-move", hints, matches);
  if (matches != 1) { candidateFrom = candidateTo = -1; return; }
  if (candidateFrom != from || candidateTo != to) {
    candidateFrom = from; candidateTo = to; candidateAt = now;
    Serial.printf("[MACHINE] %s candidate %c%d-%c%d\r\n", white ? "software" : "cable",
      'a' + from % 8, from / 8 + 1, 'a' + to % 8, to / 8 + 1);
  }
}
void command(const uint8_t* frame, size_t length) {
  bool encoded = false;
  if (!modeBValidBlock(frame, length, &encoded)) {
    ++invalidCommands;
    if (invalidCommands <= 3) Serial.printf("[MACHINE RX] invalid checksum: %.*s\r\n", int(length), frame);
    return;
  }
  ++validCommands;
  if (frame[0] == 'X' || frame[0] == 'T')
    Serial.printf("[MACHINE RX] cable %c turn=%s lifting=%d\r\n", frame[0], machineWhiteToMove() ? "white" : "black", lifting);
  cableSeen = true;
  if (encoded) timing.observe(false, true);
  else if (frame[0] == 'L') {
    uint8_t a[2], b[2];
    computeModeBChecksumHex(a, frame, length - 2, false);
    computeModeBChecksumHex(b, frame, length - 2, true);
    timing.observe(true, memcmp(a, b, 2) == 0);
  }
  switch (frame[0]) {
    case 'S':
      physicalPromotionHostPoll(false);
      virtualHostPoll(false);
      sendCable(virtualCableStatus(), 67); break;
    case 'V': reply((const uint8_t*)"v0100", 5); break;
    case 'X':
      if (physicalPromotionClear(false)) { reply((const uint8_t*)"x",1); break; }
      virtualHostClear(false);
      players.clearOpening(false);
      if (!lifting && players.accepts(false, machineWhiteToMove())) {
        resetHints();
        if (!selection.board()) cableEcho.clear();
      }
      reply((const uint8_t*)"x", 1);
      break;
    // Cable-side King/module reset: preserve its established behavior.
    case 'T': machineGatewayResetGame(); break;
    case 'L':
      reply((const uint8_t*)"l", 1);
      sendCable(virtualCableStatus(), 67);
      led(false, frame);
      break;
    case 'R': case 'W': {
      const int hi = nibble(frame[1]), lo = nibble(frame[2]);
      if (hi < 0 || lo < 0) break;
      const int addr = hi * 16 + lo;
      if (frame[0] == 'W') {
        const int vh = nibble(frame[3]), vl = nibble(frame[4]);
        if (vh < 0 || vl < 0) break;
        registers[addr] = vh * 16 + vl;
      }
      static const char hex[] = "0123456789ABCDEF";
      const uint8_t payload[] = {uint8_t(frame[0] + 32), frame[1], frame[2],
        uint8_t(hex[registers[addr] >> 4]), uint8_t(hex[registers[addr] & 15])};
      reply(payload, 5);
      break;
    }
  }
}

void ensureStarted() {
  if (runtimeStarted || !selection.machine()) return;
  runtimeStarted = true;
  machineReset();
  machineBoard(startingBoard);
  registers[1] = 0x14; registers[4] = 0x0f;
  pinMode(9, INPUT_PULLUP);
  char board[64]; machineBoard(board); publish(board);
  Serial.println("[MACHINE] selected: virtual board; first legal opening selects colours");
  Serial.println("[MACHINE] virtual cable echo guard: 2026-11-02");
}
} // namespace

bool machineGatewayActive() { return selection.machine(); }
bool machineGatewayWantsBoard() { return !selection.virtualOnly(); }
void machineGatewayBoardLink(bool connected) {
  if (boardOnline == connected) return;
  boardOnline = connected;
  boardSynchronized = false;
  if (!connected && selection.board()) Serial.println("[MACHINE] board disconnected: waiting; no virtual fallback");
}
bool machineGatewaySelectPhysical() { return selection.physicalConnected(); }
bool machineGatewayAcceptSoftware() { return selection.softwareConnected(); }
void machineGatewayCableActivity() {
  if (selection.cableActivity() && !advertisingOwned) {
    advertisingOwned = true;
    chesslinkServerStart();
    Serial.println("[MACHINE] cable activity: racing real board against ChessLink software");
  }
}
void machineGatewayServiceSelection() {
  if (advertisingOwned && selection.physical()) {
    chesslinkServerStopMachineOffer();
    advertisingOwned = false;
    Serial.println("[MACHINE] real board selected; software offer closed until reboot");
  }
  ensureStarted();
}
const uint8_t* machineGatewayStatus() { return runtimeStarted ? statusFrame : nullptr; }
void machineGatewayCommand(const uint8_t* frame, size_t n) {
  ensureStarted();
  if (runtimeStarted) command(frame, n);
}
void machineGatewaySoftwareLed(const uint8_t frame[167]) {
  ensureStarted();
  if (runtimeStarted) led(true, frame); // Original BLE raster, no physical-board rotation.
}
void machineGatewaySoftwareClear() {
  if (physicalPromotionClear(true)) return;
  virtualHostClear(true);
  players.clearOpening(true);
  if (!lifting && players.accepts(true, machineWhiteToMove())) resetHints();
}
void machineGatewaySoftwarePoll() {
  physicalPromotionHostPoll(true);
  virtualHostPoll(true);
}
static void restartMachineGame(bool resetConfiguration) {
  if (!selection.machine()) return;
  ensureStarted();
  // Save only complete positions. An interrupted lift must never enter PGN.
  // Repeated startup resets with no opening do not create empty games.
  if (players.assigned() || lifting || halted) machineRecorderRestart();
  physicalPromotion.reset();
  machineReset();
  players.reset(); pendingLeds.clear(); resetHints();
  virtualOperator.reset();
  halted = lifting = castlingMotion = robotConfirmedReplay = false;
  movementStage = 0;
  liftedAt = hintAt = candidateAt = softwareStepSentAt = 0;
  cableEcho.clear();
  if (resetConfiguration) {
    memset(registers, 0, sizeof(registers));
    registers[1] = 0x14; registers[4] = 0x0f;
  }
  gameAdvanced = false;
  // A known new starting position reopens board discovery. Never replace an
  // existing physical-board selection or infer a reset from a disconnect.
  if (!boardOnline && selection.reopenAtStart())
    Serial.println("[MACHINE] starting position: board radio search reopened");
  awaitingStart = selection.board();
  boardSynchronized = !awaitingStart;
  if (selection.board() && boardOnline) machineGatewayCancelBoardCommand();
  char board[64]; machineBoard(board); publish(board);
  // Explicit restart must also publish under conservative Phoenix timing.
  // publish() already sends to an active-timing cable; avoid a duplicate there.
  if (!cableSeen || !timing.allowActiveGoStatus()) sendCable(statusFrame, 67);
  Serial.println(resetConfiguration ? "[MACHINE] board restart: starting position sent to both hosts; colours undecided"
                                   : "[NEWGAME] soft restart: starting position sent; connections/configuration preserved");
}
void machineGatewayResetGame() { restartMachineGame(true); }
bool machineGatewayNewgameAllowed() {
  return selection.machine() && !selection.board() && !boardOnline && chesslinkServerMachineSubscribed();
}
bool machineGatewaySoftNewgame() {
  if (newgameHold || !machineGatewayNewgameAllowed()) return false;
  newgameHold = true;
  restartMachineGame(false);
  return true;
}
bool machineGatewayNewgamePublished() {
  if (!machineGatewayNewgameAllowed()) return false;
  char board[64]; machineBoard(board);
  return !memcmp(board, startingBoard, 64) && delivery.ready(millis(), 500) &&
         chesslinkServerMachineStatusSent(statusFrame);
}
bool machineGatewaySoftwarePosition(const uint8_t frame[67]) {
  if (frame[0] != 's' || !modeBValidBlock(frame, 67) || !machineGatewayNewgameAllowed()) return false;
  for (int sq = 0; sq < 64; ++sq)
    if ((frame[1 + modeBStatusWireIndex(sq % 8, sq / 8 + 1)] & 127) != startingBoard[sq]) return false;
  return machineGatewaySoftNewgame();
}
void machineGatewayNewgameFinish() { newgameHold = false; }
void machineGatewayBoardStatus(const uint8_t frame[67]) {
  if (!selection.board() || !boardOnline) return;
  ensureStarted();
  char actual[64], current[64]; machineBoard(current);
  for (int sq = 0; sq < 64; ++sq)
    actual[sq] = frame[1 + modeBStatusWireIndex(sq % 8, sq / 8 + 1)];
  const bool atStart = memcmp(actual, startingBoard, 64) == 0;
  if (atStart && gameAdvanced) { machineGatewayResetGame(); machineBoard(current); }
  if (awaitingStart) {
    if (!atStart) return;
    awaitingStart = false;
  }
  if (!boardSynchronized) {
    if (memcmp(actual, current, 64) && !(lifting && !memcmp(actual, physicalExpected, 64))) {
      // Reconnect during a lift may report the last actually forwarded frame.
      bool same = true;
      for (int sq = 0; sq < 64; ++sq)
        if (actual[sq] != statusFrame[1 + modeBStatusWireIndex(sq % 8, sq / 8 + 1)]) same = false;
      if (!lifting || !same) return;
    }
    boardSynchronized = true;
    Serial.println("[MACHINE] physical board synchronized");
  }
  physicalAtPosition = !memcmp(actual, current, 64);
  if (physicalPromotion.negotiating()) {
    if (!physicalAtPosition) physicalPromotionFail("board changed during figure confirmation");
    return;
  }
  if (robotConfirmedReplay) return; // Do not replace an in-progress confirmed replay.
  if (physicalPromotion.phase==MachinePhysicalPromotion::Executing && memcmp(actual,physicalExpected,64)) return;
  bool complete = false;
  if (lifting && !memcmp(actual, physicalExpected, 64) && (currentBoardType() == BoardType::Cynus || currentBoardType() == BoardType::ChessnutMove)) {
    // The robot reports only a confirmed camera snapshot. Adapt that snapshot
    // to ChessLink steps AFTER execution, never before the physical scan.
    robotConfirmedReplay = true;
    movementStage = 0;
    char preview[64];
    castlingMotion = machineCastleStage(current, preview, candidateFrom, candidateTo, 0);
    current[candidateFrom] = '.';
    publish(current, false);
    return;
  }
  if (lifting && !memcmp(actual, physicalExpected, 64)) {
    if (physicalPromotion.phase==MachinePhysicalPromotion::Executing) {
      if (!machineCommitVirtual(physicalPromotion.plan())) {
        physicalPromotionFail("confirmed promotion is no longer legal"); return;
      }
      physicalPromotion.reset();
    } else machineCommit(candidateFrom, candidateTo);
    gameAdvanced = true;
    lifting = false; resetHints();
    complete = true;
    physicalAtPosition = true;
    clearBoardLeds(currentBoardType());
    Serial.println("[MACHINE] physical move confirmed");
  }
  bool changed = false;
  for (int sq = 0; sq < 64; ++sq)
    if (actual[sq] != statusFrame[1 + modeBStatusWireIndex(sq % 8, sq / 8 + 1)]) changed = true;
  if (changed || complete) publish(actual, complete);
}

void machineGatewayPoll() {
  if (!selection.machine()) return;
  ensureStarted();
  // BLE S replies use the server's cache, bypassing machineGatewayStatus.
  // Log from the actual runtime loop so a stalled one-shot move stays visible.
  logRuntimeState();
  if (!softwareStepSent && chesslinkServerMachineStatusSent(statusFrame)) {
    softwareStepSent = true;
    softwareStepSentAt = millis();
  }
  uint32_t now = millis();
  if (cableSeen && timing.allowActiveGoStatus() && now - lastStatusAt >= 41)
    sendCable(virtualCableStatus(), 67);
  if (newgameHold) {
    // Rebuild all 64 fields on both hosts even when Phoenix has stopped
    // querying after a finished game. A single UART write is insufficient
    // evidence of a stable new physical-board position.
    if (!selection.board() && cableSeen && delivery.sends < 3 && now-lastStatusAt >= 200)
      sendCable(statusFrame, 67);
    return;
  }
  // A physical robot can finish after Phoenix has stopped sending LED
  // requests. Deliver confirmed replay stages (and their final position)
  // without depending on a new request to open a reply slot. Keep this
  // bounded and exclusive to the Cynus intermediary; virtual-only and
  // ordinary cable timing are unchanged.
  if (selection.board() && (currentBoardType() == BoardType::Cynus || currentBoardType() == BoardType::ChessnutMove) &&
      boardOnline && boardSynchronized && !awaitingStart && cableSeen &&
      (robotConfirmedReplay || (gameAdvanced && !lifting)) &&
      !timing.allowActiveGoStatus() && delivery.sends < 3 &&
      now - lastStatusAt >= 200)
    sendCable(statusFrame, 67);
  const bool ready = cableSeen && chesslinkServerMachineSubscribed() &&
      (!selection.board() || (boardOnline && boardSynchronized && !awaitingStart && (lifting || physicalAtPosition)));
  if (physicalPromotion.negotiating()) {
    physicalPromotionPoll(now,ready); return;
  }
  if (!selection.board() && virtualOperator.active()) {
    virtualPoll(now, ready);
    return;
  }
  if (selection.board() && lifting && !robotConfirmedReplay && ready && !physicalPromotion.active() && now - physicalLedAt >= 500) {
    // Robot driver receives the same stable command until its scan confirms it.
    if (currentBoardType()==BoardType::ChessnutMove) chessnutmoveSetTargetPosition(physicalExpected);
    else dispatchLedFrameToBoard(currentBoardType(), physicalLed);
    physicalLedAt = now;
  }
  if (!ready) softwareStepSent = false;
  if (!ready && pendingLeds.count) {
    pendingLeds.clear();
    Serial.printf("[MACHINE] pending LEDs discarded after connection loss\r\n");
  }
  if (ready && !halted && !lifting && pendingLeds.count &&
      now - lastPositionAt >= 1200 && delivery.ready(now, 500) && softwareStepSent) {
    int from = -1, to = -1;
    const int matches = pendingLeds.resolve(virtualFindMove, from, to);
    if (matches == 1) {
      candidateFrom = from; candidateTo = to; candidateAt = now - 350;
    } else if (matches == 0) {
      // The other host also echoes the move being executed (e.g. e2 while
      // White lifts its pawn). Such feedback is not a promised next move.
      // Keep only usable partial move fields for later blinking endpoints.
      for (unsigned i = 0; i < pendingLeds.count; ++i) {
        const uint64_t mask = pendingLeds.masks[i];
        if (!machineCouldBeMoveFields(mask)) continue;
        hints |= mask;
        for (int sq = 0; sq < 64; ++sq)
          if (mask & (uint64_t(1) << sq)) fieldSeenAt[sq] = now;
        hintAt = now;
      }
      pendingLeds.clear();
      Serial.printf("[MACHINE] queued feedback has no complete move; continuing normal reception\r\n");
    } else {
      halted = true;
      Serial.printf("[MACHINE] STOP: queued LEDs have %s legal continuation\r\n", matches ? "multiple" : "no");
    }
  }
  // Original movement timing; stale software hints are handled at reception.
  const uint32_t movementHoldMs = castlingMotion ? 1000u : 500u;
  if (!ready) { if (!lifting && !chesslinkServerMachineSubscribed()) resetHints(); }
  else if ((!selection.board() || robotConfirmedReplay) && !halted && lifting && delivery.ready(now, movementHoldMs) &&
           softwareStepSent && now - softwareStepSentAt >= movementHoldMs) {
    char before[64], intermediate[64]; machineBoard(before);
    ++movementStage;
    if (movementStage < 3 && machineCastleStage(before, intermediate, candidateFrom, candidateTo, movementStage)) {
      publish(intermediate, false);
      liftedAt = millis();
      Serial.printf("[MACHINE] castle: %s\r\n", movementStage == 1 ? "king placed" : "rook lifted");
    } else if (movementStage == 1 && machineCaptureRemoved(candidateFrom, candidateTo, intermediate)) {
      const int captured = machineCapturedSquare(candidateFrom, candidateTo);
      publish(intermediate, false);
      Serial.printf("[MACHINE] capture: victim removed from %c%d\r\n", 'a' + captured % 8, captured / 8 + 1);
    } else {
    char oldBoard[64]; machineBoard(oldBoard);
    const bool cableMoved = players.accepts(false, machineWhiteToMove());
    if (physicalPromotion.phase==MachinePhysicalPromotion::Executing) {
      if (!machineCommitVirtual(physicalPromotion.plan())) {
        physicalPromotionFail("confirmed promotion is no longer legal"); return;
      }
      physicalPromotion.reset();
    } else machineCommit(candidateFrom, candidateTo);
    gameAdvanced = true;
    lifting = false; resetHints();
    if (robotConfirmedReplay) {
      robotConfirmedReplay = false;
      physicalAtPosition = true;
      clearBoardLeds(currentBoardType());
    }
    char board[64]; machineBoard(board);
    if (!selection.board() && cableMoved) {
      uint64_t fields = 0;
      for (int sq = 0; sq < 64; ++sq) if (oldBoard[sq] != board[sq]) fields |= uint64_t(1) << sq;
      cableEcho.completed(fields);
    }
    publish(board);
    Serial.println("[MACHINE] move completed; position published to both hosts");
    }
  } else if (!halted && !lifting && candidateFrom >= 0 &&
             now - candidateAt >= 350 && now - lastPositionAt >= 1200 &&
             delivery.ready(now, 500) && softwareStepSent && ready) {
    if (pendingLeds.count) {
      Serial.printf("[MACHINE] executing queued move %c%d-%c%d after completed position\r\n",
        'a' + candidateFrom % 8, candidateFrom / 8 + 1, 'a' + candidateTo % 8, candidateTo / 8 + 1);
      pendingLeds.clear();
    }
    if (selection.lockVirtual()) {
      machineGatewayStopBoardSearch();
      Serial.println("[MACHINE] virtual game locked: board radio search stopped until new starting position");
    }
    if (selection.board()) {
      if (!boardOnline || !boardSynchronized || awaitingStart) return;
      const unsigned planCount=machineVirtualPlans(candidateFrom,candidateTo,physicalPromotion.plans,4);
      if (planCount>1) { physicalPromotionStart(planCount); return; }
      if (!machinePreview(candidateFrom, candidateTo, physicalExpected)) return;
      char before[64]; machineBoard(before);
      uint8_t squares[4]; size_t count = 0;
      const bool cynusMove = currentBoardType() == BoardType::Cynus;
      // Cynus's raw LED decoder expects one endpoint pair, including for
      // castling and en passant. Its firmware handles the rook/captured
      // pawn. The full physicalExpected target still confirms ALL changes.
      for (int sq = 0; sq < 64 && count < 4; ++sq)
        if (before[sq] != physicalExpected[sq] &&
            (!cynusMove || sq == candidateFrom || sq == candidateTo)) {
          // Cynus consumes a file-ascending, rank-8-first LED grid.
          // encodeLedFrame produces the opposite cable wire orientation.
          // Adapt only this generated machine command, never host frames
          // or the existing solo driver paths.
          squares[count++] = uint8_t(currentBoardType() == BoardType::Cynus ? 63 - sq : sq);
        }
      encodeLedFrame(squares, count, physicalLed, false);
      lifting = true;
      physicalLedAt = now;
      if (currentBoardType()==BoardType::ChessnutMove) chessnutmoveSetTargetPosition(physicalExpected);
      else dispatchLedFrameToBoard(currentBoardType(), physicalLed);
      Serial.println("[MACHINE] move sent to physical board; waiting for actual position");
      return;
    }
#ifdef CHESSLINK_VIRTUAL_OPERATOR_V2
    if (!virtualStart()) {
      halted = true;
      Serial.println("[VBOARD] STOP: no legal movement plan");
    }
    return;
#else
    // Ordinary virtual moves retain their established timing. Promotions
    // need a source-only figure trial: endpoints do not identify the piece.
    if (machineVirtualPlans(candidateFrom, candidateTo, virtualOperator.plans, 4) > 1) {
      if (!virtualStart()) {
        halted = true;
        Serial.println("[VBOARD] STOP: no legal promotion plan");
      }
      return;
    }
    // Give the unchanged PGN recorder (>600ms settle) each full position.
    char board[64]; machineBoard(board);
    movementStage = 0;
    char preview[64];
    castlingMotion = machineCastleStage(board, preview, candidateFrom, candidateTo, 0);
    board[candidateFrom] = '.';
    lifting = true;
    publish(board, false); liftedAt = now;
#endif
  }
  static uint32_t pressedAt = 0;
  if (digitalRead(9) == LOW) {
    if (!pressedAt) pressedAt = now;
    if (!halted && !lifting && now - pressedAt >= 1500 && now - lastPositionAt >= 1200) {
      halted = true; resetHints(); machineSaveAndExport();
      Serial.println("[MACHINE] stopped and saved; reboot for a new game");
    }
  } else pressedAt = 0;
}

#endif
