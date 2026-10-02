#include "machine_gateway.h"
#ifdef CHESSLINK_ENABLE_MACHINE
#include "machine/machine.h"
#include "machine/castle_motion.h"
#include "machine/delivery_gate.h"
#include "machine/pending_leds.h"
#include "machine/players.h"
#include "chesslink_server.h"
#include "pgn_recorder.h"
#include "cable_status_policy.h"

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
uint64_t lastMoveFields = 0;
uint32_t conflictStartedAt = 0, conflictLastAt = 0;

size_t sendCable(const uint8_t* frame, size_t n) {
  const size_t written = machineGatewayWriteCable(frame, n);
  lastStatusAt = millis();
  if (n == 67 && frame[0] == 's' && written == n && memcmp(frame, statusFrame, n) == 0) {
    if (!delivery.sends) Serial.printf("[MACHINE TX] stage=%d status=%.*s\r\n", movementStage, int(n), frame);
    delivery.sent(millis());
  }
  return written;
}

bool observeCableConflict(uint64_t mask) {
  // Sustained indications on the just-moved pieces can be a correction
  // request. They are not a position ACK; stop conservatively for inspection.
  if (lifting || halted) return halted;
  const uint32_t now = millis();
  if (machineCouldBeMoveFields(mask)) {
    conflictStartedAt = conflictLastAt = 0;
    return false;
  }
  if (mask && (mask & lastMoveFields) && !(mask & ~lastMoveFields)) {
    if (!conflictStartedAt || now - conflictLastAt > 2500) conflictStartedAt = now;
    conflictLastAt = now;
    if (now - conflictStartedAt >= 5000) {
      halted = true;
      Serial.printf("[MACHINE] STOP: persistent LED correction on previous move; inspect host position\r\n");
    }
  }
  return halted;
}
uint32_t validCommands = 0, invalidCommands = 0;

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
void led(bool white, const uint8_t* frame) {
  if (newgameHold) return;
  // After an explicit New Game, discard old host highlights until the actual
  // board has been rebuilt. Initial board discovery still retains an opening.
  if (awaitingStart) return;
  SquareHighlight feedback[64];
  const size_t feedbackCount = decodeKingLedFrame(frame, feedback, 64);
  uint64_t feedbackMask = 0;
  for (size_t i = 0; i < feedbackCount; ++i) feedbackMask |= uint64_t(1) << feedback[i].squareIndex;
  if (feedbackMask == UINT64_MAX) {
    logLed(white, frame, "all-board-initialization", feedbackMask, -1);
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
  if (!selection.board() && players.assigned() && !white && observeCableConflict(feedbackMask)) return;
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
    const int matches = machineFindMove(opening, from, to);
    logLed(white, frame, "opening", opening, matches);
    if (matches != 1) return;
    players.assignFirst(white);
    resetHints();
    mask = opening;
    Serial.printf("[MACHINE] colours: software=%s cable=%s\r\n",
      white ? "white" : "black", white ? "black" : "white");
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
  const int matches = machineFindMove(hints, from, to);
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
    case 'S': sendCable(statusFrame, 67); break;
    case 'V': reply((const uint8_t*)"v0100", 5); break;
    case 'X': players.clearOpening(false); if (!lifting && players.accepts(false, machineWhiteToMove())) resetHints(); reply((const uint8_t*)"x", 1); break;
    // Cable-side King/module reset: preserve its established behavior.
    case 'T': machineGatewayResetGame(); break;
    case 'L':
      reply((const uint8_t*)"l", 1);
      sendCable(statusFrame, 67);
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
  players.clearOpening(true);
  if (!lifting && players.accepts(true, machineWhiteToMove())) resetHints();
}
static void restartMachineGame(bool resetConfiguration) {
  if (!selection.machine()) return;
  ensureStarted();
  // Save only complete positions. An interrupted lift must never enter PGN.
  // Repeated startup resets with no opening do not create empty games.
  if (players.assigned() || lifting || halted) machineRecorderRestart();
  machineReset();
  players.reset(); pendingLeds.clear(); resetHints();
  halted = lifting = castlingMotion = robotConfirmedReplay = false;
  movementStage = 0;
  liftedAt = hintAt = candidateAt = softwareStepSentAt = 0;
  lastMoveFields = 0; conflictStartedAt = conflictLastAt = 0;
  if (resetConfiguration) {
    memset(registers, 0, sizeof(registers));
    registers[1] = 0x14; registers[4] = 0x0f;
  }
  gameAdvanced = false;
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
  if (!machineGatewayNewgameAllowed()) return false;
  newgameHold = true;
  restartMachineGame(false);
  return true;
}
bool machineGatewayNewgamePublished() {
  if (!machineGatewayNewgameAllowed()) return false;
  char board[64]; machineBoard(board);
  return !memcmp(board, startingBoard, 64) && delivery.sends > 0 &&
         chesslinkServerMachineStatusSent(statusFrame);
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
  if (robotConfirmedReplay) return; // Do not replace an in-progress confirmed replay.
  bool complete = false;
  if (lifting && !memcmp(actual, physicalExpected, 64) && currentBoardType() == BoardType::Cynus) {
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
    machineCommit(candidateFrom, candidateTo);
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
  if (!softwareStepSent && chesslinkServerMachineStatusSent(statusFrame)) {
    softwareStepSent = true;
    softwareStepSentAt = millis();
  }
  uint32_t now = millis();
  if (cableSeen && timing.allowActiveGoStatus() && now - lastStatusAt >= 41)
    sendCable(statusFrame, 67);
  if (newgameHold) return;
  // A physical robot can finish after Phoenix has stopped sending LED
  // requests. Deliver confirmed replay stages (and their final position)
  // without depending on a new request to open a reply slot. Keep this
  // bounded and exclusive to the Cynus intermediary; virtual-only and
  // ordinary cable timing are unchanged.
  if (selection.board() && currentBoardType() == BoardType::Cynus &&
      boardOnline && boardSynchronized && !awaitingStart && cableSeen &&
      (robotConfirmedReplay || (gameAdvanced && !lifting)) &&
      !timing.allowActiveGoStatus() && delivery.sends < 3 &&
      now - lastStatusAt >= 200)
    sendCable(statusFrame, 67);
  const bool ready = cableSeen && chesslinkServerMachineSubscribed() &&
      (!selection.board() || (boardOnline && boardSynchronized && !awaitingStart && (lifting || physicalAtPosition)));
  if (selection.board() && lifting && !robotConfirmedReplay && ready && now - physicalLedAt >= 500) {
    // Robot driver receives the same stable command until its scan confirms it.
    dispatchLedFrameToBoard(currentBoardType(), physicalLed);
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
    const int matches = pendingLeds.resolve(machineFindMove, from, to);
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
  if (!ready) { if (!lifting && !chesslinkServerMachineSubscribed()) resetHints(); }
  else if ((!selection.board() || robotConfirmedReplay) && !halted && lifting && delivery.ready(now, castlingMotion ? 1000 : 500) &&
           softwareStepSent && now - softwareStepSentAt >= (castlingMotion ? 1000u : 500u)) {
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
    machineCommit(candidateFrom, candidateTo);
    gameAdvanced = true;
    lifting = false; resetHints();
    if (robotConfirmedReplay) {
      robotConfirmedReplay = false;
      physicalAtPosition = true;
      clearBoardLeds(currentBoardType());
    }
    char board[64]; machineBoard(board);
    lastMoveFields = 0; conflictStartedAt = conflictLastAt = 0;
    for (int sq = 0; sq < 64; ++sq) if (oldBoard[sq] != board[sq]) lastMoveFields |= uint64_t(1) << sq;
    publish(board);
    Serial.println("[MACHINE] move completed; position published to both hosts");
    }
  } else if (!halted && !lifting && candidateFrom >= 0 &&
             now - candidateAt >= 350 && now - lastPositionAt >= 1200 &&
             delivery.ready(now, 500) && softwareStepSent && ready) {
    // Do not execute a proposed next move while recent correction LEDs persist.
    if (conflictStartedAt && now - conflictLastAt < 2500) { delay(1); return; }
    if (pendingLeds.count) {
      Serial.printf("[MACHINE] executing queued move %c%d-%c%d after completed position\r\n",
        'a' + candidateFrom % 8, candidateFrom / 8 + 1, 'a' + candidateTo % 8, candidateTo / 8 + 1);
      pendingLeds.clear();
    }
    if (selection.lockVirtual()) {
      machineGatewayStopBoardSearch();
      Serial.println("[MACHINE] virtual game locked: board radio search stopped until reboot");
    }
    if (selection.board()) {
      if (!boardOnline || !boardSynchronized || awaitingStart) return;
      if (!machinePreview(candidateFrom, candidateTo, physicalExpected)) return;
      char before[64]; machineBoard(before);
      uint8_t squares[4]; size_t count = 0;
      for (int sq = 0; sq < 64 && count < 4; ++sq)
        if (before[sq] != physicalExpected[sq]) {
          // Cynus consumes a file-ascending, rank-8-first LED grid.
          // encodeLedFrame produces the opposite cable wire orientation.
          // Adapt only this generated machine command, never host frames
          // or the existing solo driver paths.
          squares[count++] = uint8_t(currentBoardType() == BoardType::Cynus ? 63 - sq : sq);
        }
      encodeLedFrame(squares, count, physicalLed, false);
      lifting = true;
      physicalLedAt = now;
      dispatchLedFrameToBoard(currentBoardType(), physicalLed);
      Serial.println("[MACHINE] move sent to physical board; waiting for actual position");
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
