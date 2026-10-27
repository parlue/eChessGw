# Certabo / Spectrum beta: explicit queen setup, 27 October 2026

ESP32-C3 SuperMini. Build marker: ble256-queen-layout-20261027.

## IMPORTANT: choose the board through the extra queens

- Normal Certabo: normal starting position + WHITE EXTRA QUEEN d3, BLACK d6.
- Spectrum: normal starting position + WHITE EXTRA QUEEN e3, BLACK e6.
- Keep all 34 figures still for seven complete scans. No LED commands are sent
  before a complete, unique 34-ID calibration succeeds. There are deliberately
  NO setup-difference LEDs during this initial learning stage.
- After learning, the four centre squares blink three times. Spectrum blinks
  BLUE at 80%. Remove the two extra queens from the chosen d/e squares; their
  LEDs remain lit until removal. Play starts only at the correct 32-piece setup.
- Mixed or incomplete setups do not select a driver. Packet size, L/D replies,
  MAC and figure-ID prefixes never select the driver. Reconnect learns anew.

Spectrum default play colour is blue 80%; existing explicitly saved gesture
settings remain usable. Startup confirmation is always blue 80%. Colour and
brightness gestures are unchanged. Certabo uses its original 8-byte mask;
Spectrum uses the separate 81-corner, 247-byte RGB driver.

MTU request remains 256. BLE connection/discovery/subscription, other boards,
King/Phoenix cable processing and stable v11 are unchanged.

Seven host test groups and the SuperMini build passed: flash 730524 bytes,
static RAM 79060 bytes. Tests cover both layouts with the same IDs, all payload
fragmentations, contradictory markers, missing/duplicate/mixed figures, blue80
confirmation, queen removal, archive isolation and reconnect, plus prior King,
castling, mapping, parser and calibration regression coverage.

This fixes a proven wrong LED-driver selection and a learning gate. It is NOT
proof that a radio-level connection failure before subscription is resolved.
Physical testing on both boards remains required. No freeze yet.

Matching source: source.zip. Checksums: SHA256.json.
Previous beta retained unchanged at ../spectrum-20261026-transport-identity/.
