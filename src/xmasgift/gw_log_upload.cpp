#include <LittleFS.h>
#include <NimBLEDevice.h>

#include "gw_log.h"
#include "gw_log_upload.h"
#include "pgn_recorder.h"
#include "gw_serial.h"  // keep last: Serial -> log-recording wrapper

namespace gwlog {

const char kTagName[] = "eChessGateway";

namespace {

const char kService[] = "e5c40000-6c4f-4a3b-9d2e-6b1a7f0c0001";
const char kLogChar[] = "e5c40001-6c4f-4a3b-9d2e-6b1a7f0c0001";
const char kCmdChar[] = "e5c40002-6c4f-4a3b-9d2e-6b1a7f0c0001";

portMUX_TYPE cmdMux = portMUX_INITIALIZER_UNLOCKED;
char cmdBuf[64];
size_t cmdLen = 0;

void onCommand(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  portENTER_CRITICAL(&cmdMux);
  for (size_t i = 0; i < length && cmdLen < sizeof(cmdBuf) - 1; ++i) cmdBuf[cmdLen++] = static_cast<char>(data[i]);
  portEXIT_CRITICAL(&cmdMux);
}

// Write without response; the controller can be briefly full, so retry a few times.
bool sendBytes(NimBLERemoteCharacteristic* ch, const uint8_t* data, size_t length) {
  for (int attempt = 0; attempt < 20; ++attempt) {
    if (ch->writeValue(data, length, false)) return true;
    vTaskDelay(pdMS_TO_TICKS(15));
  }
  return false;
}

bool sendText(NimBLERemoteCharacteristic* ch, const char* text) {
  return sendBytes(ch, reinterpret_cast<const uint8_t*>(text), strlen(text));
}

// PGN download (2026-10-16): sends every saved game (oldest first) as plain PGN text:
//   "PGN1 <games> <bytes>\n", the games (each one followed by a line break), "#PGN-END\n"
// The games stay on the gateway until the app confirms that it saved them (command PGNDEL).
bool sendPgn(NimBLERemoteCharacteristic* ch, size_t chunk) {
  const int count = pgnRecorderSavedGameCount();
  size_t total = 0;
  char path[48];
  for (int i = 0; i < count; ++i) {
    if (!pgnRecorderGamePgnPathByIndex(i, path, sizeof(path))) continue;
    File f = LittleFS.open(path, "r");
    if (f) {
      total += f.size() + 1;
      f.close();
    }
  }
  char header[40];
  snprintf(header, sizeof(header), "PGN1 %d %u\n", count, static_cast<unsigned>(total));
  if (!sendText(ch, header)) return false;
  uint8_t buffer[180];
  size_t sent = 0;
  for (int i = 0; i < count; ++i) {
    if (!pgnRecorderGamePgnPathByIndex(i, path, sizeof(path))) continue;
    File f = LittleFS.open(path, "r");
    if (!f) continue;
    while (f.available()) {
      const size_t n = f.read(buffer, chunk < sizeof(buffer) ? chunk : sizeof(buffer));
      if (n == 0) break;
      if (!sendBytes(ch, buffer, n)) {
        f.close();
        return false;
      }
      sent += n;
      vTaskDelay(pdMS_TO_TICKS(10));  // pace the writes, the app's GATT server has small buffers
    }
    f.close();
    if (!sendText(ch, "\n")) return false;
  }
  const bool ok = sendText(ch, "#PGN-END\n");
  Serial.printf("[GWLOG] pgn download %s (%d game(s), %u bytes sent)\r\n", ok ? "finished" : "failed", count, static_cast<unsigned>(sent));
  return ok;
}

// Deletes every saved game; only called after the app confirmed that it saved them. Highest index first so the
// chronological slot lookup stays valid during the loop (same as the other delete-after-save paths).
int deleteAllGames() {
  const int count = pgnRecorderSavedGameCount();
  for (int i = count - 1; i >= 0; --i) pgnRecorderDeleteGameByIndex(i);
  Serial.printf("[GWLOG] pgn delete confirmed by the app -- deleted %d game(s)\r\n", count);
  return count;
}

// Pops one complete command line from cmdBuf, or returns false.
bool nextCommand(char* out, size_t outSize) {
  bool found = false;
  portENTER_CRITICAL(&cmdMux);
  for (size_t i = 0; i < cmdLen; ++i) {
    if (cmdBuf[i] == '\n') {
      size_t n = i < outSize - 1 ? i : outSize - 1;
      memcpy(out, cmdBuf, n);
      out[n] = 0;
      memmove(cmdBuf, cmdBuf + i + 1, cmdLen - i - 1);
      cmdLen -= i + 1;
      found = true;
      break;
    }
  }
  portEXIT_CRITICAL(&cmdMux);
  if (found) {  // strip a trailing CR
    size_t n = strlen(out);
    while (n > 0 && (out[n - 1] == '\r' || out[n - 1] == ' ')) out[--n] = 0;
  }
  return found;
}

}  // namespace

bool uploadTo(const NimBLEAddress& address) {
  Serial.printf("[GWLOG] tag found at %s -- uploading the stored log\r\n", address.toString().c_str());
  NimBLEClient* client = NimBLEDevice::createClient();
  client->setConnectTimeout(8000);
  client->setConnectionParams(12, 24, 0, 200);
  if (!client->connect(address)) {
    Serial.println("[GWLOG] connect to the tag app failed");
    NimBLEDevice::deleteClient(client);
    return false;
  }
  NimBLERemoteCharacteristic* logChar = nullptr;
  NimBLERemoteCharacteristic* cmdChar = nullptr;
  if (!client->getServices(true).empty()) {
    NimBLERemoteService* service = client->getService(kService);
    if (service) {
      logChar = service->getCharacteristic(kLogChar);
      cmdChar = service->getCharacteristic(kCmdChar);
    }
  }
  if (!logChar || !cmdChar) {
    Serial.println("[GWLOG] tag app has no log service");
    client->disconnect();
    NimBLEDevice::deleteClient(client);
    return false;
  }
  portENTER_CRITICAL(&cmdMux);
  cmdLen = 0;
  portEXIT_CRITICAL(&cmdMux);
  if (!cmdChar->canNotify() || !cmdChar->subscribe(true, onCommand)) {
    Serial.println("[GWLOG] could not subscribe to the tag app's command channel");
    client->disconnect();
    NimBLEDevice::deleteClient(client);
    return false;
  }

  const uint16_t mtu = client->getMTU();
  const size_t chunk = mtu > 23 ? (mtu - 3 < 180 ? mtu - 3 : 180) : 20;
  bool ok = true;
  {
    Reader reader;
    char header[112];
    snprintf(header, sizeof(header), "GWLOG1 %u rec=%d fw=%s up=%lu\n", static_cast<unsigned>(reader.totalBytes()),
             recording() ? 1 : 0, buildStamp(), static_cast<unsigned long>(millis()));
    ok = sendText(logChar, header);
    uint8_t buffer[180];
    size_t sent = 0;
    while (ok && client->isConnected()) {
      const size_t n = reader.read(buffer, chunk);
      if (n == 0) break;
      ok = sendBytes(logChar, buffer, n);
      sent += n;
      vTaskDelay(pdMS_TO_TICKS(10));  // pace the writes, the app's GATT server has small buffers
    }
    if (ok) ok = sendText(logChar, "\n#GWLOG-END\n");
    Serial.printf("[GWLOG] log upload %s (%u bytes sent)\r\n", ok ? "finished" : "failed", static_cast<unsigned>(sent));
  }

  // Commands from the app: it may switch recording on/off or delete the stored files.
  uint32_t until = millis() + 20000;
  while (ok && client->isConnected() && static_cast<int32_t>(millis() - until) < 0) {
    char cmd[32];
    if (nextCommand(cmd, sizeof(cmd))) {
      until = millis() + 10000;
      if (strcmp(cmd, "PGN") == 0) {  // answers with its own end marker instead of "#OK"
        ok = sendPgn(logChar, chunk);
        until = millis() + 30000;  // the app asks the user whether the games may be deleted (PGNDEL); it sends PING meanwhile
        continue;
      }
      if (strcmp(cmd, "REC1") == 0) setRecording(true);
      else if (strcmp(cmd, "REC0") == 0) setRecording(false);
      else if (strcmp(cmd, "CLR") == 0) clearStored();
      else if (strcmp(cmd, "PGNDEL") == 0) deleteAllGames();
      else if (strcmp(cmd, "BYE") == 0) break;
      else continue;
      char reply[40];
      snprintf(reply, sizeof(reply), "#OK %s\n", cmd);
      sendText(logChar, reply);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  client->disconnect();
  NimBLEDevice::deleteClient(client);
  return ok;
}

}  // namespace gwlog
