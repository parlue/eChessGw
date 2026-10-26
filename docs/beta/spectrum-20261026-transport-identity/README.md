# Certabo / Spectrum beta: transport identity, 26 October 2026

ESP32-C3 SuperMini. Build marker: ble256-transport-id-20261026.

- Board type never depends on piece IDs; the same pieces work on both boards.
- Passive protocol/transport identification, with MTU request unchanged at 256.
- Standalone D indicates RGB. L alone is not exclusive to mono boards.
- Without D, require three complete positions and three original position-start
  notifications: exactly 64 bytes selects the observed Spectrum bridge profile;
  greater than 64 selects the observed classic Certabo bridge profile.
- Unknown or conflicting profiles disable LED output. These packet lengths are
  observed firmware fingerprints, not universal model identifiers. Hardware
  validation on both bridges remains necessary.
- Buffer up to three initial complete positions, replay them into calibration
  after identification, clear the buffer on malformed input or RX gaps.
- Remove the prefix-based Spectrum noise filter. Unknown/duplicate learned IDs
  withhold the complete position; they never become empty squares.
- Learn all 34 figures anew at each connection, then remove the extra queens.
- Spectrum 81 RGB LEDs, default red 80%, colour/brightness gestures retained.
- King/Phoenix cable protocol, MTU, connection sequence and stable v11 unchanged.

Five host test groups passed, including real-parser notification fragmentation,
identical figure IDs on both profiles, bounded replay, unknown/conflicting input,
reconnect, calibration, RGB mapping, gestures, King filtering/echo and castling.
SuperMini build passed: flash 731228 bytes; static RAM 80028 bytes.

This corrects ID-dependent selection after BLE subscription. It is NOT a confirmed
fix for failures before connection/subscription or a new freeze.
Matching source: source.zip. Payload checksums: SHA256.json.
Previous beta retained unchanged at ../spectrum-20261025-v11-compare256/.
