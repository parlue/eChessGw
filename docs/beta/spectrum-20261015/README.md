# Spectrum beta 2026-10-15

Target: ESP32-C3 SuperMini, PlatformIO environment `esp32-c3-supermini`.
Built from the current development workspace; this is not the frozen v11 environment.
Build successful: flash 727508 bytes; static RAM 79036 bytes.

Spectrum: 81 RGB LEDs, FF55 / CRLF framing, red (204,0,0).
Passive L/D identification; no LED writes when identity is unknown or conflicting.
Real Spectrum hardware validation remains pending. In particular, startup depends
on an independently emitted D marker; the specification only documents D as a reply.

Test: calibration with 34 pieces; remove extra queens on d3/d6; verify corner mapping,
all-off, King/Phoenix moves, and reconnect. Report serial logs if LEDs stay off.
The stable v11 manifest and binaries are unchanged and remain available for rollback.

Corresponding source: source.zip (including platformio.ini and Spectrum test).
Build with `pio run -e esp32-c3-supermini`.
Toolchain used: espressif32 7.0.1, Arduino 2.0.17, NimBLE-Arduino 2.5.1,
riscv32-esp GCC 8.4.0+2021r2-patch5. Dependencies are obtained by PlatformIO.
SHA256.json records the release payload hashes.
