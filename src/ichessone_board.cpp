#include "ichessone_board.h"

#include <Preferences.h>
#include "gw_serial.h"  // keep last: Serial -> log-recording wrapper

const char kIChessOneBoardName[] = "iChessOne";

void ichessoneResetLedState();  // defined with the LED output code below

namespace {

constexpr char kWriteUuid[] = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char kNotifyUuid[] = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr uint32_t kConnectTimeoutMs = 8000;
constexpr size_t kMaxNotifyPayload = 48;
constexpr size_t kPositionMessageLength = 34;  // '=' 'p' + 32 raw bytes

struct RawPacket {
  uint8_t length;
  uint8_t data[kMaxNotifyPayload];
};

NimBLEClient* bleClient = nullptr;
NimBLERemoteCharacteristic* writeChar = nullptr;
NimBLERemoteCharacteristic* notifyChar = nullptr;
QueueHandle_t notifyQueue = nullptr;

char currentBoard[64] = {};  // Mode-B wire order, same convention sendCableStatusFrame() expects

// Reassembly buffer for the split "=p<32 bytes>" position message -- see
// this file's own header comment for why this is needed (34 bytes exceeds
// the doc's own default 20-byte notify payload, so the board splits it).
uint8_t positionAssembly[kPositionMessageLength];
size_t positionAssemblyLen = 0;

// iChessOne's own piece codes (vendor doc's value table): 0=. 1=P 2=N 3=B
// 4=R 5=Q 6=K 7=p 8=n 9=b A=r B=q C=k -- already this project's own letter
// convention (uppercase white, lowercase black, '.' empty), pure lookup.
char pieceFromICode(uint8_t code) {
  static constexpr char kLut[] = {
      '.', 'P', 'N', 'B', 'R', 'Q', 'K', 'p', 'n', 'b', 'r', 'q', 'k'};
  return (code < sizeof(kLut)) ? kLut[code] : '.';
}

// LED settings gestures (2026-10-12, user's own spec): on the starting position plus ONE extra white queen,
// the square of that queen picks the LED colour (g file) or brightness (h file), like the queen gesture
// a4/b4 picks the BLE protocol:
//   g3 red (default)  g4 blue  g5 green  g6 orange        h3 25%  h4 50%  h5 75% (default)  h6 100%
// The choice applies once the queen has stood still for a second (see applyGesture()) and is confirmed by
// the four centre squares blinking three times in the new setting.
// Start position in this driver's wire order: rank 1 first, files h..a within a rank.
constexpr char kStartWire[65] = "RNBKQBNR" "PPPPPPPP" "........" "........" "........" "........" "pppppppp" "rnbkqbnr";
int gestureCandidate = -1;  // wire index of the extra white queen on a gesture square, or -1
uint32_t gestureSince = 0;
bool gestureApplied = false;

int detectSettingsGesture() {
  int extra = -1;
  for (int i = 0; i < 64; ++i) {
    if (currentBoard[i] == kStartWire[i]) continue;
    if (extra >= 0 || currentBoard[i] != 'Q' || kStartWire[i] != '.') return -1;  // anything else differs
    extra = i;
  }
  if (extra < 0) return -1;
  const int file0 = 7 - (extra % 8);
  const int rank = extra / 8 + 1;
  return (file0 >= 6 && rank >= 3 && rank <= 6) ? extra : -1;  // g3..g6 or h3..h6
}

void updateGestureCandidate() {
  const int now = detectSettingsGesture();
  if (now != gestureCandidate) {
    gestureCandidate = now;
    gestureSince = millis();
    gestureApplied = false;
  }
}

// Same pattern as chessnut_board.cpp's own sendCableStatusFrame(): plain
// (not cableHostUsesEncodedChecksum) convention -- a real Mode-B board
// always computes its own outgoing checksum over plain content.
void sendCableStatusFrame() {
  uint8_t frame[kModeBStatusFrameLength] = {};
  frame[0] = 's';
  memcpy(frame + 1, currentBoard, 64);
  computeModeBChecksumHex(frame + 65, frame, 65, /*useEncodedConvention=*/false);
  onBoardStatusFrame(frame);
}

// Decodes one complete 32-byte position payload (the "=p" prefix already
// stripped by the caller). 2 squares per byte, upper nibble = first square,
// lower nibble = second, in row-major order starting a8, b8, ... h8, a7,
// ... h1 -- confirmed directly against the doc's own worked starting-
// position example.
void handlePositionMessage(const uint8_t* payload32) {
  for (int byteIdx = 0; byteIdx < 32; ++byteIdx) {
    const uint8_t firstCode = payload32[byteIdx] >> 4;
    const uint8_t secondCode = payload32[byteIdx] & 0x0f;
    for (int half = 0; half < 2; ++half) {
      const int rawIndex = byteIdx * 2 + half;  // 0..63, a8,b8,...,h8,a7,...,h1
      const int rank = 8 - (rawIndex / 8);
      const int file0 = rawIndex % 8;  // 0-based a..h, ascending
      const uint8_t code = (half == 0) ? firstCode : secondCode;
      currentBoard[modeBStatusWireIndex(file0, rank)] = pieceFromICode(code);
    }
  }
  updateGestureCandidate();
  sendCableStatusFrame();
  armVerboseCableLog();
}

void handleNotifyPacket(const uint8_t* data, size_t length) {
  if (length == 0) return;

  // Mid-reassembly: further bytes are a raw continuation of the position
  // payload, not a new framed message -- the board doesn't interleave
  // another reply inside a still-incomplete "=p" transfer.
  if (positionAssemblyLen > 0) {
    const size_t toCopy = min(length, kPositionMessageLength - positionAssemblyLen);
    memcpy(positionAssembly + positionAssemblyLen, data, toCopy);
    positionAssemblyLen += toCopy;
    if (positionAssemblyLen >= kPositionMessageLength) {
      handlePositionMessage(positionAssembly + 2);  // skip '=' 'p'
      positionAssemblyLen = 0;
    }
    return;
  }

  if (length < 2 || data[0] != '=') return;  // not a reply we recognize

  if (data[1] == 'p') {
    const size_t toCopy = min(length, kPositionMessageLength);
    memcpy(positionAssembly, data, toCopy);
    positionAssemblyLen = toCopy;
    if (positionAssemblyLen >= kPositionMessageLength) {
      handlePositionMessage(positionAssembly + 2);
      positionAssemblyLen = 0;
    }
    return;
  }

  // =hxx.xx (hardware version), =bSV (battery), =sOFF (status-off ack) --
  // logged only, nothing currently acts on them.
  Serial.printf("[ICHESSONE] reply: %.*s\r\n", static_cast<int>(length), data);
}

void onNotify(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  if (notifyQueue == nullptr) return;
  RawPacket packet{};
  packet.length = static_cast<uint8_t>(min(length, kMaxNotifyPayload));
  memcpy(packet.data, data, packet.length);
  xQueueSend(notifyQueue, &packet, 0);
}

class ClientCallbacks final : public NimBLEClientCallbacks {
 public:
  void onConnect(NimBLEClient*) override {}
  void onDisconnect(NimBLEClient*, int) override {
    writeChar = nullptr;
    notifyChar = nullptr;
    positionAssemblyLen = 0;
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

}  // namespace

bool ichessoneIsConnected() {
  return bleClient != nullptr && bleClient->isConnected() && writeChar != nullptr;
}

bool ichessoneConnect(const NimBLEAddress& address) {
  if (bleClient == nullptr) {
    bleClient = NimBLEDevice::createClient();
    bleClient->setClientCallbacks(new ClientCallbacks(), true);
    bleClient->setConnectTimeout(kConnectTimeoutMs);
  }
  if (notifyQueue == nullptr) notifyQueue = xQueueCreate(16, sizeof(RawPacket));

  // Same fast connection interval this project's other BLE board drivers
  // all use (proven necessary for reliable, timely status delivery -- see
  // millennium_board.cpp/chessnut_board.cpp's own identical calls).
  bleClient->setConnectionParams(12, 24, 0, 200);

  Serial.printf("Connecting to %s ...\r\n", address.toString().c_str());
  if (!bleClient->connect(address)) {
    Serial.println("BLE connection failed.");
    return false;
  }

  if (bleClient->getServices(true).empty()) {
    Serial.println("iChessOne BLE service discovery returned nothing.");
    bleClient->disconnect();
    return false;
  }

  writeChar = findCharacteristicInAnyService(bleClient, kWriteUuid);
  notifyChar = findCharacteristicInAnyService(bleClient, kNotifyUuid);
  if (writeChar == nullptr || notifyChar == nullptr) {
    Serial.println("iChessOne BLE characteristics not found.");
    writeChar = nullptr;
    bleClient->disconnect();
    return false;
  }

  if (!notifyChar->canNotify() || !notifyChar->subscribe(true, onNotify)) {
    Serial.println("Could not enable iChessOne notifications.");
    writeChar = nullptr;
    bleClient->disconnect();
    return false;
  }

  // Vendor doc recommends requesting MTU 50 so the position data fits one
  // packet -- NimBLEClient has no per-client setMTU() in this library
  // version (only NimBLEDevice::setMTU(), a global default applied before
  // any connection). Not requesting it here isn't a correctness problem:
  // handleNotifyPacket()'s reassembly already handles the position data
  // arriving split across multiple packets at whatever MTU actually gets
  // negotiated, just with one extra notify round-trip of latency.
  Serial.printf("iChessOne negotiated MTU=%u.\r\n", bleClient->getMTU());

  positionAssemblyLen = 0;
  resetKingLedFrameBaseline();

  Serial.println("iChessOne BLE gateway connected.");
  // CPIRQ: stream position updates only when a piece actually moves, rather
  // than on a fixed timer -- this project is already fully event-driven
  // (every other board's own notify-triggered flow works the same way), so
  // there's no reason to prefer CPMxxxx's periodic-polling alternative.
  writeChar->writeValue(reinterpret_cast<const uint8_t*>("CPIRQ"), 5, true);
  // CPIRQ only reports CHANGES (doc: "Reply: none (other than the new position
  // data when a piece moves)"). RP is the doc's one-shot "send the current
  // position" for exactly this case; without it no status exists until the
  // first piece is touched, so the BT-BT "board ready" signal and the cable
  // host's first status would wait for that.
  writeChar->writeValue(reinterpret_cast<const uint8_t*>("RP"), 2, true);
  ichessoneResetLedState();
  return true;
}

namespace {

// LED output (2026-10-11 review against iChessOneAPI.pdf and the Certabo
// findings). Every LED request becomes a 64-bit square mask that goes through
// one gate: identical masks are never rewritten, two EL commands are at least
// 25 ms apart (the doc asks for >= 20 ms), and a failed write is retried
// instead of being forgotten (a lost "clear" would leave an LED stuck on).
constexpr uint32_t kMinGapMs = 25;
constexpr uint32_t kRetryMs = 250;
constexpr uint32_t kSuggestionHoldMs = 1000;  // Phoenix keeps re-sending while it suggests

struct Rgb { uint8_t r, g, b; };
constexpr Rgb kColors[4] = {{15, 0, 0}, {0, 0, 15}, {0, 15, 0}, {15, 7, 0}};  // red, blue, green, orange
// 25 / 50 / 75 / 100 % of the 0..F range ("8" is 50% in the vendor's own example; 0 would mean "default").
constexpr uint8_t kBrightness[4] = {4, 8, 11, 15};
uint8_t colorIndex = 0;       // red
uint8_t brightnessIndex = 2;  // 75%
uint8_t pendingFlashes = 0;

void loadSettings() {
  Preferences prefs;
  if (!prefs.begin("ichessone", true)) return;
  const uint8_t c = prefs.getUChar("color", 0);
  const uint8_t b = prefs.getUChar("bright", 2);
  colorIndex = c < 4 ? c : 0;
  brightnessIndex = b < 4 ? b : 2;
  prefs.end();
}

void saveSettings() {
  Preferences prefs;
  if (!prefs.begin("ichessone", false)) return;
  prefs.putUChar("color", colorIndex);
  prefs.putUChar("bright", brightnessIndex);
  prefs.end();
}

uint64_t lastMask = 0;
bool haveLast = false;
uint32_t lastWriteAt = 0;
uint64_t pendingMask = 0;
bool havePending = false;
uint32_t nextTryAt = 0;

// Phoenix accumulator: Phoenix reveals one move as alternating single-square
// frames, repeated every ~43 ms. The union of the last two different squares is
// shown steadily; it expires kSuggestionHoldMs after the last frame.
SquareHighlight accumulated[2];
size_t accumulatedCount = 0;
bool accumulatedShown = false;
uint32_t accumulatedSeenAt = 0;

uint64_t maskOf(const SquareHighlight* squares, size_t count) {
  uint64_t mask = 0;
  for (size_t i = 0; squares != nullptr && i < count; ++i) {
    if (squares[i].squareIndex < 64) mask |= uint64_t(1) << squares[i].squareIndex;
  }
  return mask;
}

bool sendMask(uint64_t mask, uint8_t flashes = 0) {
  // One steady colour for all squares (2026-09-02: only one color is needed, always on rather than
  // blinking), now selectable by the settings gestures: default red at 75% brightness.
  // Bytes: i r | g b (each 0..F), see kColors/kBrightness. fc = flashes<<4 | 1 (the 1 = clear
  // previous each call, so this always fully replaces whatever was lit before, never accumulates).
  // tt=0xFF keeps it on until the next EL command; with flashes>0 the board flashes that many times
  // by itself and tt=0 turns it off afterwards (vendor doc: "stay on after that based on tt").
  const Rgb color = kColors[colorIndex];
  const uint8_t fc = flashes ? static_cast<uint8_t>((flashes << 4) | 0x01) : static_cast<uint8_t>(0x01);
  const uint8_t tt = flashes ? static_cast<uint8_t>(0x00) : static_cast<uint8_t>(0xFF);
  uint8_t command[16] = {'E', 'L', static_cast<uint8_t>((kBrightness[brightnessIndex] << 4) | color.r),
                         static_cast<uint8_t>((color.g << 4) | color.b), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, fc, tt};
  for (int square = 0; square < 64; ++square) {
    if (!((mask >> square) & 1u)) continue;
    const int file0 = square % 8;  // 0-based a..h
    const int rank = square / 8 + 1;
    // Row-byte order in the command is row9,row8,...,row1,row0 (command[4]
    // = row9/far bar .. command[13] = row0/near bar), so rank r lives at
    // command[4 + (9 - r)]. File-to-bit mapping (a=bit0 .. h=bit7) confirmed
    // directly from the doc's own worked example (highlighting a1+h8 -> row1
    // byte 0x01, row8 byte 0x80).
    command[4 + (9 - rank)] |= static_cast<uint8_t>(1 << file0);
  }
  if (!writeChar->writeValue(command, sizeof(command), true)) return false;
  lastMask = flashes ? 0 : mask;  // after a flash sequence the board is dark again
  haveLast = true;
  lastWriteAt = millis();
  return true;
}

void flushPending() {
  if (!havePending || !ichessoneIsConnected()) return;
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - nextTryAt) < 0) return;
  if (haveLast && uint32_t(now - lastWriteAt) < kMinGapMs) return;
  if (sendMask(pendingMask, pendingFlashes)) {
    havePending = false;
  } else {
    nextTryAt = now + kRetryMs;
    Serial.println("[ICHESSONE] LED write failed; will retry");
  }
}

void requestMask(uint64_t mask, uint8_t flashes = 0) {
  if (!ichessoneIsConnected()) return;
  if (!flashes && haveLast && mask == lastMask) {  // already on the board, drop any stale queued change
    havePending = false;
    return;
  }
  pendingMask = mask;
  pendingFlashes = flashes;
  havePending = true;
  nextTryAt = 0;
  flushPending();
}

void applyGesture() {
  if (gestureCandidate < 0 || gestureApplied || uint32_t(millis() - gestureSince) < 1000) return;
  gestureApplied = true;
  const int file0 = 7 - (gestureCandidate % 8);
  const int rank = gestureCandidate / 8 + 1;
  const int index = rank - 3;  // 0..3
  if (file0 == 6) colorIndex = static_cast<uint8_t>(index); else brightnessIndex = static_cast<uint8_t>(index);
  saveSettings();
  Serial.printf("[ICHESSONE] LED setting %s -> choice %d (colour %u, brightness %u%%), saved\r\n",
                file0 == 6 ? "colour" : "brightness", index + 1, colorIndex, 25u * (brightnessIndex + 1));
  // Confirmation: d4 e4 d5 e5 blink three times in the new setting (flashed by the board itself).
  accumulatedShown = false;
  accumulatedCount = 0;
  requestMask((uint64_t(1) << 27) | (uint64_t(1) << 28) | (uint64_t(1) << 35) | (uint64_t(1) << 36), 3);
}

}  // namespace

void ichessoneResetLedState() {
  haveLast = false;
  havePending = false;
  accumulatedShown = false;
  accumulatedCount = 0;
  loadSettings();
}

void ichessonePoll() {
  if (!ichessoneIsConnected()) return;
  RawPacket packet{};
  while (xQueueReceive(notifyQueue, &packet, 0) == pdTRUE) {
    handleNotifyPacket(packet.data, packet.length);
  }
  if (accumulatedShown && uint32_t(millis() - accumulatedSeenAt) > kSuggestionHoldMs) {
    accumulatedShown = false;
    accumulatedCount = 0;
    requestMask(0);
  }
  applyGesture();
  flushPending();
}

void ichessoneSetHighlightedSquares(const SquareHighlight* squares, size_t count) {
  accumulatedShown = false;
  accumulatedCount = 0;
  requestMask(maskOf(squares, count));
}

void ichessoneShowSuggestion(const SquareHighlight* squares, size_t count, bool repeatingHost) {
  if (!repeatingHost) {
    // King and BLE clients send each suggestion once: show exactly that set.
    ichessoneSetHighlightedSquares(squares, count);
    return;
  }
  if (count == 0) return;  // empty Phoenix frames carry nothing; the hold timer clears
  for (size_t i = 0; i < count; ++i) {
    bool known = false;
    for (size_t j = 0; j < accumulatedCount; ++j) {
      if (accumulated[j].squareIndex == squares[i].squareIndex) known = true;
    }
    if (known) continue;
    if (accumulatedCount < 2) {
      accumulated[accumulatedCount++] = squares[i];
    } else {
      accumulated[0] = squares[i];  // a third, different square: new suggestion
      accumulatedCount = 1;
    }
  }
  accumulatedShown = true;
  accumulatedSeenAt = millis();
  requestMask(maskOf(accumulated, accumulatedCount));
}

void ichessoneClearLeds() { ichessoneSetHighlightedSquares(nullptr, 0); }
