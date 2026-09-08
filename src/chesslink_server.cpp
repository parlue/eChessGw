#include "chesslink_server.h"

#include <NimBLEDevice.h>

#include <algorithm>
#include <cstring>

#include "chessnut_server.h"
#include "chesslink_status_refresh.h"

// Ported from CynusLink's own proven ChessLink-server role (real UUIDs,
// checksum/register handling, connection tuning) -- see chesslink_server.h.

namespace {

constexpr char kName[] = "MILLENNIUM CHESS";
constexpr char kServiceUuid[] = "49535343-fe7d-4ae5-8fa9-9fafd205e455";
constexpr char kTxUuid[] = "49535343-1e4d-4bd9-ba61-23c647249616";
constexpr char kRxUuid[] = "49535343-8841-43f4-a8d4-ecbe34729bb3";

NimBLEServer* server = nullptr;
NimBLECharacteristic* txChar = nullptr;
NimBLECharacteristic* rxChar = nullptr;
bool started = false;
bool connected = false;
bool notifyEnabled = false;
uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;

// True once a write arrives via chessnut_server.cpp's own bridge (its
// MainWriteCallbacks::onWrite) -- a real ChessLink client (BC/Chess Dojo)
// discovered/landed on Chessnut's characteristics instead of this module's
// own, since both services are always simultaneously present in the GATT
// table (unavoidable -- see chesslinkServerInit()'s own comment) and a
// multi-protocol client can probe/land on either. While true, every reply
// for this session routes out via chessnutServerWriteRawFrame() instead of
// this module's own TX characteristic. Reset on disconnect.
bool bridgedViaChessnut = false;

// Small EEPROM-style register set a real Mode-B board exposes via 'R'/'W'.
// Defaults match CynusLink's own reset defaults (register[1]=0x14 is the
// "board scan time" register other Mode-B hosts read to learn our auto-
// report interval).
uint8_t registers[256] = {};

uint8_t cachedStatus[kModeBStatusFrameLength] = {};
bool haveCachedStatus = false;
uint8_t lastSentStatus[kModeBStatusFrameLength] = {};
bool haveSentStatus = false;
ChesslinkStatusRefresh statusRefresh;

constexpr size_t kMaxNotifyChunk = 64;
struct RawPacket { uint8_t length; uint8_t data[kMaxNotifyChunk]; };
QueueHandle_t rxQueue = nullptr;

uint8_t rxFrame[kFrameBufferSize];
size_t rxFrameLength = 0;

void resetRegisters() {
  memset(registers, 0, sizeof(registers));
  registers[0] = 0x00;
  registers[1] = 0x14;
  registers[2] = 0x00;
  registers[4] = 0x0F;
}

// Sends a logical (plain 7-bit ASCII, no cable parity encoding -- that's a
// physical-RS232-only concern) Mode-B frame, checksummed with the plain
// convention real ChessLink software always expects over BLE, chunked to
// the negotiated peer MTU exactly like CynusLink's own sendCL().
size_t writeFrame(const uint8_t* logicalFrame, size_t length) {
  // Log the reason once per transition instead of silently no-op'ing --
  // chessnut_server.cpp's sendBoardFrame() already does this (its own
  // comment: "nothing happened" was indistinguishable from "we never even
  // tried" from the log alone). chesslink_server.cpp never got the same
  // treatment, which left the BearChess "no LED/status update ever arrives"
  // report (2026-08-31) with no way to tell whether a status change simply
  // never made it out because no client was connected/subscribed yet.
  enum class SendBlock { None, NotInitialized, NotConnected, NotSubscribed, BridgedNotSubscribed };
  static SendBlock lastLoggedBlock = SendBlock::None;
  auto logOnce = [](SendBlock block, const char* message) {
    if (block == lastLoggedBlock) return;
    lastLoggedBlock = block;
    Serial.println(message);
  };

  uint8_t full[kFrameBufferSize];
  memcpy(full, logicalFrame, length);
  computeModeBChecksumHex(full + length, full, length, /*useEncodedConvention=*/false);
  const size_t total = length + 2;

  if (bridgedViaChessnut) {
    // See bridgedViaChessnut's own comment -- the client is actually
    // subscribed to Chessnut's own TX characteristic, not this module's own,
    // and never sent via this module's txChar/connected/notifyEnabled at all.
    if (!chessnutServerNotifyEnabled()) {
      logOnce(SendBlock::BridgedNotSubscribed,
              "[CHESSLINK] frame ready but bridged client has not subscribed yet -- not sent");
      return 0;
    }
    lastLoggedBlock = SendBlock::None;
    if (chessnutServerWriteRawFrame(full, total) != total) {
      Serial.println("[CHESSLINK] bridged notify failed");
      return 0;
    }
    Serial.printf("[CHESSLINK] %c frame notified to bridged client (%u bytes)\r\n",
                  static_cast<char>(logicalFrame[0]), static_cast<unsigned>(total));
    return total;
  }

  if (txChar == nullptr) {
    logOnce(SendBlock::NotInitialized, "[CHESSLINK] frame ready but server not initialized -- not sent");
    return 0;
  }
  if (!connected) {
    logOnce(SendBlock::NotConnected, "[CHESSLINK] frame ready but no client connected -- not sent");
    return 0;
  }
  if (!notifyEnabled) {
    logOnce(SendBlock::NotSubscribed,
            "[CHESSLINK] frame ready but client has not subscribed to notifications yet -- not sent");
    return 0;
  }
  lastLoggedBlock = SendBlock::None;

  uint16_t mtu = 23;
  if (server != nullptr && connHandle != BLE_HS_CONN_HANDLE_NONE) {
    const uint16_t peerMtu = server->getPeerMTU(connHandle);
    if (peerMtu >= 23) mtu = peerMtu;
  }
  const size_t maxPayload = mtu > 3 ? static_cast<size_t>(mtu - 3) : 20;

  // Diagnostic, 2026-09-08: unlike chessnut_server.cpp's own sendBoardFrame()
  // ("board frame notified to client"), this function never logged anything
  // on a SUCCESSFUL send -- only the three blocked-reason cases above. That
  // made it impossible to tell "the notify actually went out" from "nothing
  // in this function ever ran" purely from the log, while chasing "client
  // connects but never sees a move" (BC/Chess Dojo, same session). Also logs
  // the actual MTU-based chunk count -- kModeBStatusFrameLength (67 bytes)
  // exceeds a single chunk at the BLE default/minimum MTU (23, giving 20
  // payload bytes), and multiple notify() calls for one logical frame do NOT
  // reassemble on the receiving end (no such protocol exists for BLE
  // notifications) -- confirmed this exact bug independently in this same
  // session's Chessnut-bridge code. If MTU is ever still at its default here
  // when a status frame is due, this send silently corrupts every one.
  const size_t chunkCount = (total + maxPayload - 1) / maxPayload;
  if (chunkCount > 1) {
    Serial.printf("[CHESSLINK] WARNING: sending %u-byte frame in %u notify() chunks (mtu=%u) -- "
                  "BLE notifications do not reassemble across multiple notify() calls, this will "
                  "likely arrive corrupted\r\n",
                  static_cast<unsigned>(total), static_cast<unsigned>(chunkCount), mtu);
  }
  for (size_t offset = 0; offset < total; offset += maxPayload) {
    const size_t count = std::min(maxPayload, total - offset);
    txChar->setValue(full + offset, count);
    if (!txChar->notify()) {
      Serial.println("[CHESSLINK] notify failed");
      return 0;
    }
    if (offset + count < total) delay(8);
  }
  Serial.printf("[CHESSLINK] %c frame notified to client (%u bytes, mtu=%u)\r\n",
                static_cast<char>(logicalFrame[0]), static_cast<unsigned>(total), mtu);
  return total;
}

bool sendStatus(const char* reason) {
  if (!haveCachedStatus) return false;
  Serial.printf("[CHESSLINK] status send requested: %s\r\n", reason);
  // The status frame length includes its two checksum characters.
  // writeFrame appends a fresh checksum after the command and 64 squares.
  if (writeFrame(cachedStatus, kModeBStatusFrameLength - 2) != kModeBStatusFrameLength) return false;
  memcpy(lastSentStatus, cachedStatus, sizeof(lastSentStatus));
  haveSentStatus = true;
  return true;
}

bool hexNibble(uint8_t c, int& value) {
  c &= 0x7f;
  if (c >= '0' && c <= '9') { value = c - '0'; return true; }
  if (c >= 'A' && c <= 'F') { value = c - 'A' + 10; return true; }
  if (c >= 'a' && c <= 'f') { value = c - 'a' + 10; return true; }
  return false;
}

bool hexByte(uint8_t hi, uint8_t lo, uint8_t& out) {
  int h, l;
  if (!hexNibble(hi, h) || !hexNibble(lo, l)) return false;
  out = static_cast<uint8_t>((h << 4) | l);
  return true;
}

void appendHex(uint8_t* out, size_t& pos, uint8_t value) {
  static constexpr char hex[] = "0123456789ABCDEF";
  out[pos++] = static_cast<uint8_t>(hex[value >> 4]);
  out[pos++] = static_cast<uint8_t>(hex[value & 0x0F]);
}

// Handles one complete, checksum-validated Mode-B command from the
// connected ChessLink client -- CynusLink's own handleCL(), adapted to
// this project's shared per-board dispatch (dispatchLedFrameToBoard()/
// clearBoardLeds()) instead of driving a single hardcoded board type.
void handleFrame(const uint8_t* frame, size_t length) {
  switch (frame[0]) {
    case 'S':
      if (sendStatus("S command")) statusRefresh.reset();
      break;
    case 'V':
      writeFrame(reinterpret_cast<const uint8_t*>("v0100"), 5);
      break;
    case 'X':
      clearBoardLeds(currentBoardType());
      writeFrame(reinterpret_cast<const uint8_t*>("x"), 1);
      break;
    case 'T':
      resetRegisters();
      haveSentStatus = false;
      statusRefresh.reset();
      Serial.println("[CHESSLINK] Magic Board reset command received; register defaults restored");
      break;
    case 'R': {
      uint8_t addr;
      if (!hexByte(frame[1], frame[2], addr)) break;
      uint8_t reply[7];
      size_t pos = 0;
      reply[pos++] = 'r';
      appendHex(reply, pos, addr);
      appendHex(reply, pos, registers[addr]);
      writeFrame(reply, pos);
      break;
    }
    case 'W': {
      uint8_t addr, value;
      if (!hexByte(frame[1], frame[2], addr) || !hexByte(frame[3], frame[4], value)) break;
      registers[addr] = value;
      // A register write does not change the board position. Keep the
      // last-sent status so Poll does not inject a duplicate 's' after
      // this command's 'w' reply during the client's handshake.
      // Refresh once after the whole configuration burst has gone quiet;
      // BearChess can discard the subscription-time position while probing V.
      if (addr == 1 || addr == 2 || addr == 3) statusRefresh.schedule(millis());
      uint8_t reply[7];
      size_t pos = 0;
      reply[pos++] = 'w';
      appendHex(reply, pos, addr);
      appendHex(reply, pos, value);
      writeFrame(reply, pos);
      break;
    }
    case 'L': {
      // The checksum of a bare 'l' ack is content-independent, so answer
      // instantly instead of waiting on anything else -- matches
      // CynusLink's own sendCL("l") and the cable path's instant l6C ack.
      writeFrame(reinterpret_cast<const uint8_t*>("l"), 1);
      if (length != 167) break;
      Serial.println("[CHESSLINK] L frame received from client, dispatching to connected board");
      dispatchLedFrameToBoard(currentBoardType(), frame);
      break;
    }
    default:
      break;
  }
}

class ServerCallbacks final : public NimBLEServerCallbacks {
 public:
  void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
    // chesslinkServerInit() and chessnutServerInit() both run unconditionally
    // at boot and share one NimBLEServer (NimBLEDevice::createServer() is a
    // singleton) -- so this service's characteristics stay live and
    // connectable even when Chessnut masquerade was the one actually
    // selected via the BT-BT queen gesture. A client that already knows
    // this device's address/UUIDs from an earlier session (BLE has no
    // per-service "not advertised" enforcement once directly connected)
    // could connect here anyway and see total silence with no explanation.
    // Reject outright instead -- confirmed real-hardware case, 2026-09-04.
    if (!started) {
      Serial.printf("[CHESSLINK] rejecting connection from %s -- ChessLink masquerade was not "
                    "selected this session (Chessnut was); disconnecting\r\n",
                    info.getAddress().toString().c_str());
      if (server != nullptr) server->disconnect(info.getConnHandle());
      return;
    }
    connected = true;
    statusRefresh.reset();
    connHandle = info.getConnHandle();
    haveSentStatus = false;
    bridgedViaChessnut = false;  // determined per-write, see chesslinkServerHandleExternalWrite()
    Serial.printf("[CHESSLINK] client connected %s\r\n", info.getAddress().toString().c_str());
    if (server != nullptr) {
      // Fast connection interval, matching CynusLink's own proven tuning --
      // King's/a real ChessLink host's acceptance window is narrow.
      server->updateConnParams(connHandle, 12, 24, 0, 200);
    }
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
    Serial.printf("[CHESSLINK] client disconnected (%d)\r\n", reason);
    connected = false;
    notifyEnabled = false;
    connHandle = BLE_HS_CONN_HANDLE_NONE;
    rxFrameLength = 0;
    statusRefresh.reset();
    if (bridgedViaChessnut) {
      // chessnut_server.cpp's own onDisconnect never runs while ChessLink
      // owns the connection -- see chessnutServerResetBridgeState()'s own
      // comment for why this explicit call is needed instead.
      chessnutServerResetBridgeState();
      bridgedViaChessnut = false;
    }
    NimBLEDevice::startAdvertising();
  }

  // Ported from chessnut_server.cpp's own ServerCallbacks -- this module
  // never had the same instrumentation, leaving the whole "link connected"
  // to "client starts using GATT" window invisible here specifically.
  // Added 2026-09-08 while chasing "BC/Chess Dojo connect but never play".
  void onMTUChange(uint16_t mtu, NimBLEConnInfo& info) override {
    Serial.printf("[CHESSLINK] MTU changed to %u for %s\r\n", mtu,
                  info.getAddress().toString().c_str());
  }
  void onConnParamsUpdate(NimBLEConnInfo& info) override {
    Serial.printf("[CHESSLINK] connection parameters updated for %s\r\n",
                  info.getAddress().toString().c_str());
  }
  void onPhyUpdate(NimBLEConnInfo& info, uint8_t txPhy, uint8_t rxPhy) override {
    Serial.printf("[CHESSLINK] PHY updated for %s: tx=%u rx=%u\r\n",
                  info.getAddress().toString().c_str(), txPhy, rxPhy);
  }
};

class TxCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t subValue) override {
    // Per-characteristic callbacks are bound unconditionally in Init(), so
    // this fires even when Chessnut (not ChessLink) was the mode actually
    // selected -- the server-level onConnect guard above doesn't cover a
    // client that connects generically and only then subscribes to this
    // specific, non-selected service's characteristic. See that guard's own
    // comment for the full real-hardware story.
    if (!started) {
      notifyEnabled = subValue != 0;
      Serial.printf("[CHESSLINK] client %s notifications on tx characteristic (ChessLink masquerade "
                    "not selected -- tracked only for the Chessnut bridge)\r\n",
                    notifyEnabled ? "enabled" : "disabled");
      return;
    }
    notifyEnabled = subValue != 0;
    Serial.printf("[CHESSLINK] client %s notifications on tx characteristic\r\n",
                  notifyEnabled ? "enabled" : "disabled");
    if (notifyEnabled) haveSentStatus = false;
  }
};

class RxCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
    // Only pushes raw bytes into a queue -- runs on the BLE host task's own
    // (small) stack, matching every other board driver's own established
    // stack-overflow-avoidance pattern in this project.
    const std::string rawValue = characteristic->getValue();
    // TEMPORARY diagnostic, 2026-09-04: BC reports it sent its reply move,
    // but no "[CHESSLINK] L frame received" ever showed up on the gateway --
    // logging every raw write here (even when rejected/ignored below) to see
    // whether it arrives at all. Remove once resolved.
    {
      char hex[3 * kMaxNotifyChunk + 1] = {};
      size_t pos = 0;
      for (size_t i = 0; i < rawValue.size() && pos + 3 < sizeof(hex); ++i) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", static_cast<uint8_t>(rawValue[i]));
      }
      Serial.printf("[CHESSLINK RX WRITE] started=%d len=%u bytes=%s\r\n", started ? 1 : 0,
                    static_cast<unsigned>(rawValue.size()), hex);
    }
    if (!started) {
      if (!rawValue.empty() && modeBCommandLength(static_cast<uint8_t>(rawValue[0]) & 0x7f) == 0) {
        chessnutServerHandleExternalWrite(
            reinterpret_cast<const uint8_t*>(rawValue.data()), rawValue.size());
      }
      return;
    }
    if (rxQueue == nullptr) return;
    const std::string value = characteristic->getValue();
    size_t offset = 0;
    while (offset < value.size()) {
      RawPacket packet{};
      packet.length = static_cast<uint8_t>(std::min(value.size() - offset, kMaxNotifyChunk));
      memcpy(packet.data, value.data() + offset, packet.length);
      xQueueSend(rxQueue, &packet, 0);
      offset += packet.length;
    }
  }
};

ServerCallbacks serverCallbacks;
TxCallbacks txCallbacks;
RxCallbacks rxCallbacks;

void processByte(uint8_t raw) {
  rxFrame[rxFrameLength++] = raw & 0x7f;
  while (rxFrameLength > 0) {
    const size_t expected = modeBCommandLength(rxFrame[0]);
    if (expected == 0) {
      memmove(rxFrame, rxFrame + 1, --rxFrameLength);
      continue;
    }
    if (rxFrameLength < expected) break;
    if (modeBValidBlock(rxFrame, expected)) {
      handleFrame(rxFrame, expected);
    } else {
      Serial.printf("[CHESSLINK] rejected %c frame: bad checksum\r\n", static_cast<char>(rxFrame[0] & 0x7f));
    }
    rxFrameLength -= expected;
    memmove(rxFrame, rxFrame + expected, rxFrameLength);
  }
  if (rxFrameLength == kFrameBufferSize) rxFrameLength = 0;
}

}  // namespace

bool initialized = false;

void chesslinkServerInit() {
  if (initialized) return;
  resetRegisters();
  rxQueue = xQueueCreate(32, sizeof(RawPacket));

  // No explicit setMTU() call -- an unjustified copy-paste of CynusLink's
  // own value (128), never verified for this Chessnut-side masquerade;
  // cer2nut (the actual Chessnut reference) never requests an MTU at all.
  // Removed 2026-09-05.

  server = NimBLEDevice::createServer();
  NimBLEService* service = server->createService(kServiceUuid);
  txChar = service->createCharacteristic(kTxUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  rxChar = service->createCharacteristic(kRxUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  txChar->setCallbacks(&txCallbacks);
  rxChar->setCallbacks(&rxCallbacks);
  server->start();

  initialized = true;
}

void chesslinkServerStart() {
  if (!initialized || started) return;
  // Claim the shared server-level callbacks and advertising fields only
  // now, not in chesslinkServerInit() -- both this module and
  // chessnut_server.cpp's own Init() run unconditionally at boot (before
  // BT-BT mode selection is known), but NimBLEServer has only one callback
  // slot and NimBLEDevice has only one active advertising payload at a
  // time. Whichever masquerade mode is actually selected is the only one
  // that ever calls its own Start(), so claiming both here -- rather than
  // at Init() -- is what keeps the two modes from clobbering each other.
  server->setCallbacks(&serverCallbacks);

  // Explicit, separate primary/scan-response packets -- NOT the implicit
  // enableScanResponse(true)+setName()+addServiceUUID() convenience
  // sequence this used before, which relies on NimBLE's own internal
  // auto-splitting logic to put the UUID (18 bytes) and name (18 bytes,
  // together 36 > the 31-byte legacy primary-packet limit) into the right
  // packet -- never independently verified to actually do so. Matches
  // VirtualT2's own proven-working Android GATT server (confirmed reliably
  // connecting to BC in ~2s): primary packet carries only the service UUID,
  // scan response carries only the name.
  NimBLEAdvertisementData advertisementData;
  advertisementData.addServiceUUID(kServiceUuid);
  NimBLEAdvertisementData scanResponseData;
  scanResponseData.setName(kName);

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setAdvertisementData(advertisementData);
  advertising->setScanResponseData(scanResponseData);
  NimBLEDevice::startAdvertising();

  started = true;
  Serial.println("[CHESSLINK] advertising as MILLENNIUM CHESS");
}

bool chesslinkServerConnected() { return connected; }

void chesslinkServerPoll() {
  if (!started) return;
  RawPacket packet;
  while (rxQueue != nullptr && xQueueReceive(rxQueue, &packet, 0) == pdTRUE) {
    statusRefresh.noteTraffic(millis());
    for (uint8_t i = 0; i < packet.length; ++i) processByte(packet.data[i]);
  }

  // Active 1s health-check watchdog, ported from CynusLink's own proven
  // processSupervision() (2026-09-04): don't rely solely on onConnect/
  // onDisconnect callbacks firing -- if the BLE stack ever silently misses a
  // disconnect event (a known, if rare, real-world BLE edge case),
  // connected/notifyEnabled could stay stuck true forever with nothing to
  // self-correct it, and advertising could silently stop without anything
  // noticing and restarting it. This polls the actual BLE stack state
  // directly instead, self-healing within at most 1s regardless of whether
  // any callback ever fired.
  static uint32_t lastHealthCheckAt = 0;
  if (static_cast<uint32_t>(millis() - lastHealthCheckAt) >= 1000) {
    lastHealthCheckAt = millis();
    if (connected && server != nullptr && server->getConnectedCount() == 0) {
      Serial.println("[CHESSLINK] health-check: connected flag was stale (0 actual clients) -- resetting");
      connected = false;
      notifyEnabled = false;
      connHandle = BLE_HS_CONN_HANDLE_NONE;
      statusRefresh.reset();
    }
    if (!connected) {
      NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
      if (advertising != nullptr && !advertising->isAdvertising()) {
        Serial.println("[CHESSLINK] health-check: advertising was inactive -- restarting");
        NimBLEDevice::startAdvertising();
      }
    }
  }

  const bool subscribedEitherWay =
      (connected && notifyEnabled) || (bridgedViaChessnut && chessnutServerNotifyEnabled());
  // Never insert the refresh into a partially received command. A failed
  // notification leaves it pending and spaces retries by the same grace period.
  if (subscribedEitherWay && haveCachedStatus && rxFrameLength == 0 &&
      statusRefresh.due(millis())) {
    if (sendStatus("configuration settled")) statusRefresh.reset();
    else statusRefresh.noteTraffic(millis());
  }
  if (subscribedEitherWay && haveCachedStatus &&
      (!haveSentStatus || memcmp(lastSentStatus, cachedStatus, sizeof(cachedStatus)) != 0)) {
    sendStatus(haveSentStatus ? "poll: position changed" : "poll: initial/reset status");
  }
}

void chesslinkServerPublishStatus(const uint8_t frame[kModeBStatusFrameLength]) {
  // Preserve Mode-B wire order (h1..a1 through h8..a8) over BLE too.
  // BearChess reverses this order when constructing its FEN. Its receive
  // log on 2026-09-08 confirmed that sending FEN order here instead placed
  // white at the top and turned the real e2-e4 move into d7-d5.
  // sendStatus passes only the 65 payload bytes to writeFrame, which
  // computes the outgoing checksum independently of the cable checksum.
  memcpy(cachedStatus, frame, sizeof(cachedStatus));
  haveCachedStatus = true;
  // Immediate forward, mirroring the cable path's own immediate-forward
  // behavior in onBoardStatusFrame() -- chesslinkServerPoll()'s own
  // resend-on-change check then has nothing new to do until the position
  // changes again. Also covers the bridged case (see bridgedViaChessnut) --
  // this module's own connected/notifyEnabled stay false there, so checking
  // only those would silently skip every send for a bridged client.
  if ((connected && notifyEnabled) || (bridgedViaChessnut && chessnutServerNotifyEnabled())) {
    sendStatus("board status published");
  }
}

size_t chesslinkServerWriteFrame(const uint8_t* logicalFrame, size_t length) {
  return writeFrame(logicalFrame, length);
}

bool chesslinkServerNotifyEnabled() { return notifyEnabled; }

size_t chesslinkServerWriteRawFrame(const uint8_t* rawFrame, size_t length) {
  if (txChar == nullptr || !notifyEnabled) return 0;
  txChar->setValue(rawFrame, length);
  txChar->notify();
  return length;
}

void chesslinkServerResetBridgeState() {
  notifyEnabled = false;
}

void chesslinkServerHandleExternalWrite(const uint8_t* data, size_t length) {
  if (!started) return;  // ChessLink mode isn't the one actually selected either
  if (rxQueue == nullptr || length == 0) return;
  // A write arriving here means the connected client is a ChessLink one
  // that ended up on Chessnut's characteristics instead of this module's
  // own (see bridgedViaChessnut's own comment) -- the shared NimBLEServer-
  // level ServerCallbacks::onConnect() for THIS module already ran (it owns
  // the callback slot whenever ChessLink mode is selected, confirmed by
  // "[CHESSLINK] client connected" already having logged), so `connected`
  // is already true; only the bridge flag and reply routing need setting.
  bridgedViaChessnut = true;
  size_t offset = 0;
  while (offset < length) {
    RawPacket packet{};
    packet.length = static_cast<uint8_t>(std::min(length - offset, kMaxNotifyChunk));
    memcpy(packet.data, data + offset, packet.length);
    xQueueSend(rxQueue, &packet, 0);
    offset += packet.length;
  }
}
