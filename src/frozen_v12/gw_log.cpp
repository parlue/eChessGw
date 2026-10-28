#define GW_NO_SERIAL_MACRO  // this file talks to the real Serial
#include "gw_serial.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_system.h>

#include "gw_log.h"

GwSerialTee gwSerial(Serial);

namespace gwlog {
namespace {

constexpr size_t kRingSize = 12288;
constexpr size_t kLineMax = 160;
constexpr size_t kFileMax = 49152;
constexpr size_t kFlushChunk = 4096;
constexpr uint32_t kFlushBytes = 3072;
constexpr uint32_t kFlushIntervalMs = 60000;
constexpr uint32_t kQuietMs = 150;
constexpr uint32_t kForceBytes = 8192;
constexpr uint32_t kForceIntervalMs = 300000;
constexpr uint32_t kAutoOffMs = 2UL * 60UL * 60UL * 1000UL;
constexpr uint8_t kMaxBootsRecording = 3;
constexpr const char* kPaths[2] = {"/gwlog0.txt", "/gwlog1.txt"};

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
uint8_t ring[kRingSize];
uint64_t total = 0;      // bytes ever written into the ring
uint64_t persisted = 0;  // of those, bytes already in flash
char line[kLineMax + 2];
size_t lineLen = 0;
bool lineCut = false;
volatile bool captureOn = true;
bool began = false;
bool recording_ = false;
uint32_t recordingSince = 0;
uint32_t lastFlushAt = 0;
uint32_t lastRxAt = 0;
uint32_t lastRxCounter = 0;
uint8_t fileIdx = 0;
uint32_t fileSeq = 0;
bool fsReady = false;
SemaphoreHandle_t fileMutex = nullptr;
uint8_t flushBuf[kFlushChunk];

// Caller holds `mux`.
void pushLocked(const char* text, size_t len) {
  for (size_t i = 0; i < len; ++i) ring[(total + i) % kRingSize] = static_cast<uint8_t>(text[i]);
  total += len;
}

// Caller holds `mux`.
void finishLineLocked() {
  if (lineLen == 0) { lineCut = false; return; }
  line[lineLen] = 0;
  const bool noise = strncmp(line, "[HEARTBEAT]", 11) == 0 || strncmp(line, "Gateway: BLE=", 13) == 0;
  if (!noise) {
    char prefix[16];
    const int n = snprintf(prefix, sizeof(prefix), "%lu ", static_cast<unsigned long>(millis()));
    pushLocked(prefix, n > 0 ? static_cast<size_t>(n) : 0);
    pushLocked(line, lineLen);
    if (lineCut) pushLocked("~", 1);
    pushLocked("\n", 1);
  }
  lineLen = 0;
  lineCut = false;
}

const char* resetName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int-wdt";
    case ESP_RST_TASK_WDT: return "task-wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "brownout";
    default: return "other";
  }
}

void saveFilePrefs() {
  Preferences prefs;
  if (!prefs.begin("gwlog", false)) return;
  prefs.putUChar("idx", fileIdx);
  prefs.putUInt("seq", fileSeq);
  prefs.end();
}

bool ensureFs() {
  if (!fsReady) fsReady = LittleFS.begin(/*formatOnFail=*/true);
  return fsReady;
}

size_t fileSize(const char* path) {
  if (!LittleFS.exists(path)) return 0;
  File f = LittleFS.open(path, "r");
  if (!f) return 0;
  const size_t n = f.size();
  f.close();
  return n;
}

// Caller holds fileMutex. Picks the active file, rotating when it is full.
const char* ensureFile() {
  if (!ensureFs()) return nullptr;
  if (fileSize(kPaths[fileIdx]) >= kFileMax) {
    fileIdx ^= 1;
    fileSeq++;
    LittleFS.remove(kPaths[fileIdx]);
    saveFilePrefs();
  }
  const char* path = kPaths[fileIdx];
  if (fileSize(path) == 0) {
    File f = LittleFS.open(path, "w");
    if (!f) return nullptr;
    f.printf("# gwlog seq=%lu\n", static_cast<unsigned long>(fileSeq));
    f.close();
  }
  return path;
}

bool flushOnce() {
  size_t n = 0;
  portENTER_CRITICAL(&mux);
  const uint64_t pending = total - persisted;
  if (pending > kRingSize) persisted = total - kRingSize;  // overwritten before it could be saved
  n = static_cast<size_t>(total - persisted);
  if (n > kFlushChunk) n = kFlushChunk;
  for (size_t i = 0; i < n; ++i) flushBuf[i] = ring[(persisted + i) % kRingSize];
  portEXIT_CRITICAL(&mux);
  if (n == 0) return true;

  bool ok = false;
  if (fileMutex && xSemaphoreTake(fileMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    const char* path = ensureFile();
    if (path) {
      File f = LittleFS.open(path, FILE_APPEND);
      if (f) {
        ok = f.write(flushBuf, n) == n;
        f.close();
      }
    }
    xSemaphoreGive(fileMutex);
  }
  if (ok) {
    portENTER_CRITICAL(&mux);
    persisted += n;
    portEXIT_CRITICAL(&mux);
  } else {
    Serial.println("[GWLOG] flash write failed; recording switched off");
    recording_ = false;
  }
  return ok;
}

}  // namespace

void begin() {
  if (began) return;
  began = true;
  fileMutex = xSemaphoreCreateMutex();
  bool autoOff = false;
  {
    Preferences prefs;
    if (prefs.begin("gwlog", false)) {
      recording_ = prefs.getBool("rec", false);
      uint8_t boots = prefs.getUChar("boots", 0);
      fileIdx = prefs.getUChar("idx", 0) & 1;
      fileSeq = prefs.getUInt("seq", 0);
      if (recording_) {
        boots++;
        if (boots > kMaxBootsRecording) { recording_ = false; autoOff = true; boots = 0; prefs.putBool("rec", false); }
        prefs.putUChar("boots", boots);
      } else if (boots) {
        prefs.putUChar("boots", 0);
      }
      prefs.end();
    }
  }
  recordingSince = millis();
  lastFlushAt = millis();
  char text[96];
  const int n = snprintf(text, sizeof(text), "=== BOOT reset=%s rec=%d%s fw=%s ===", resetName(esp_reset_reason()),
                         recording_ ? 1 : 0, autoOff ? " (auto-off after restarts)" : "", buildStamp());
  portENTER_CRITICAL(&mux);
  char prefix[16];
  const int p = snprintf(prefix, sizeof(prefix), "%lu ", static_cast<unsigned long>(millis()));
  pushLocked(prefix, p > 0 ? static_cast<size_t>(p) : 0);
  pushLocked(text, n > 0 ? static_cast<size_t>(n) : 0);
  pushLocked("\n", 1);
  portEXIT_CRITICAL(&mux);
}

void feed(const uint8_t* data, size_t len) {
  if (!captureOn || !began) return;
  portENTER_CRITICAL(&mux);
  for (size_t i = 0; i < len; ++i) {
    const uint8_t c = data[i];
    if (c == '\n') finishLineLocked();
    else if (c == '\r') continue;
    else if ((c >= 0x20 && c <= 0x7E) || c == '\t') {
      if (lineLen < kLineMax) line[lineLen++] = static_cast<char>(c);
      else lineCut = true;
    }
  }
  portEXIT_CRITICAL(&mux);
}

void capture(bool on) { captureOn = on; }

bool recording() { return recording_; }

void setRecording(bool on) {
  recording_ = on;
  recordingSince = millis();
  lastFlushAt = millis();
  Preferences prefs;
  if (prefs.begin("gwlog", false)) {
    prefs.putBool("rec", on);
    prefs.putUChar("boots", 0);
    prefs.end();
  }
  Serial.printf("[GWLOG] recording %s\r\n", on ? "ON (switches itself off after 3 restarts or 2 hours)" : "OFF");
}

void clearStored() {
  if (!ensureFs()) return;
  if (fileMutex && xSemaphoreTake(fileMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    LittleFS.remove(kPaths[0]);
    LittleFS.remove(kPaths[1]);
    fileIdx = 0;
    fileSeq = 0;
    saveFilePrefs();
    xSemaphoreGive(fileMutex);
  }
  Serial.println("[GWLOG] stored log files deleted");
}

void poll(uint32_t uartRxCounter) {
  if (!began) return;
  const uint32_t now = millis();
  if (uartRxCounter != lastRxCounter) { lastRxCounter = uartRxCounter; lastRxAt = now; }
  if (!recording_) return;
  if (now - recordingSince > kAutoOffMs) { setRecording(false); return; }
  uint64_t pending;
  portENTER_CRITICAL(&mux);
  pending = total - persisted;
  portEXIT_CRITICAL(&mux);
  if (pending == 0) return;
  const bool due = pending >= kFlushBytes || now - lastFlushAt >= kFlushIntervalMs;
  if (!due) return;
  const bool quiet = now - lastRxAt >= kQuietMs;
  const bool forced = pending >= kForceBytes || now - lastFlushAt >= kForceIntervalMs;
  if (!quiet && !forced) return;
  if (flushOnce()) lastFlushAt = millis();
}

// Rebuild diagnostic timestamp for the 2026-10-27 early-setup beta.
const char* buildStamp() { return __DATE__ " " __TIME__; }

// ---- Reader ----
Reader::Reader() {
  portENTER_CRITICAL(&mux);
  uint64_t pending = total - persisted;
  if (pending > kRingSize) pending = kRingSize;
  ramLen_ = static_cast<size_t>(pending);
  ram_ = ramLen_ ? static_cast<char*>(malloc(ramLen_)) : nullptr;
  if (ram_) {
    for (size_t i = 0; i < ramLen_; ++i) ram_[i] = static_cast<char>(ring[(total - ramLen_ + i) % kRingSize]);
  } else {
    ramLen_ = 0;
  }
  portEXIT_CRITICAL(&mux);
  if (ensureFs()) {
    const char* older = kPaths[fileIdx ^ 1];
    const char* newer = kPaths[fileIdx];
    if (LittleFS.exists(older)) { paths_[0] = older; total_ += fileSize(older); }
    if (LittleFS.exists(newer)) { paths_[1] = newer; total_ += fileSize(newer); }
  }
  total_ += ramLen_;
}

Reader::~Reader() {
  if (file_) { static_cast<File*>(file_)->close(); delete static_cast<File*>(file_); }
  free(ram_);
}

bool Reader::openStage() {
  if (file_) { static_cast<File*>(file_)->close(); delete static_cast<File*>(file_); file_ = nullptr; }
  if (stage_ <= 1 && paths_[stage_]) {
    File f = LittleFS.open(paths_[stage_], "r");
    if (!f) return false;
    file_ = new File(f);
  }
  return true;
}

size_t Reader::read(uint8_t* out, size_t max) {
  while (stage_ < 3) {
    if (stage_ <= 1) {
      if (!file_ && paths_[stage_] && !openStage()) { stage_++; continue; }
      if (!file_) { stage_++; continue; }
      size_t n = 0;
      if (fileMutex && xSemaphoreTake(fileMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        n = static_cast<File*>(file_)->read(out, max);
        xSemaphoreGive(fileMutex);
      }
      if (n > 0) return n;
      static_cast<File*>(file_)->close();
      delete static_cast<File*>(file_);
      file_ = nullptr;
      stage_++;
    } else {  // RAM part
      const size_t n = ramLen_ - ramPos_ < max ? ramLen_ - ramPos_ : max;
      if (n > 0) { memcpy(out, ram_ + ramPos_, n); ramPos_ += n; return n; }
      stage_ = 3;
    }
  }
  return 0;
}

}  // namespace gwlog
