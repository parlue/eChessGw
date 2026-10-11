#include "certabo_board.h"
#include "certabo_protocol.h"
#include "certabo_startup.h"
#include "certabo_move_lights.h"
#include "certabo_king_echo.h"

#include <Preferences.h>
#include <algorithm>
#include <atomic>
#include <cstring>

const char kCertaboBoardName[] = "Certabo";

namespace {
constexpr char kServiceUuid[] = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char kWriteUuid[] = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr char kNotifyUuid[] = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr size_t kChunkSize = 128;
struct Packet { uint32_t sequence; uint16_t length; uint8_t data[kChunkSize]; };
NimBLEClient* client = nullptr;
NimBLERemoteCharacteristic* writer = nullptr;
QueueHandle_t queue = nullptr;
std::atomic<bool> ready{false};
std::atomic<uint32_t> dropped{0};
uint32_t nextSequence = 0;  // BLE task producer only
uint32_t expectedSequence = 0;  // loop consumer only
certabo::Parser parser;
certabo::Calibration calibration;
certabo::Startup startup;
certabo::RawBoard latestBoard{};
char calibrationKey[13]{};  // BLE MAC, without separators
uint8_t lastPosition[67]{};
bool havePosition = false;
bool kingMode = false;
char kingBaseline[64]{};
bool haveKingBaseline = false;
uint8_t kingPulse[8]{};
uint32_t kingPulseUntil = 0;
// King-only arrival display. Two cases, same machinery (kingPulse mask +
// deadline, overriding wantedLeds until it expires, so a fast King reply can
// never cut it short -- the reply simply appears right after):
//  - the human's OWN move: both changed squares for kOwnMoveHoldMs. This is what
//    the King itself shows (its replay of from+to), but the replay arrives
//    ~140 ms after the scan and the reply replaces it ~120 ms later, so the
//    move was only visible for a blink (2026-10-11: "e4 leuchtet, e2 nicht").
//  - the human executing the King's OWN suggestion: destination only, kKingPulseMs.
// The time is counted from the moment the mask is really written (the loop gets
// to sendLeds() up to ~66 ms after the scan -- b1-c3 was visible for 177 ms in
// the 2026-10-11 capture); the deadline armed at the scan is only a safety cap.
constexpr uint32_t kKingPulseMs = 350;
constexpr uint32_t kOwnMoveHoldMs = 500;
constexpr uint32_t kKingPulseArmMs = 700;
uint32_t kingPulseHoldMs = kKingPulseMs;
bool kingPulseWritten = true;
// True once the last arrival has been dealt with (own-move display started, or
// deliberately not shown because it executed a King suggestion).
bool ownMoveHandled = true;
// Squares of the last completed physical move (see certabo_king_echo.h). The
// King's replay of the human's own move is recognised from it and shown through
// the own-move display instead of as an unrelated 120 ms suggestion. Recorded for
// every host but only read by certaboSetOneShotLedSquares() (King), never on the
// Phoenix path.
certabo::MoveEcho moveEcho;

void armKingPulse(const uint8_t* squares, size_t count, uint32_t holdMs) {
  certabo::ledMask(squares, count, kingPulse);
  kingPulseHoldMs = holdMs;
  kingPulseUntil = millis() + kKingPulseArmMs;
  kingPulseWritten = false;
}
bool litIn(const uint8_t mask[8], int square) {
  return (mask[7 - square / 8] >> (square % 8)) & 1u;
}
bool unknownReported = false;
bool occupancyReported = false;
uint32_t badFrames = 0;
uint32_t lastDiagnosticsAt = 0;
uint32_t reportedDrops = 0;
uint32_t reportedBad = 0;
size_t diagnosticBytes = 0;
uint8_t wantedLeds[8]{};
uint8_t ledCandidateHits[64]{};
certabo::MoveLights moveLights;
uint32_t ledSeenAt[64] = {0};
uint64_t ledHints = 0;
char lightBoard[64]{};
uint8_t sentLeds[8]{};
bool haveSentLeds = false;
uint32_t lastLedSentAt=0;
certabo::LedType sentLedType = certabo::LedType::Unknown;

void notify(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  if (!queue) return;
  for (size_t offset = 0; offset < length; offset += kChunkSize) {
    Packet packet{};
    packet.sequence = nextSequence++;
    packet.length = static_cast<uint16_t>(std::min(kChunkSize, length - offset));
    memcpy(packet.data, data + offset, packet.length);
    if (xQueueSend(queue, &packet, 0) != pdTRUE) ++dropped;
  }
}

class Callbacks final : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient*, int reason) override {
    ready.store(false);
    Serial.printf("[CERTABO] disconnected (%d); reconnecting automatically\r\n", reason);
  }
};

// NVS is an archive only. Calibration starts empty on every MCU boot.
void exportCalibration() {
  uint8_t blob[certabo::Calibration::kBlobSize]{};
  if (!calibration.save(blob)) return;
  Serial.printf("[CERTABO_EXPORT] %s ", calibrationKey);
  for (auto byte : blob) Serial.printf("%02X", byte);
  Serial.println();
}

void saveCalibration() {
  uint8_t blob[certabo::Calibration::kBlobSize]{};
  Preferences prefs;
  if (!calibration.save(blob) || !prefs.begin("certabo", false)) {
    Serial.println("[CERTABO] calibration works in RAM; could not open NVS");
    return;
  }
  const bool saved = prefs.putBytes(calibrationKey, blob, sizeof(blob)) == sizeof(blob);
  prefs.end();
  Serial.println(saved ? "[CERTABO] calibration saved" : "[CERTABO] calibration save failed; using RAM");
}

void position(const certabo::RawBoard& board) {
  // Calibration needs raw scans. After learning, startup LEDs must use only
  // validated positions, just like the position sent to either host.
  if (!calibration.ready()) latestBoard = board;
  if (!calibration.ready()) {
    if (!calibration.observe(board, true)) return;
    Serial.printf("[CERTABO] calibration complete: %u IDs; remove extra queens\r\n",
                  static_cast<unsigned>(calibration.count()));
    saveCalibration();
    exportCalibration();
    startup.learned();
  }
  uint8_t frame[67]{};
  if (!calibration.decode(board, frame)) {
    static uint32_t lastRejectedDump=0;
    static uint32_t lastWithheldLog=0;
    if(uint32_t(millis()-lastRejectedDump)>=5000) {
      lastRejectedDump=millis();
      Serial.print("[CERTABO_REJECTED] :");
      for(const auto& id:board) for(auto byte:id) Serial.printf("%u ",byte);
      Serial.println();
    }
    // Transient "unknown/duplicate" scans are expected while a piece is being
    // moved (lifted / mid-air / placed) and must not spam the monitor. Keep
    // withholding the ambiguous position (never forward a half-read board),
    // but only log the condition a few times per second.
    if (uint32_t(millis()-lastWithheldLog)>=3000) {
      lastWithheldLog=millis();
      Serial.println("[CERTABO] unknown/duplicate piece ID; position withheld (not treated as empty)");
    }
    unknownReported = true;
    return;
  }
  if (unknownReported) {
    Serial.println("[CERTABO] valid position resumed (unknown/duplicate was transient)");
  }
  unknownReported = false;
  latestBoard = board;
  for(size_t i=0;i<64;++i) lightBoard[i]=frame[64-(i^56)];
  // What the King had lit before this scan: a suggestion pair means the human
  // is executing the King's move, a single lifted square means the human's own.
  uint8_t wantedBefore[8];
  memcpy(wantedBefore,wantedLeds,sizeof(wantedBefore));
  if(moveLights.observe(lightBoard)) {
    memset(wantedLeds,0,sizeof(wantedLeds));
    memset(ledSeenAt,0,sizeof(ledSeenAt));
    ledHints = 0;
    memset(ledCandidateHits,0,sizeof(ledCandidateHits));
    Serial.println("[CERTABO LED] completed position; clear steady lights");
  }
  // Rebuilding the exact standard starting position mid-game (aborting to
  // start a new game) is not a single tracked move -- moveLights.observe()
  // only recognizes one piece lifted/placed at a time, so it never fires
  // here and the last move's LEDs stayed lit. Force-clear unconditionally
  // whenever the board is back to the opening array.
  {
    // lightBoard is a1..h8 (white first), not the parser's a8..h1 order.
    constexpr char kStart[] = "RNBQKBNRPPPPPPPP................................pppppppprnbqkbnr";
    bool isStart = true;
    for (size_t i = 0; i < 64 && isStart; ++i) if (lightBoard[i] != kStart[i]) isStart = false;
    if (isStart) {
      memset(wantedLeds, 0, sizeof(wantedLeds));
      memset(ledSeenAt, 0, sizeof(ledSeenAt));
      ledHints = 0;
      memset(ledCandidateHits, 0, sizeof(ledCandidateHits));
      moveLights.reset();
      moveEcho.reset();
    }
  }
  if (startup.active()) {
    if (!startup.finish(frame)) return;
    memset(wantedLeds, 0, sizeof(wantedLeds));
    Serial.println("[CERTABO] startup complete; normal position ready");
  }
  // Keep a complete-position baseline across lift-only scans. On arrival,
  // briefly confirm the destination; pending King replies remain in wantedLeds.
  if (haveKingBaseline) {
    int from=-1, to=-1, differences=0;
    bool arrived=false;
    uint64_t changed=0;
    for (int s=0;s<64;++s) if (kingBaseline[s]!=lightBoard[s]) {
      ++differences;
      changed|=uint64_t(1)<<s;
      if (lightBoard[s]=='.') from=s;
      else { to=s; arrived=true; }
    }
    if (arrived) {
      // Independent of kingMode: a King's first L frame can arrive only AFTER
      // the first move was scanned (it switches King mode on), and that frame
      // is the replay that must then trigger the own-move display.
      moveEcho.arrived(changed,differences,millis());
      ownMoveHandled = false;
      int litChanged = 0;
      for (int s=0;s<64;++s) if (((changed>>s)&1u) && litIn(wantedBefore,s)) ++litChanged;
      const bool executedSuggestion = litChanged >= 2;
      if (kingMode && differences>=2 && differences<=4) {
        if (executedSuggestion) {
          // Executing the King's own move: its lights go off (observe() above),
          // only the destination blips. Nothing of this arrival may be shown
          // again by a replay frame.
          ownMoveHandled = true;
          if (differences==2 && from>=0 && to>=0 && kingBaseline[from]==lightBoard[to]) {
            const uint8_t target=static_cast<uint8_t>(to);
            armKingPulse(&target,1,kKingPulseMs);
          }
        } else if (differences>2 || (from>=0 && to>=0 && kingBaseline[from]==lightBoard[to])) {
          // The human's own move: show it -- every changed square -- for a fixed
          // time, whatever the King does next.
          uint8_t squares[4]; size_t n=0;
          for (int s=0;s<64 && n<4;++s) if ((changed>>s)&1u) squares[n++]=static_cast<uint8_t>(s);
          armKingPulse(squares,n,kOwnMoveHoldMs);
          ownMoveHandled = true;
        }
      }
      memcpy(kingBaseline,lightBoard,64);
    } else if (differences>0) {
      // Next move has begun (piece lifted): the King's lift frame for it must
      // never be mistaken for the replay of the previous move.
      moveEcho.lifted();
    }
  } else {
    memcpy(kingBaseline,lightBoard,64);
    haveKingBaseline=true;
  }
  // Forward every distinct valid position, including lift/capture intermediate
  // positions. Repeated scans of an unchanged board do not flood the hosts.
  if (!havePosition || memcmp(lastPosition, frame, sizeof(frame)) != 0) {
    memcpy(lastPosition, frame, sizeof(frame));
    havePosition = true;
    Serial.print("[CERTABO_RAW] :");
    for (const auto& id : board) for (auto byte : id) Serial.printf("%u ", byte);
    Serial.print("\r\n");
    Serial.printf("[CERTABO] position: %.*s\r\n", 67, reinterpret_cast<const char*>(frame));
    onBoardStatusFrame(frame);
  }
}

void sendLeds() {
  const auto type = parser.leds;
  const uint32_t now = millis();
  uint8_t display[8];
  memcpy(display,wantedLeds,8);
  const bool pulseShown = kingMode && !startup.active() &&
      static_cast<int32_t>(kingPulseUntil-now)>0;
  if (pulseShown) memcpy(display,kingPulse,8);
  // A late host LED frame must not relight the restored starting position.
  // Calibration/blink confirmation retains exclusive control during startup.
  constexpr char kStart[] = "RNBQKBNRPPPPPPPP................................pppppppprnbqkbnr";
  if (!startup.active() && havePosition && memcmp(lightBoard, kStart, 64) == 0) {
    memset(display, 0, sizeof(display));
    memset(wantedLeds, 0, sizeof(wantedLeds));
    memset(ledSeenAt, 0, sizeof(ledSeenAt));
    memset(ledCandidateHits, 0, sizeof(ledCandidateHits));
    ledHints = 0;
    moveLights.reset();
    moveEcho.reset();
    kingPulseUntil=0;
  }
  const bool unchanged=haveSentLeds && type==sentLedType && memcmp(display,sentLeds,8)==0;
  // Normal play is change-driven: an identical mask is never retransmitted.
  // Startup alone permits a timed repeat so its phase clock can advance.
  const bool refreshKing = kingMode && !startup.active() &&
                           uint32_t(now-lastLedSentAt) >= 300;
  if (unchanged && !refreshKing && (!startup.active() || uint32_t(now-lastLedSentAt)<250)) return;
  // Coalesce rapid King updates into the newest complete mask. Never block
  // board reception; certaboPoll retries the pending state. Phoenix unchanged.
  if (kingMode && !startup.active() && haveSentLeds &&
      uint32_t(now-lastLedSentAt) < 120) return;
  uint8_t command[247]{};
  const size_t length = type == certabo::LedType::Rgb ? sizeof(command) : 8;
  if (type == certabo::LedType::Rgb) certabo::rgbCommand(display, command);
  else memcpy(command, display, 8);
  const uint16_t mtu = client->getMTU();
  const size_t chunk = mtu >= 23 ? mtu - 3 : 20;
  // The supplied bridge is a raw UART byte stream. Keep RGB frame fragments
  // contiguous, ordered and within MTU; use Write Without Response only.
  for (size_t offset = 0; offset < length; offset += chunk) {
    if (!certaboIsConnected() || !writer->writeValue(command + offset, std::min(chunk, length - offset), false)) {
      Serial.println("[CERTABO] LED write failed; reconnecting (bridge may have received a partial command)");
      ready.store(false);
      client->disconnect();
      return;
    }
  }
  memcpy(sentLeds, display, 8);
  lastLedSentAt=millis();
  if (pulseShown && !kingPulseWritten) {
    // First real write of this arrival display: it now stays visible for its
    // full hold time from here, however late the loop got to it after the scan.
    kingPulseWritten=true;
    kingPulseUntil=lastLedSentAt+kingPulseHoldMs;
  }
  if(!unchanged) {
  Serial.printf("[CERTABO LED] sent t=%lu write_ms=%lu bytes=%u:",
                static_cast<unsigned long>(millis()),static_cast<unsigned long>(millis()-now),
                static_cast<unsigned>(length));
  for (uint8_t square=0;square<64;++square)
    if(display[7-square/8] & (1u << (square%8))) Serial.printf(" %c%d",'a'+square%8,1+square/8);
  Serial.println();
  }
  sentLedType = type;
  haveSentLeds = true;
  if (startup.active()) startup.sent(now);
}
}  // namespace

bool certaboIsConnected() { return ready.load() && client && client->isConnected(); }
void certaboSetKingMode(bool enabled) { kingMode = enabled; }

bool certaboConnect(const NimBLEAddress& address) {
  haveKingBaseline=false;
  kingPulseUntil=0;
  kingPulseWritten=true;
  ownMoveHandled=true;
  moveEcho.reset();
  moveLights.reset();
  memset(ledCandidateHits,0,sizeof(ledCandidateHits));
  ready.store(false);
  writer = nullptr;
  if (!client) {
    client = NimBLEDevice::createClient();
    if (!client) return false;
    client->setClientCallbacks(new Callbacks(), true);
    client->setConnectTimeout(8000);
  }
  if (!queue) queue = xQueueCreate(32, sizeof(Packet));
  if (!queue) { Serial.println("[CERTABO] RX queue allocation failed"); return false; }
  xQueueReset(queue);
  nextSequence = expectedSequence = 0;
  dropped.store(0);
  reportedDrops = reportedBad = badFrames = 0;
  diagnosticBytes = 0;
  parser.reset();
  havePosition = haveSentLeds = unknownReported = occupancyReported = false;
  memset(wantedLeds, 0, sizeof(wantedLeds));
  memset(ledSeenAt, 0, sizeof(ledSeenAt));
  ledHints = 0;
  std::string key = address.toString();
  key.erase(std::remove(key.begin(), key.end(), ':'), key.end());
  if (key.size() != 12) return false;
  // Every connection starts a fresh seven-scan calibration. The archived ID
  // map is for diagnostics/offline testing only; never reuse it as the live
  // map because a Certabo scan can report transient or changed IDs.
  calibration.reset();
  startup.reset();
  latestBoard = {};
  memset(ledCandidateHits, 0, sizeof(ledCandidateHits));
  memcpy(calibrationKey, key.c_str(), 13);
  Serial.printf("[CERTABO] connecting to %s\r\n", address.toString().c_str());
  const uint16_t previousMtu = NimBLEDevice::getMTU();
  const bool mtuRequested = NimBLEDevice::setMTU(256);
  Serial.printf("[CERTABO] requested MTU=256 accepted=%u previous=%u\r\n", mtuRequested, previousMtu);
  const bool connected = client->connect(address);
  NimBLEDevice::setMTU(previousMtu);
  if (!connected) return false;
  // NUS is also used by iChessOne. Scan selects by name, then verify the
  // actual service UUID and properties; never treat NUS alone as Certabo.
  NimBLERemoteService* service = client->getService(kServiceUuid);
  writer = service ? service->getCharacteristic(kWriteUuid) : nullptr;
  auto* reader = service ? service->getCharacteristic(kNotifyUuid) : nullptr;
  if (!writer || !writer->canWriteNoResponse() || !reader || !reader->canNotify() ||
      !reader->subscribe(true, notify)) {
    Serial.println("[CERTABO] NUS discovery/properties/subscription failed");
    client->disconnect();
    writer = nullptr;
    return false;
  }
  if (!client->isConnected()) return false;
  ready.store(true);
  Serial.printf("[CERTABO] subscribed, MTU=%u; waiting for pushed positions (no read/handshake)\r\n", client->getMTU());
  if (!calibration.ready()) {
    Serial.println("[CERTABO] CALIBRATE: normal starting position; extra white queen d3, black queen d6; hold still for 7 scans");
  }
  if (calibration.ready()) exportCalibration();
  resetKingLedFrameBaseline();
  return true;
}

void certaboPoll() {
  if (!certaboIsConnected()) return;
  Packet packet{};
  while (certaboIsConnected() && xQueueReceive(queue, &packet, 0) == pdTRUE) {
    if (packet.sequence != expectedSequence) {
      parser.resetStream();
      calibration.resetWindow();
      Serial.println("[CERTABO] RX gap; discarding partial frame and calibration samples");
    }
    expectedSequence = packet.sequence + 1;
    // Bounded byte-exact capture, including malformed frames, for hardware diagnosis.
    if (diagnosticBytes < 8192) {
      char hex[kChunkSize * 2 + 1];
      const size_t n = std::min(size_t(packet.length), size_t(8192 - diagnosticBytes));
      for (size_t i=0; i<n; ++i) snprintf(hex+i*2, 3, "%02X", packet.data[i]);
      hex[n*2]=0;
      Serial.printf("[CERTABO_RX] %lu %s\r\n", static_cast<unsigned long>(packet.sequence), hex);
      diagnosticBytes += n;
    }
    for (size_t i = 0; i < packet.length; ++i) {
      const auto oldLedType = parser.leds;
      const auto result = parser.feed(packet.data[i]);
      if (parser.leds != oldLedType) Serial.printf("[CERTABO] %s LEDs detected\r\n", parser.leds == certabo::LedType::Rgb ? "RGB" : "mono");
      if (result == certabo::ParseResult::Position) {
        position(parser.board());
        // Certabo scans can arrive back-to-back in one BLE queue drain.
        // Publish the LED state after each complete scan so a later scan
        // cannot hide an intermediate move indication until the drain ends.
        if (certaboIsConnected()) sendLeds();
      }
      else if (result == certabo::ParseResult::Invalid) { ++badFrames; calibration.resetWindow(); }
      else if (result == certabo::ParseResult::OccupancyOnly && !occupancyReported) {
        Serial.println("[CERTABO] occupancy-only/Sentio data is unsupported; piece-ID board required");
        occupancyReported = true;
      }
    }
  }
  if (static_cast<uint32_t>(millis() - lastDiagnosticsAt) >= 1000) {
    lastDiagnosticsAt = millis();
    const uint32_t losses = dropped.load();
    if (losses != reportedDrops || badFrames != reportedBad) {
      Serial.printf("[CERTABO] dropped RX chunks=%lu, invalid frames=%lu\r\n",
                    static_cast<unsigned long>(losses), static_cast<unsigned long>(badFrames));
      reportedDrops = losses;
      reportedBad = badFrames;
    }
  }
  if (startup.active()) startup.mask(millis(), latestBoard, wantedLeds, calibration.ready() ? &calibration : nullptr);
  if (certaboIsConnected()) sendLeds();
}

void certaboSetHighlightedSquares(const SquareHighlight* squares, size_t count) {
  if (startup.active()) return;  // Calibration owns LEDs until the starting position is ready.
  moveLights.reset();
  memset(ledCandidateHits,0,sizeof(ledCandidateHits));
  uint8_t indices[64]{};
  const size_t size = squares ? std::min(count, sizeof(indices)) : 0;
  for (size_t i = 0; i < size; ++i) indices[i] = squares[i].squareIndex;
  certabo::ledMask(indices, size, wantedLeds);
  if(certaboIsConnected()) sendLeds();
}
void certaboClearLeds() { certaboSetHighlightedSquares(nullptr, 0); }

void certaboSetMoveLights(const SquareHighlight* squares, size_t count) {
  if(startup.active() || !havePosition) return;
  // Phoenix blinks a move as alternating single-square frames and also echoes
  // the opponent's just-played move before showing its own reply. MoveLights
  // already accumulates the pair, discards a stale pair when a third square
  // appears, and suppresses an echo whose squares it saw complete -- so the
  // displayed union is exactly what MoveLights tracks, never a raw frame.
  if (count == 0) return;

  uint8_t indices[64]{};
  const size_t n = std::min(count, size_t(64));
  for (size_t i = 0; i < n; ++i) indices[i] = squares[i].squareIndex;

  // No Serial logging here, even change-gated: this runs on every incoming
  // Phoenix 'L' frame (continuous while it blinks a suggestion), and
  // certaboPoll()/board_driver.cpp's dispatch share loop() with the King/
  // Phoenix cable channel on this single-core MCU -- see board_driver.cpp's
  // own comment on the matching debug print removed alongside this one.

  moveLights.add(indices, n, lightBoard);

  // Suppress the echo of the just-completed human move. MoveLights.observe()
  // flags the move's squares as suppressed as soon as the Certabo confirms the
  // piece landed; the Phoenix then keeps re-sending those squares for ~1s as
  // its echo. Without this, the echo re-lights the LEDs right after the
  // completion clear, so the human's own move stays lit until Black's reply
  // replaces it.
  if (moveLights.isSuppressed()) {
    // observe() can infer and suppress the destination square from a pure
    // board-scan diff before Phoenix's own LED frame for that square ever
    // arrives -- the human completing their own move physically is often
    // faster than Phoenix's alternating echo. That silently ate the
    // destination's one-time flash on White's own moves. A square already
    // shown (in ledHints) stays suppressed; a square never yet displayed
    // still gets its one flash below.
    bool anyNew = false;
    for (size_t i = 0; i < n; ++i) {
      if (!(ledHints & (uint64_t(1) << indices[i]))) { anyNew = true; break; }
    }
    if (!anyNew) return;
  }

  // Display: accumulate every recently-seen square and expire squares not
  // re-sent within the window below, then cap at the two most recent
  // squares. Phoenix blinks its reply roughly every 100ms; King's own cable
  // timing is looser/slower (confirmed: moves play correctly over King, but
  // its LED suggestion is intermittently missing at 800ms -- King does not
  // re-send its 'L' frame as often as Phoenix). 2000ms comfortably covers
  // both hosts' real repeat intervals while still clearing promptly once a
  // move actually completes (that path returns earlier, above).
  constexpr uint32_t kLedHintExpiryMs = 2000;
  const uint32_t now = millis();
  for (size_t i = 0; i < n; ++i) {
    const uint8_t s = indices[i];
    ledSeenAt[s] = now;
    ledHints |= (uint64_t(1) << s);
  }
  for (int s = 0; s < 64; ++s) {
    if ((ledHints & (uint64_t(1) << s)) && now - ledSeenAt[s] > kLedHintExpiryMs) {
      ledHints &= ~(uint64_t(1) << s);
    }
  }

  uint8_t show[2]{};
  size_t showCount = 0;
  for (int s = 0; s < 64; ++s) {
    if (!(ledHints & (uint64_t(1) << s))) continue;
    if (showCount < 2) {
      show[showCount++] = static_cast<uint8_t>(s);
    } else {
      const size_t oldest = ledSeenAt[show[0]] < ledSeenAt[show[1]] ? 0 : 1;
      if (ledSeenAt[s] > ledSeenAt[show[oldest]]) show[oldest] = static_cast<uint8_t>(s);
    }
  }
  certabo::ledMask(show, showCount, wantedLeds);
  if(certaboIsConnected()) sendLeds();
}

// Direct display for a host whose 'L' frames are one-shot and already complete
// (a real King: source AND destination marked in the same frame, distinct
// corner bits, never re-sent -- see board_driver.h's
// dispatchCableLedFrameToBoard() and millennium_board.cpp's own "unlike King's
// one-shot steady display" note). The decoded frame simply replaces the whole
// mask, like chessnutSetHighlightedSquares()/ichessoneSetHighlightedSquares()
// do for the same host, instead of being run through the Phoenix accumulator in
// certaboSetMoveLights() above, whose display state assumes the host repairs it:
//   - the "isSuppressed() && !anyNew" early return drops a frame entirely --
//     no sendLeds() at all, so the board keeps showing the previous state;
//   - the 2000ms ledHints expiry is sized for Phoenix's ~10-20 frames per
//     second. A one-shot frame is never re-sent, so a suggestion older than
//     that window would silently degrade from two squares to one (or none) as
//     soon as any later frame arrived;
//   - the accumulator caps the display at the two most recently seen squares,
//     which only means something for Phoenix's alternating blink phases -- a
//     King's single frame already is the complete pair.
// The frame still goes through MoveLights.add() first, so position()'s
// completion detection (and its "completed position; clear steady lights"
// clear) keeps working exactly as it does for Phoenix -- and so the one
// remaining filter can use it: a frame whose squares all belong to the move
// that just completed is the host replaying the human's own move, and is
// skipped instead of re-lighting the squares that clear just emptied. A new
// suggestion always contains at least one square outside the completed move (a
// legal reply cannot reuse both squares of the move it answers), so it is never
// skipped; it replaces the mask and is sent once.
// Nothing here depends on the host re-sending anything: certaboPoll() calls
// sendLeds() on every loop pass and sendLeds() is itself change-driven (an
// unchanged mask is not retransmitted, and a failed write leaves sentLeds
// untouched), so a lost or slow BLE write is retried by us on the next pass --
// which is what the slow Certabo link actually needs.
void certaboSetOneShotLedSquares(const SquareHighlight* squares, size_t count) {
  if (startup.active() || !havePosition) return;
  if (!squares || count == 0) {
    memset(wantedLeds, 0, sizeof(wantedLeds));
    moveLights.reset();
    if (certaboIsConnected()) sendLeds();
    return;
  }
  uint8_t indices[64]{};
  const size_t n = std::min(count, sizeof(indices));
  uint64_t incoming = 0;
  for (size_t i = 0; i < n; ++i) {
    indices[i] = squares[i].squareIndex;
    incoming |= uint64_t(1) << (indices[i] & 63);
  }
  // Replay of the human's own move, recognised from the board scan itself (see
  // certabo_king_echo.h): independent of whether the King announced the move
  // with a lift frame first, of kingMode and of the 120 ms write coalescing.
  // It is NOT a suggestion, so it never goes through MoveLights/wantedLeds. If
  // the arrival scan could not start the own-move display itself (very first
  // move after connecting: King mode was still off), the replay starts it with
  // exactly the squares the King lit.
  if (moveEcho.isEcho(incoming, millis())) {
    if (!ownMoveHandled) {
      armKingPulse(indices, n, kOwnMoveHoldMs);
      ownMoveHandled = true;
      if (certaboIsConnected()) sendLeds();
    }
    return;
  }
  moveLights.add(indices, n, lightBoard);
  // Every square of this frame belongs to the move that just completed (that is
  // the only way MoveLights defers a whole frame): the host replaying the
  // human's own move. The completion clear above already emptied the mask, so
  // there is nothing new to show -- see the comment block above.
  if (moveLights.isSuppressed()) return;
  certabo::ledMask(indices, n, wantedLeds);
  if(certaboIsConnected()) sendLeds();
}
