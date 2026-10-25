# Certabo / Spectrum beta: V11 comparison, MTU 256

Target: ESP32-C3 SuperMini. Build 2026-10-25-v11-compare256.
This is a test build, not a confirmed fix for the reported Certabo connection failure.

- MTU request remains 256 for both board types.
- Original V11 application connection call restored; no added MTU185 or no-MTU retry.
- Spectrum settings are loaded only after an RGB protocol has been identified and a
  complete position received, instead of before every Certabo connection.
- Existing Spectrum recognition, LED encoder, noise filter and gestures retained.
- Logs include original advertisement data, RSSI, connectable flag, NimBLE version,
  connection interval and timeout, and connection/discovery/subscription errors.
- Log marker: ble256-v11compare-20261025.

SuperMini build passed: flash 731552 bytes; static RAM 79452 bytes.
Hardware confirmation remains pending. Test standard Certabo and Spectrum, and
export the new gateway log if connection still fails. MAC addresses are not used
to classify the board type. King/Phoenix cable protocol is unchanged.

Stable v11 and previous beta packages remain unchanged and available for rollback.
No new freeze. Matching source: source.zip; payload checksums: SHA256.json.
Build source with pio run -e esp32-c3-supermini.
