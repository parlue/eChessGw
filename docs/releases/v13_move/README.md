# V13 Move — 8 November 2026

ESP32-C3 SuperMini release, frozen as `src/v13_move/`.

- Dedicated Chessnut Move controller: full target positions and exact sensor confirmation.
- Physical engine duels: Cynus castling and en-passant commands use the moving piece endpoints; the complete board is still checked.
- Promotion choice is checked with the moving host through normal ChessLink position/LED feedback before a robot is commanded. No trial pieces are moved on the real board or sent to the other engine.
- Manya Cynus receives the exact promotion command (for example `move a7a8q`); Move receives the full confirmed target.
- A recognized new starting position reopens board discovery when no board is connected. An existing physical-board selection is preserved.
- One-shot M5 commands replace stale LED fields on physical and virtual routes.
- Existing Certabo/Spectrum and Chessnut Bluetooth LED behavior is included.

## Build and source

Build the freeze with `pio run -e esp32-c3-superminiV13_Move`.
The frozen sources include their complete project-header dependency closure.
Espressif32 7.0.1, Arduino framework 3.20017.241212+sha.dcc1105b,
NimBLE-Arduino 2.5.1 and the relevant compiler/flasher versions are pinned.
Development continues in `esp32-c3-supermini`, which excludes the freeze.

[Immutable source](https://github.com/parlue/eChessGw/tree/v13_move/src/v13_move)
| [Release](https://github.com/parlue/eChessGw/releases/tag/v13_move)
| [Web flasher](https://parlue.github.io/eChessGw/)

## Evidence and limits

The published firmware is the exact 798640-byte image flashed during the live test session.
Cynus castling was then confirmed by the user in a real duel. Four native test suites passed,
including 114 physical promotion scenarios, both colors, Q/R/B/N, captures, rejection,
blink/silence/stale feedback, connection loss and write failures. The actual Cynus command
and actual Move driver are compiled into the relevant tests against deterministic transport doubles.
A separate project with no active include directory successfully rebuilt the frozen closure.

This does not claim all new robot behavior has been hardware verified: a complete Move
hardware run and the new promotion negotiation still need real-board confirmation. Feedback
confirmation uses fresh LED-off/X plus continuing host activity and a stable timing interval;
silence alone is not confirmation. It adds no new protocol command.

Published bytes are kept unchanged. Rebuilding may differ because of embedded timestamps
and source paths. `FREEZE.json` records source provenance; `SHA256.json` covers these release assets.

V12 remains available as the previous release and web-flasher fallback. Earlier beta directories
are historical archives. No device is flashed by publishing this release.
