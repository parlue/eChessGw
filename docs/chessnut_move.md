# Chessnut Move integration

Updated 2026-11-05. Active environment: `esp32-c3-supermini`.

## Scope and isolation

Only `BoardType::ChessnutMove` uses the new controller. Air/GO/Pro, Millennium,
Certabo, iChessOne, Cynus and virtual-only engine duels retain their existing
routes. Frozen firmware snapshots are not changed. There is no global BLE MTU
or timing change. Names containing `CHESSNUT-MOVE`, `chessnut move` or
`CHESSNUT_MOVE` match the Move driver before generic Chessnut matching.

Protocol reference: the hardware-tested eChessTactics app in
`Documents/CynusTactics`, specifically `SensorProtocol.kt`, `MoveBoard.kt` and
`CynusClient.kt`. This firmware change has been built and checked with simulated
transports; it has not been flashed or exercised on a Move during this change.

## Separate display, target and observation

- Display LEDs: 34 bytes, `43 20`, two four-bit squares per byte; lit nibble `2`.
  Readiness/setup/result displays do not start motors.
- Explicit target: 35 bytes, `42 21`, 32 packed piece bytes, Normal mode `01`.
  Internal arrays use a1 through h8; wire slots run h8 through a1, low nibble
  first. Piece table: `.qkbpnRPrBNQK`.
- Sensor observation: the normal Chessnut `01 24` position packet. Every nibble
  is validated before accepting a snapshot.

`chessnutmoveSetTargetPosition()` accepts an entire target, including the exact
promotion piece. It first writes difference LEDs, waits for the acknowledged
write and a 10 ms separation, then sends one acknowledged motor command.
Motor commands require negotiated MTU >=38; LEDs require >=37. No packet is
silently split into unrelated motor commands.

A matching request during movement is idempotent. Other targets and display
updates are refused during movement. Intermediate robot positions do not reach
connected engines; completion requires all 64 sensed squares to match the
expected target. Human movement remains sensor-driven and is never commanded
back to the robot merely because it was observed.

There is no four-second success timer. After 120 seconds without completion,
or any failed LED/motor write, further commands are locked until reconnection.
There is no automatic motor retry. Disconnect/reconnect discards targets,
hints, notification queues and the LED cache, and starts with a fresh scan.

## Gateway routes

1. Physical-board engine duels send the existing `physicalExpected` full target
   directly to Move. They do not turn it back into source/destination LEDs.
   After physical confirmation, the existing staged robot replay presents
   lift/capture/placement steps to both engines. Cynus keeps its own path.
2. A Chessnut-compatible client can send a complete Move Normal target using
   `42 21 ... 01`, or display-only Move LEDs using `43 20 ...`. These commands
   are handled only when the physical board type is Move. The gateway does not
   change its advertised identity or add a new target command to ChessLink.
3. Legacy ChessLink/Chessnut LED hints have a Move-only compatibility adapter:
   from a confirmed game started in the initial position, it executes only a
   unique legal continuation. Short separately blinking endpoint frames can
   be combined. Reset/error clusters do not select an arbitrary source.

LED endpoints alone cannot specify a promotion piece or distinguish a delayed
move echo from an immediate return along the same endpoints. Those cases stay
**display-only**; no queen or reverse move is guessed. Use explicit full targets
for reliable promotion, setup and PGN replay (including both colours). The
existing physical-duel move resolver still cannot infer an exact promotion from
LED endpoints alone; this change does not claim to add that protocol information.
After an arbitrary setup, full targets continue to work; the compatibility game
tracker is re-established by a sensed standard initial position.

## Files and checks

The driver is `src/chessnutmove_board.cpp`; the portable Move-only state machine,
packet codec and private copy of existing gateway chess rules are in
`src/chessnutmove_control.cpp` and `include/chessnutmove_control.h`. No PGN/rules
state in other modes is reused or modified by that private controller.

On the development machine:

```bat
tools\test_chessnutmove.cmd
tools\test_machine_gateway.cmd
tools\test_virtual_operator.cmd
platformio run -e esp32-c3-supermini
```

The Move test compiles the actual driver against deterministic BLE/queue doubles.
It checks wire order, every LED square, separate blink endpoints, exact target
confirmation, duplicate and echo suppression, human moves, both robot colours,
captures, en passant, castling, all four promotion figures in both colours,
initial-position setup, malformed sensors, MTU refusal, failed writes, timeout
and reconnect. The existing runtime suites include an additional Move physical
robot/castling scenario and retain all previous scenarios.

Build output: `.pio/build/esp32-c3-supermini/firmware.bin`. Building does not flash.
