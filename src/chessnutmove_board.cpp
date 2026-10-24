#include "chessnutmove_board.h"
#include "gw_serial.h"  // keep last: Serial -> log-recording wrapper

// Confirmed via the community chessnut-move-rs port (daymxn/chessnut-move-rs,
// DEVICE_NAME constant) since Chessnut's own official EasyLinkSDK doesn't
// cover the Move board at all (only Air/Pro/Evo/Go, the passive sensor
// family) -- user's own question 2026-10-06, "wird das grossgeschrieben?":
// yes, advertised as "CHESSNUT-MOVE". Still matched case-insensitively (see
// containsCaseInsensitive() in main.cpp), and must be registered BEFORE the
// generic kChessnutBoardName substring match in main.cpp's kKnownBoards[],
// since "chessnut-move" also contains "chessnut" and would otherwise be
// caught by the plain (non-robotic) Chessnut driver instead.
const char kChessnutMoveBoardName[] = "CHESSNUT-MOVE";
const char kChessnutMoveBoardNameAlt[] = "chessnut move";  // space variant, defensive

// Shared with every other board driver, defined once in chessnut_board.cpp
// -- NOT redefined here, since both .cpp files link into the same firmware
// image and a second definition would be a duplicate-symbol link error.
extern bool phoenixHasActiveLedSuggestion;

namespace {

constexpr char kWriteUuid[] = "1b7e8272-2877-41c3-b46e-cf057c562023";
constexpr char kReadMiscUuid[] = "1b7e8273-2877-41c3-b46e-cf057c562023";
constexpr char kReadBoardUuid[] = "1b7e8262-2877-41c3-b46e-cf057c562023";
constexpr size_t kMaxNotifyPayload = 48;
constexpr uint32_t kConnectTimeoutMs = 8000;

struct RawPacket {
  uint8_t length;
  uint8_t data[kMaxNotifyPayload];
};

NimBLEClient* bleClient = nullptr;
NimBLERemoteCharacteristic* writeChar = nullptr;
NimBLERemoteCharacteristic* miscChar = nullptr;
NimBLERemoteCharacteristic* boardChar = nullptr;
QueueHandle_t boardDataQueue = nullptr;
QueueHandle_t miscDataQueue = nullptr;

uint8_t lastRawBoardData[32] = {};
bool haveLastRawBoardData = false;
uint32_t lastInitialStatusRequestMs = 0;

// Local "show whatever currently differs from the last settled position"
// LED fallback -- added 2026-08-31 at the user's explicit direction, after
// finding Chessnut has no such feature of its own (unlike, per the user's
// direct observation, a real Millennium/Supreme T2 board, which circles
// every lifted piece with its own local LEDs regardless of what Phoenix's
// 'L' frame currently contains). Purely a physical-board display aid for
// the player -- does NOT touch anything sent to Phoenix over the cable.
//
// Priority note (confirmed on real hardware 2026-08-31): this used to defer
// to Phoenix's own last-decoded 'L' frame whenever that had something to
// show. That's backwards -- during a real capture, Phoenix can still be
// blinking a stale suggestion from before the capture started, and that
// silently ate the "piece missing" indicator during exactly the moment it
// mattered most. A real physical discrepancy must always win; see
// chessnutmoveHasLocalDeviation() in board_driver.cpp's dispatchLedFrameToBoard().
// Steady/on, never blinking -- Chessnut's own LED command has no time-based
// flash pattern support (unlike Mode-B's 'L' slot-time field).
char currentBoard[64] = {};
char settledBoard[64] = {};
bool haveSettledBoard = false;
uint32_t lastBoardChangeMs = 0;
bool ledSettlePending = false;
uint8_t deferredLedFrame[167] = {};
bool haveDeferredLedFrame = false;
int selectionQueenWireIndex = -1;
// A short debounce, not a multi-second one -- this is a real-time board,
// per the user's explicit correction. 500ms is enough to bridge a piece
// being physically slid across adjacent squares (each square's sensor
// triggering its own brief transient along the path) without accumulating
// a whole multi-step move; it is NOT meant to hold captures together the
// way earlier (reverted) attempts at delaying the actual cable status did.
constexpr uint32_t kLedSettleMs = 500;

void sendCableStatusFrame() {
  uint8_t frame[kModeBStatusFrameLength] = {};
  frame[0] = 's';
  memcpy(frame + 1, currentBoard, 64);
  // Plain checksum, not cableHostUsesEncodedChecksum -- found 2026-08-31 by
  // reading Elfacun/Diablillo's own proven-working source directly
  // (github.com/inmbolmie/elfacun, board_b.cpp): a real/reference Mode-B
  // board ALWAYS computes its own outgoing checksum over the plain 7-bit
  // content, then applies odd-parity encoding to the whole frame
  // (including the checksum digits) as a separate final step -- it never
  // computes the checksum over already-parity-encoded bytes, regardless of
  // what convention the connected host's own frames validate under.
  // cableHostUsesEncodedChecksum only describes how to interpret Phoenix's
  // OWN incoming frames (modeBValidBlock()'s dual check); it was being
  // reused here to select the outgoing convention too, which is wrong.
  // This exactly explains the 16/32-piece mirror-symmetric registration
  // pattern found this session: '.' and most pieces need their own parity
  // bit for odd-parity encoding, but the queen and rook characters happen
  // not to (verified by hand for every piece letter) -- computing the
  // checksum over the already-encoded bytes silently flips the checksum's
  // own top bit exactly when a queen/rook-vs-everything-else transition
  // changes the frame's total parity-bit count by one, producing a
  // checksum Phoenix correctly rejects as corrupt. A paired transition
  // (a normal move) always nets zero change here, which is also exactly
  // why two-square updates never showed this symptom.
  computeModeBChecksumHex(frame + 65, frame, 65, /*useEncodedConvention=*/false);
  onBoardStatusFrame(frame);
}

size_t collectLocalDeviations(SquareHighlight* deviations, size_t maxOut) {
  size_t count = 0;
  for (int wireIndex = 0; wireIndex < 64 && count < maxOut; ++wireIndex) {
    if (currentBoard[wireIndex] == settledBoard[wireIndex]) continue;
    const int rank = wireIndex / 8 + 1;
    const int file0 = 7 - (wireIndex % 8);
    deviations[count].squareIndex =
        static_cast<uint8_t>(boardSquareIndex(static_cast<char>('a' + file0), rank));
    deviations[count].role = SquareHighlightRole::Generic;
    ++count;
  }
  return count;
}

void showLocalBoardDeviations() {
  SquareHighlight deviations[32];
  const size_t count = collectLocalDeviations(deviations, 32);
  chessnutmoveSetHighlightedSquares(count > 0 ? deviations : nullptr, count);
}

// Chessnut's own piece codes: empty square = 0, white uppercase, black
// lowercase. Confirmed against two independent reverse-engineering projects.
char pieceFromChessnutCode(uint8_t code) {
  static constexpr char kLut[] = {
      '.', 'q', 'k', 'b', 'p', 'n', 'R', 'P', 'r', 'B', 'N', 'Q', 'K'};
  return (code < sizeof(kLut)) ? kLut[code] : '.';
}

// Counts BLE board-data notifications lost because boardDataQueue was full
// when they arrived -- added 2026-08-31 to test the theory that "sometimes
// yes, sometimes no" status updates for a given square (confirmed on real
// hardware not to be a Chessnut sensor problem -- the user's own words:
// "das Board funktioniert mit allen 100%") are actually being silently
// dropped here rather than lost upstream. xQueueSend()'s return value was
// previously discarded entirely, so a full queue failed with zero trace.
uint32_t droppedBoardPackets = 0;

void pushToQueue(QueueHandle_t queue, uint8_t* data, size_t length) {
  if (queue == nullptr || length == 0) return;
  RawPacket packet{};
  packet.length = static_cast<uint8_t>(min(length, kMaxNotifyPayload));
  memcpy(packet.data, data, packet.length);
  if (xQueueSend(queue, &packet, 0) != pdTRUE && queue == boardDataQueue) {
    ++droppedBoardPackets;
  }
}

void onBoardNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  pushToQueue(boardDataQueue, data, length);
}

void onMiscNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  pushToQueue(miscDataQueue, data, length);
}

class ClientCallbacks final : public NimBLEClientCallbacks {
 public:
  void onConnect(NimBLEClient*) override {}
  void onDisconnect(NimBLEClient*, int) override {
    writeChar = nullptr;
    miscChar = nullptr;
    boardChar = nullptr;
    haveLastRawBoardData = false;
    Serial.println("BLE disconnected; reconnecting automatically.");
  }
};

NimBLERemoteCharacteristic* findCharacteristicInAnyService(NimBLEClient* client, const char* uuid) {
  for (NimBLERemoteService* service : client->getServices()) {
    NimBLERemoteCharacteristic* characteristic = service->getCharacteristic(uuid);
    if (characteristic != nullptr) return characteristic;
  }
  return nullptr;
}

void handleBoardDataPacket(const uint8_t* data, size_t length) {
  if (length < 34 || data[0] != 0x01 || data[1] != 0x24) return;
  const uint8_t* raw = data + 2;  // 32 bytes, 2 squares per byte
  if (haveLastRawBoardData && memcmp(lastRawBoardData, raw, 32) == 0) return;
  memcpy(lastRawBoardData, raw, 32);
  haveLastRawBoardData = true;
  // A new position supersedes any suggestion for the previous snapshot.
  haveDeferredLedFrame = false;

  memset(currentBoard, '.', sizeof(currentBoard));
  for (int byteIdx = 0; byteIdx < 32; ++byteIdx) {
    const uint8_t lowerCode = raw[byteIdx] & 0x0f;
    const uint8_t upperCode = raw[byteIdx] >> 4;
    for (int half = 0; half < 2; ++half) {
      const int rawIndex = byteIdx * 2 + half;  // 0..63, H8..A8,...,H1..A1
      const int rank = 8 - (rawIndex / 8);
      const int file0 = 7 - (rawIndex % 8);
      const uint8_t code = (half == 0) ? lowerCode : upperCode;
      currentBoard[modeBStatusWireIndex(file0, rank)] = pieceFromChessnutCode(code);
    }
  }

  if (selectionQueenWireIndex >= 0 && currentBoard[selectionQueenWireIndex] == '.') {
    settledBoard[selectionQueenWireIndex] = '.';
    selectionQueenWireIndex = -1;
    Serial.println("[CHESSNUT LED] selection queen removed; cleared local baseline square");
  }

  sendCableStatusFrame();
  armVerboseCableLog();

  if (!haveSettledBoard) {
    memcpy(settledBoard, currentBoard, sizeof(settledBoard));
    haveSettledBoard = true;
  }
  lastBoardChangeMs = millis();
  ledSettlePending = true;
  showLocalBoardDeviations();
}

void handleMiscPacket(const uint8_t* data, size_t length) {
  if (length < 3) return;
  if (data[0] == 0x0f && data[1] == 0x01) {
    Serial.printf("[CHESSNUT] button pressed: %u\r\n", data[2]);
  }
}

}  // namespace

void chessnutmoveShowLocalBoardDeviations() { showLocalBoardDeviations(); }

bool chessnutmoveHasLocalDeviation() {
  SquareHighlight deviations[1];
  return collectLocalDeviations(deviations, 1) > 0;
}

uint32_t chessnutmoveDroppedBoardPackets() { return droppedBoardPackets; }

bool chessnutmoveIsConnected() {
  return bleClient != nullptr && bleClient->isConnected() && writeChar != nullptr;
}

bool chessnutmoveConnect(const NimBLEAddress& address) {
  if (bleClient == nullptr) {
    bleClient = NimBLEDevice::createClient();
    bleClient->setClientCallbacks(new ClientCallbacks(), true);
    bleClient->setConnectTimeout(kConnectTimeoutMs);
  }
  if (boardDataQueue == nullptr) boardDataQueue = xQueueCreate(16, sizeof(RawPacket));
  if (miscDataQueue == nullptr) miscDataQueue = xQueueCreate(16, sizeof(RawPacket));
  if (boardDataQueue == nullptr || miscDataQueue == nullptr) return false;
  // The previous connection is closed and the main loop is gated during
  // setup. Discard old notifications before subscribing to the new link.
  xQueueReset(boardDataQueue);
  xQueueReset(miscDataQueue);

  // Request a fast connection interval before connecting, matching
  // millennium_board.cpp's own client-side setConnectionParams() -- added
  // 2026-08-31, never previously ported here. This exact fix (15-30ms
  // interval, zero slave latency, 2s supervision timeout) was proven
  // critical for King/T2's reliable status delivery earlier in this
  // project's history (see project memory: "BREAKTHROUGH: King progressed
  // to a third move for the first time ever", 2026-08-27) -- a slower
  // default interval let notifications queue up and arrive coalesced/
  // delayed rather than as separate timely updates. Chessnut never got this
  // fix; worth testing given T2 (which has it) captures correctly and
  // Chessnut (which doesn't) has never once registered a capture with
  // Phoenix despite every content/timing variant tried on the cable side.
  bleClient->setConnectionParams(12, 24, 0, 200);

  Serial.printf("Connecting to %s ...\r\n", address.toString().c_str());
  if (!bleClient->connect(address)) {
    Serial.println("BLE connection failed.");
    return false;
  }

  if (bleClient->getServices(true).empty()) {
    Serial.println("Chessnut BLE service discovery returned nothing.");
    bleClient->disconnect();
    return false;
  }

  writeChar = findCharacteristicInAnyService(bleClient, kWriteUuid);
  boardChar = findCharacteristicInAnyService(bleClient, kReadBoardUuid);
  miscChar = findCharacteristicInAnyService(bleClient, kReadMiscUuid);
  if (writeChar == nullptr || boardChar == nullptr) {
    Serial.println("Chessnut BLE characteristics not found.");
    writeChar = nullptr;
    bleClient->disconnect();
    return false;
  }

  if (!boardChar->canNotify() || !boardChar->subscribe(true, onBoardNotify)) {
    Serial.println("Could not enable Chessnut board notifications.");
    writeChar = nullptr;
    bleClient->disconnect();
    return false;
  }
  if (miscChar != nullptr && miscChar->canNotify()) {
    miscChar->subscribe(true, onMiscNotify);
  }

  Serial.println("Chessnut BLE gateway connected.");
  resetKingLedFrameBaseline();
  haveLastRawBoardData = false;
  haveSettledBoard = false;
  selectionQueenWireIndex = -1;
  ledSettlePending = false;
  haveDeferredLedFrame = false;

  static constexpr uint8_t initCommand[] = {0x21, 0x01, 0x00};
  if (writeChar == nullptr || !writeChar->writeValue(initCommand, sizeof(initCommand), true)) {
    Serial.println("[CHESSNUT] real-time initialization failed; retrying connection");
    bleClient->disconnect();
    return false;
  }
  lastInitialStatusRequestMs = millis();
  return chessnutmoveIsConnected();
}

void chessnutmoveDeferLedFrame(const uint8_t frame167[167]) {
  memcpy(deferredLedFrame, frame167, sizeof(deferredLedFrame));
  haveDeferredLedFrame = true;
}

void chessnutmoveTrackSelectionQueen(int mode) {
  if (mode == 0 || mode == 1) {
    selectionQueenWireIndex = modeBStatusWireIndex(mode, 4);
  }
}

void chessnutmoveSetHighlightedSquares(const SquareHighlight* squares, size_t count) {
  if (!chessnutmoveIsConnected()) return;
  uint8_t command[10] = {0x0a, 0x08, 0, 0, 0, 0, 0, 0, 0, 0};
  for (size_t i = 0; i < count; ++i) {
    const int file0 = squares[i].squareIndex % 8;  // 0-based a..h
    const int rank = squares[i].squareIndex / 8 + 1;
    const uint8_t fileBit = static_cast<uint8_t>(0x80 >> file0);  // a=0x80 .. h=0x01
    command[2 + (8 - rank)] |= fileBit;
  }
  // Write-with-response. A write-without-response variant was tried
  // 2026-08-31 (theory: the blocking round-trip here starves the cable's
  // incoming UART read of CPU time, risking corrupted bytes from Phoenix)
  // but the very next real-hardware test hung the whole device on boot --
  // no serial output at all, persisting through a full USB unplug/replug,
  // unrelated to the earlier 2026-08-30 attempt's own regression. Reverted
  // immediately without a confirmed root cause; do not retry without
  // isolating why it hung first.
  if (!writeChar->writeValue(command, sizeof(command), true)) {
    Serial.println("[CHESSNUT LED] BLE write failed");
  }
}

void chessnutmovePoll() {
  if (!chessnutmoveIsConnected()) return;

  RawPacket packet{};
  while (xQueueReceive(boardDataQueue, &packet, 0) == pdTRUE) {
    handleBoardDataPacket(packet.data, packet.length);
  }
  while (xQueueReceive(miscDataQueue, &packet, 0) == pdTRUE) {
    handleMiscPacket(packet.data, packet.length);
  }

  // Retry only until this connection delivers its first position. A board
  // that missed initialization must not require moving a piece to recover.
  if (!haveLastRawBoardData && chessnutmoveIsConnected() &&
      static_cast<uint32_t>(millis() - lastInitialStatusRequestMs) >= 2000) {
    static constexpr uint8_t initCommand[] = {0x21, 0x01, 0x00};
    lastInitialStatusRequestMs = millis();
    Serial.println("[CHESSNUT] waiting for fresh position; retrying real-time request");
    if (!writeChar->writeValue(initCommand, sizeof(initCommand), true)) {
      bleClient->disconnect();
      return;
    }
  }

  // Once nothing has changed for kLedSettleMs, decide whether to accept the
  // current position as the new confirmed baseline. Accept it ONLY if at
  // least one square shows a piece that wasn't there in the old baseline
  // (an "arrival" -- covers both empty->occupied and a piece swap sampled
  // in one snapshot). That single event resolves the whole outstanding
  // diff at once: a normal move's origin (now empty) clears together with
  // its destination (now occupied) the moment the destination settles --
  // matching "the piece is back or stands elsewhere" (the user's own
  // words). If NOTHING arrived -- a piece was only ever removed, nothing
  // placed anywhere -- do NOT accept, no matter how long it's been quiet:
  // confirmed on real hardware 2026-08-31 that unconditionally accepting
  // after the timeout made a still-missing piece's warning self-clear
  // after 500ms ("blinkt einmal, dann nichts"), which is exactly the
  // moment a real capture needs it to keep showing.
  if (ledSettlePending &&
      static_cast<uint32_t>(millis() - lastBoardChangeMs) >= kLedSettleMs) {
    bool pieceArrived = false;
    for (int wireIndex = 0; wireIndex < 64; ++wireIndex) {
      if (currentBoard[wireIndex] != settledBoard[wireIndex] &&
          currentBoard[wireIndex] != '.') {
        pieceArrived = true;
        break;
      }
    }
    if (pieceArrived) {
      memcpy(settledBoard, currentBoard, sizeof(settledBoard));
      if (haveDeferredLedFrame) {
        haveDeferredLedFrame = false;
        Serial.println("[CHESSNUT LED] restoring deferred host frame after settle");
        dispatchLedFrameToBoard(BoardType::ChessnutMove, deferredLedFrame);
      } else {
        showLocalBoardDeviations();
      }
    }
    ledSettlePending = false;
  }
}

// --- Robotic move execution -------------------------------------------
// Wire format verified against daymxn/chessnut-move-rs (2026-10-06); see
// the long comment on chessnutmoveExecuteHighlightedMove() in the header.
//
// Tracked independently of currentBoard[]/settledBoard[] above (those are
// Chessnut Air's own raw sensor-scan state, in that hardware's own wire
// order -- NOT confirmed to be canonical a1..h8, so reusing them here would
// risk silently sending a move to/from the wrong square). moveBoard[] is
// this driver's own canonical a1=0..h8=63 position, maintained purely by
// replaying the highlighted-move stream from a standard start position --
// same approach cynus_board.cpp's own board64[] takes.
namespace {
char moveBoard[64] = {
    'R','N','B','Q','K','B','N','R',  // a1..h1
    'P','P','P','P','P','P','P','P',  // a2..h2
    '.','.','.','.','.','.','.','.',
    '.','.','.','.','.','.','.','.',
    '.','.','.','.','.','.','.','.',
    '.','.','.','.','.','.','.','.',
    'p','p','p','p','p','p','p','p',  // a7..h7
    'r','n','b','q','k','b','n','r',  // a8..h8
};
constexpr uint32_t kAutoMoveBusyTimeoutMs = 4000;  // "under 3s" per vendor spec + margin
uint32_t lastMoveSentMs = 0;
bool moveBusy = false;

uint8_t pieceNibble(char piece) {
  switch (piece) {
    case 'q': return 0x1; case 'k': return 0x2; case 'b': return 0x3;
    case 'p': return 0x4; case 'n': return 0x5; case 'r': return 0x8;
    case 'R': return 0x6; case 'P': return 0x7; case 'B': return 0x9;
    case 'N': return 0xa; case 'Q': return 0xb; case 'K': return 0xc;
    default: return 0x0;  // '.' or anything unrecognized -> empty
  }
}

// 35-byte auto_move command: [0x42,0x21] prefix + 32-byte packed position
// (wire slot = 63 - canonicalIndex, two 4-bit squares per byte, low nibble
// first) + 1 mode byte.
void encodeAutoMoveCommand(const char board[64], uint8_t out[35]) {
  out[0] = 0x42;
  out[1] = 0x21;
  for (int wireSlot = 0; wireSlot < 64; wireSlot += 2) {
    const uint8_t lowNibble = pieceNibble(board[63 - wireSlot]);
    const uint8_t highNibble = pieceNibble(board[63 - (wireSlot + 1)]);
    out[2 + wireSlot / 2] = lowNibble | (highNibble << 4);
  }
  out[34] = 0x01;  // AutoMoveMode::Normal
}

bool sendAutoMoveCommand(const char board[64]) {
  if (writeChar == nullptr) return false;
  uint8_t command[35];
  encodeAutoMoveCommand(board, command);
  if (!writeChar->writeValue(command, sizeof(command), true)) {
    Serial.println("[CHESSNUT-MOVE] auto_move BLE write failed");
    return false;
  }
  lastMoveSentMs = millis();
  moveBusy = true;
  return true;
}

}  // namespace

void chessnutmoveExecuteHighlightedMove(const SquareHighlight* highlights, size_t count) {
  if (chessnutmoveIsBusyMoving()) {
    Serial.println("[CHESSNUT-MOVE] highlighted-move command ignored; still busy with previous move");
    return;
  }
  if (count == 4) {
    // Castling, same detection as cynus_board.cpp's cynusExecuteHighlightedMove():
    // all 4 squares (king source/dest + rook source/dest) arrive together;
    // find the king, then the empty destination two files away on its rank.
    int kingSquare = -1;
    for (size_t i = 0; i < 4; ++i) {
      const char piece = moveBoard[highlights[i].squareIndex];
      if (piece == 'K' || piece == 'k') {
        if (kingSquare != -1) { kingSquare = -1; break; }
        kingSquare = highlights[i].squareIndex;
      }
    }
    if (kingSquare == -1) {
      Serial.println("[CHESSNUT-MOVE] 4-square highlight doesn't look like castling -- ignored");
      return;
    }
    const int kingRank = kingSquare / 8, kingFile = kingSquare % 8;
    int destSquare = -1;
    for (size_t i = 0; i < 4; ++i) {
      const int sq = highlights[i].squareIndex;
      if (sq == kingSquare || moveBoard[sq] != '.') continue;
      const int rank = sq / 8, file = sq % 8;
      if (rank == kingRank && abs(file - kingFile) == 2) { destSquare = sq; break; }
    }
    if (destSquare == -1) {
      Serial.println("[CHESSNUT-MOVE] 4-square highlight doesn't look like castling -- ignored");
      return;
    }
    const bool kingside = destSquare > kingSquare;
    const int rookFrom = kingRank * 8 + (kingside ? 7 : 0);
    const int rookTo = kingside ? destSquare - 1 : destSquare + 1;
    moveBoard[destSquare] = moveBoard[kingSquare]; moveBoard[kingSquare] = '.';
    moveBoard[rookTo] = moveBoard[rookFrom]; moveBoard[rookFrom] = '.';
    Serial.println("[CHESSNUT-MOVE] highlighted-move decoded as castling -- sending auto_move");
    sendAutoMoveCommand(moveBoard);
    return;
  }
  if (count != 2) {
    Serial.printf("[CHESSNUT-MOVE] highlighted-move command has %u squares, expected 2 or 4 -- ignored\r\n",
                  static_cast<unsigned>(count));
    return;
  }
  // Plain move (2 squares): the occupied one is the source, the other the
  // destination. En passant/promotion piece choice aren't distinguishable
  // from just two highlighted squares -- promotion always resolves to a
  // queen here, same simplification as elsewhere in this project.
  int fromSquare = -1, toSquare = -1;
  if (moveBoard[highlights[0].squareIndex] != '.') { fromSquare = highlights[0].squareIndex; toSquare = highlights[1].squareIndex; }
  else if (moveBoard[highlights[1].squareIndex] != '.') { fromSquare = highlights[1].squareIndex; toSquare = highlights[0].squareIndex; }
  if (fromSquare == -1) {
    Serial.println("[CHESSNUT-MOVE] neither highlighted square is occupied -- ignored");
    return;
  }
  char movingPiece = moveBoard[fromSquare];
  const bool isPawn = (movingPiece == 'P' || movingPiece == 'p');
  const int toRank = toSquare / 8;
  if (isPawn && (toRank == 0 || toRank == 7)) movingPiece = (movingPiece == 'P') ? 'Q' : 'q';
  // En passant: pawn moves diagonally into an empty square -> the captured
  // pawn is on the FROM rank, TO file.
  if (isPawn && moveBoard[toSquare] == '.' && (fromSquare % 8) != (toSquare % 8)) {
    moveBoard[(fromSquare / 8) * 8 + (toSquare % 8)] = '.';
  }
  moveBoard[toSquare] = movingPiece;
  moveBoard[fromSquare] = '.';
  Serial.println("[CHESSNUT-MOVE] highlighted-move decoded -- sending auto_move");
  sendAutoMoveCommand(moveBoard);
}

bool chessnutmoveIsBusyMoving() {
  if (moveBusy && uint32_t(millis() - lastMoveSentMs) >= kAutoMoveBusyTimeoutMs) moveBusy = false;
  return moveBusy;
}
