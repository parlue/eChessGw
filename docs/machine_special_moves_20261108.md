# Special moves in physical engine duels ? 2026-11-08

## En passant: reproduced and corrected
The machine gateway encoded every changed square as a Cynus LED suggestion.
After the castling correction this still included the third, captured-pawn
square for en passant. A new robot_ep test failed inside the real production
Cynus extractMoveFromLCommand decoder. The generated frame now always uses
only source/destination for Cynus. The robot firmware executes the special
move, while physicalExpected still requires the full resulting board,
including removal of the captured pawn. Other boards keep their LED display
fields. Chessnut Move still receives its full binary target without change.

Native gateway tests now exercise both colors and both diagonal EP directions,
Cynus actual endpoint decoder, full Move target dispatch, and rejection of
partial physical states (pawn moved but victim still present, or victim
removed without the pawn moving). Normal captures for both colors are also
checked through the same real Cynus decoder. Existing castling, replay,
queued-M5, disconnect and virtual cases continue to pass.

## Move production driver
Expanded tests compile the production board driver and controller against BLE
and sensor doubles. All four en-passant orientations inspect the actual
35-byte target packet, verify mover and victim squares, reject partial sensor
confirmation, and require exact target before advancing the turn. Duplicate
target suggestions produce no extra motor commands. All four castles remain
covered too.

## Promotions: important boundary
Move full-position motor targets preserve Q/R/B/N for both colors, with and
without capture: 16 tests. Diagonal capture source squares are used. An
unpromoted last-rank pawn cannot confirm the target; only the exact promoted
piece does, preserving a legal game continuation and the correct next turn.

The currently active physical engine-duel LED interface does NOT transmit
promotion choice. machineFindMove deliberately finds four candidates. Tests
robot_promotion_ambiguous and move_robot_promotion_ambiguous prove no robot
command or guessed promotion is issued. This means promotions cannot yet
complete automatically on this physical LED duel route. The full-target
Move API supports them separately. The optional virtual-operator path has
its own promotion-inference tests; it is not the flashed physical route.
No implicit queen choice has been added.

## Validation and deployment
All three suites pass:
- tools/test_machine_gateway.cmd
- tools/test_virtual_operator.cmd
- tools/test_chessnutmove.cmd
Existing MSVC narrowing warnings and vswhere discovery warning remain.
Production environment: esp32-c3-supermini.

NOT FLASHED: user explicitly postponed flashing and is continuing the current
game. The gateway still runs the previously flashed castling fix. New EP
correction is local only; no USB reset or motor command was issued in this
verification task. See cynus_machine_castling_20261108.md for that last flash.
Backups: backups/cynus-enpassant-20261108.

## Superseding update
Physical promotion confirmation is now implemented and flashed with the EP fix.
See machine_physical_promotion_20261108.md. The limitations/deployment status
above describe the earlier verification stage, not the current firmware.
