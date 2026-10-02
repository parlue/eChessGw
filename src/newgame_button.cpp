#include "newgame_button.h"
#ifdef CHESSLINK_ENABLE_MACHINE
#include "machine_gateway.h"
#include <Preferences.h>
#include <atomic>

namespace {
const NimBLEUUID serviceId("773a0001-7e5b-4c2a-9d13-619b8e04f260");
const NimBLEUUID commandId("773a0002-7e5b-4c2a-9d13-619b8e04f260");
const NimBLEUUID controlId("773a0003-7e5b-4c2a-9d13-619b8e04f260");
enum Phase { Idle, Writing, Pending, Publishing, Done, Failed };
std::atomic<int> phase{Idle};
std::atomic<bool> busy{false}, permitted{false}, accepting{false};
uint8_t request[9] = {};
uint32_t lastSearch = 0, publishAt = 0;
Preferences journal;
bool storageReady = false, storageAttempted = false;
// Persist intent before changing state. An interrupted operation is never
// blindly executed again or falsely acknowledged after a power failure.
struct Record { uint8_t id[8]; uint8_t completed; };

void notification(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  if (!accepting.load() || !permitted.load() || length != 9 || data[0] != 1) return;
  bool nonzero = false;
  for (size_t i = 1; i < 9; ++i) nonzero |= data[i] != 0;
  if (!nonzero) return;
  int expected = Idle;
  if (!phase.compare_exchange_strong(expected, Writing)) return;
  memcpy(request, data, 9);
  phase.store(Pending);
}

// Scan a full window, giving known boards priority over the phone regardless
// of advertisement order. Used both before connection and during its wait.
bool scan(NimBLEAddress& phone, bool& foundPhone, uint32_t duration) {
  auto scanner = NimBLEDevice::getScan();
  scanner->setActiveScan(true);
  auto results = scanner->getResults(duration, false);
  bool realBoard = false;
  foundPhone = false;
  for (int i = 0; i < results.getCount(); ++i) {
    auto device = results.getDevice(i);
    if (newgameButtonKnownBoard(device)) realBoard = true;
    if (device->isAdvertisingService(serviceId)) { phone = device->getAddress(); foundPhone = true; }
  }
  scanner->clearResults();
  return realBoard;
}

void worker(void*) {
  NimBLEClient* client = nullptr;
  do {
    NimBLEAddress address;
    bool found = false;
    if (scan(address, found, 2000) || !found || !permitted.load()) break;
    client = NimBLEDevice::createClient();
    if (!client) break;
    client->setConnectTimeout(4000);
    if (!client->connect(address) || !permitted.load()) break;
    auto service = client->getService(serviceId);
    if (!service) break;
    auto command = service->getCharacteristic(commandId);
    auto control = service->getCharacteristic(controlId);
    if (!command || !control || !command->canNotify() || !control->canWrite()) break;
    phase.store(Idle);
    accepting.store(true);
    if (!command->subscribe(true, notification)) break;
    const uint8_t ready[] = {0x10, 1};
    if (!permitted.load() || !control->writeValue(ready, sizeof(ready), true)) break;
    Serial.println("[NEWGAME] phone connected and ready");
    uint32_t started = millis(), scannedAt = started;
    while (client->isConnected() && permitted.load() && millis() - started < 60000) {
      const int state = phase.load();
      if (state == Done) {
        uint8_t ack[9]; memcpy(ack, request, 9); ack[0] = 0x81;
        const bool ok = control->writeValue(ack, sizeof(ack), true);
        Serial.printf("[NEWGAME] confirmation %s\r\n", ok ? "delivered" : "failed; same ID can be retried");
        vTaskDelay(pdMS_TO_TICKS(500));
        break;
      }
      if (state == Failed) break;
      if (state == Idle && millis() - scannedAt >= 3000) {
        bool ignored;
        if (scan(address, ignored, 1000)) {
          permitted.store(false);
          Serial.println("[NEWGAME] known board nearby; phone disabled");
          break;
        }
        scannedAt = millis();
      }
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  } while (false);
  accepting.store(false);
  if (client) {
    if (client->isConnected()) client->disconnect();
    NimBLEDevice::deleteClient(client);
  }
  busy.store(false);
  vTaskDelete(nullptr);
}
}

bool newgameButtonBusy() { return busy.load(); }

void newgameButtonPoll(bool boardSearchBusy) {
  if (!storageAttempted) {
    storageAttempted = true;
    storageReady = journal.begin("newgame-btn", false);
  }
  const bool allowed = storageReady && machineGatewayNewgameAllowed();
  // A worker may revoke permission after seeing a physical board. Do not
  // re-enable that session; the next full scan decides eligibility again.
  if (!allowed) permitted.store(false);
  if (phase.load() == Pending) {
    if (!allowed || !permitted.load()) { phase.store(Failed); return; }
    Record previous{};
    if (journal.getBytesLength("last") == sizeof(previous)) journal.getBytes("last", &previous, sizeof(previous));
    if (!memcmp(previous.id, request + 1, 8)) {
      Serial.println(previous.completed ? "[NEWGAME] duplicate: acknowledge only" : "[NEWGAME] interrupted previous request: manual recovery required");
      phase.store(previous.completed ? Done : Failed);
      return;
    }
    Record next{}; memcpy(next.id, request + 1, 8);
    if (journal.putBytes("last", &next, sizeof(next)) != sizeof(next) || !machineGatewaySoftNewgame()) {
      phase.store(Failed); return;
    }
    publishAt = millis();
    phase.store(Publishing);
  }
  if (phase.load() == Publishing) {
    if (!allowed || !permitted.load()) { phase.store(Failed); return; }
    if (machineGatewayNewgamePublished()) {
      Record completed{}; memcpy(completed.id, request + 1, 8); completed.completed = 1;
      phase.store(journal.putBytes("last", &completed, sizeof(completed)) == sizeof(completed) ? Done : Failed);
    } else if (millis() - publishAt >= 5000) phase.store(Failed);
  }
  const int state = phase.load();
  if (state == Done || state == Failed) machineGatewayNewgameFinish();
  if (!busy.load() && state != Publishing && state != Pending && state != Writing &&
      allowed && !boardSearchBusy && millis() - lastSearch >= 3500) {
    lastSearch = millis();
    permitted.store(true);
    busy.store(true);
    phase.store(Idle);
    if (xTaskCreate(worker, "newgame-phone", 6144, nullptr, 1, nullptr) != pdPASS) busy.store(false);
  }
}
#endif
