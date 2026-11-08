# Cynus machine castling ? 2026-11-08

## Live evidence
COM4 read without reset: machine waiting on e8-g8, physical Cynus online,
synchronized, robotReplay=0, halted=0, target has king g8 / rook f8.
User confirmed castling not executed and working castling from the app.
Raw capture: host TEMP/gateway-stall-20261108-135851.txt.

## Defect
Machine gateway converted all four changed king/rook squares into one
uniform LED frame. The production Cynus raw LED decoder expects one pair
of endpoints. The four contiguous short-castling fields create ten lit
corners; more than eight enters the phased-pattern fallback. Uniform
markers do not match that fallback. No move command is decoded.
Native test now compiles the actual extractMoveFromLCommand function from
src/cynus_board.cpp. Its board and transport dependencies are deterministic
doubles; plausibility uses the machine legal-move preview. With the old
four-field command the existing robot_castle test fails at real decoding.

## Correction and scope
Only machine-generated Cynus castling hints use king source/destination.
The unchanged driver decodes e1g1/e1c1/e8g8/e8c8 and the Cynus firmware
executes the full castle. Normal board LED rendering, standalone/cable
paths, frozen snapshots and the Cynus driver itself remain unchanged.
physicalExpected still contains the complete king/rook target. King-only
and rook-only reports cannot confirm it. Confirmed replay still sends
four ordered king/rook stages to the two hosts.

Chessnut Move retains its separate full 35-byte motor target. No conversion
to a king-only command. Added tests compile the production Move driver and
controller and verify all four castles, king/rook squares in decoded motor
packets, duplicate-target idempotence and rejection of partial sensor
confirmation. Physical hardware remains to be exercised by the user.

## Validation
- tools/test_machine_gateway.cmd: PASS, including real Cynus decoder for
  all generated Cynus commands, both colors and both castling sides.
- tools/test_virtual_operator.cmd: PASS, including physical castle and M5
  stale-field cases in the optional virtual-operator build.
- tools/test_chessnutmove.cmd: PASS, all four castling motor/sensor cases.
- platformio run -e esp32-c3-supermini: SUCCESS.
Existing vswhere discovery and conversion warnings remain.

Flashed after explicit user instruction to COM4, ESP32-C3,
MAC e0:72:a1:23:5e:e0. esptool verified all written segment hashes and
issued RTS hardware reset. RAM 79572 bytes, flash code 739122 bytes.
Firmware SHA256: d8c3a87a563334440a865a1f88b829dd3125ab55c9eb55aa66e8d4d97dd9f878.
