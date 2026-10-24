# Spectrum beta 2026-10-24

Target: ESP32-C3 SuperMini (`esp32-c3-supermini`). Not a frozen release.
Built successfully: flash 730462 bytes; static RAM 79444 bytes.
Five host test groups passed: Spectrum, Certabo protocol, King filter,
King echo and castling. Real Spectrum gateway testing remains pending.

Spectrum identification now follows eChessTactics chip-prefix detection, with
repeated 64-byte position notifications as fallback (not negotiated MTU).
Known mono-Certabo tags take priority over the packet fallback. Spectrum input
noise filtering, quiet calibration confirmation and reduced unchanged-frame
refresh follow the app. Normal Certabo and King/Phoenix cable protocols remain
separate. RGB payloads avoid the reserved FF55 sequence.

At every connection, learn all 34 pieces: normal starting position plus white
extra queen d3 and black extra queen d6. Remove extra queens after learning.
Spectrum stays dark during the 4.2-second confirmation phase. Only the correct
starting position unlocks play; archived IDs are never reused for calibration.

## Colour and brightness gestures

After calibration, use the normal starting position plus exactly one learned
extra white queen. Hold it for one second on the selected square:

| Square | Setting |
|---|---|
| g3 | Red |
| g4 | Blue |
| g5 | Green |
| g6 | Orange |
| h3 | 25% brightness |
| h4 | 50% brightness |
| h5 | 75% brightness |
| h6 | 100% brightness |

The four centre squares flash three times to confirm. Remove the queen.
Settings persist across reconnects. Default: red at 80% brightness.

Please test recognition, calibration, corner mapping, all-off in the starting
position, gestures, reconnect, King/Phoenix moves and mono-Certabo regression.
The stable v11 installer is unchanged. The previous beta remains archived.
A freeze will only follow tester confirmation.

Corresponding source: source.zip. Build: `pio run -e esp32-c3-supermini`.
Toolchain: espressif32 7.0.1, Arduino 2.0.17, NimBLE-Arduino 2.5.1.
SHA256.json lists the release payload hashes.
