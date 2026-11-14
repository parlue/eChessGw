# BETA: virtual-board promotion - 14 November 2026

ESP32-C3 SuperMini gateway, engine versus engine without a physical board.
Phoenix a7-a8 previously remained at `no-unique-move matches=4`: the four
legal promotion pieces prevented the move from starting in the standard build.

This beta starts the existing source-only figure negotiation for virtual
promotions. The moving engine must confirm the piece before it is shared
with the receiving engine. There is no automatic queen assumption.
Ordinary virtual moves retain their existing execution path. The existing
physical-board promotion handler and frozen V13 Move sources are unchanged.

## Verification and limits

- Standard gateway regression suite passed.
- 34 standard-build virtual-promotion cases passed: both colors and hosts,
  Q/R/B/N, captures and rejection. White includes a7-a8 at the board edge.
- Full VirtualOperator beta suite and physical-promotion suite passed.
- Firmware built with Espressif32 7.0.1 and flashed to the user's gateway;
  esptool verified its flash hash. Startup/BLE discovery was observed.
- Actual Phoenix promotion and physical-board regression on hardware still
  need user confirmation. Native tests use transport doubles.

Use the separate BETA installer. Stable V13 Move and the V12 fallback are
preserved. Source: source.zip; payload checksums: SHA256.json.
Firmware SHA-256: 0cb24a0d27115d4c4c0e64fad1e95d5f2742806f9a81156de34beb6f7187d12c
