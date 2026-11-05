# Chessnut Air / GO / Pro – ChessLink BT LED Beta 1

5 November 2026. ESP32-C3 SuperMini. Not yet hardware-confirmed.

This beta targets Chessnut boards connected by Bluetooth to the gateway while the gateway advertises a ChessLink board to a chess app.

- Each complete app LED pattern replaces the preceding pattern; this route no longer uses the Phoenix two-square accumulator.
- Pawn double pushes use the current board position to remove a spurious middle square (for example c7-c5 must not become c6/c7).
- Castling preserves king and rook squares. Deferred commands retain their Bluetooth origin, including a later clear command.
- The cable dispatcher, cable coordinates and stable V12 files are retained. Other board types use their existing path.

310 automated normalization and actual-code dispatch cases passed. Firmware compiled successfully; no device has been flashed or tested by the assistant.

## Test

Connect Chessnut to the gateway and the app to its ChessLink advertisement. Check pawn double pushes on both sides, repeated identical hints, normal moves, captures, both castlings and clearing a completed hint. The app may legitimately leave only the target lit after the piece is lifted.

Look for `[CHESSNUT BT LED BETA1] decoded=3 resolved=2: c5 c7` (squares vary by move) in a new log.

Install using this beta's Connect button on the main webflasher page. Stable V12 and the separate Chessnut Move beta remain selectable. Target: ESP32-C3 only.

`source.zip` contains the isolated beta sources and tests; `changes.patch` shows the changes; `SHA256.json` records the published assets. Build environment: `esp32-c3-supermini_ChessnutBtLedBeta`.
