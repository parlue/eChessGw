# eChessGw - Chessnut Move BETA (2026-11-05)

Target: ESP32-C3 SuperMini. Select **Chessnut Move - BETA** in the web flasher.
The **Stable release - V12** installer remains available and unchanged.

This beta adds a dedicated Chessnut Move path using the LED and complete-position
motor protocol proven in eChessTactics. Move receives a complete target position
and confirms completion through its piece sensors. Human moves are not sent back
to the motors. Ordinary Chessnut Air/GO/Pro uses its separate existing driver.

## Regression test with an ordinary Chessnut board

1. Install this beta and reconnect the gateway to your usual chess computer/app.
2. Check board discovery, connection and the initial position.
3. Play normal moves with both colours, captures, castling and en passant.
4. Check move LEDs and that both sides show the same position.
5. Check promotion and starting a new game from the initial position.
6. Check reconnecting the board and resuming the usual connection workflow.

This is a beta for field testing. The firmware build, Move driver simulations and
95 gateway runtime scenarios passed. No Move hardware was connected during the
implementation. See [integration details](integration.md) for exact protocol
scope and limitations: LED endpoints alone do not identify a promotion piece or
disambiguate every delayed echo; ambiguous motor actions are refused. Complete
Move target positions carry the exact promotion figure.

`build.json` records the firmware identity and completed checks. `SHA256.json`
checks the published package files. No filesystem image is included in the
installer. The matching source snapshot is retained locally by the maintainer.
