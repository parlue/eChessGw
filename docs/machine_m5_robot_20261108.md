# M5 one-shot commands with a robot board ? 2026-11-08

## Observation and limits
User reports: last robot move d5-e4, white bishop move to b4 displayed by M5,
M5 requests execution. Live USB read on COM4 preserved the running game.
Capture: regular S requests and notifications; no move history or physical
runtime state available in the flashed build. The precise cause of that
live stall is not proven. Capture saved in the host TEMP folder as
gateway-stall-20261108.txt. No reset, flash, motor command or invented
confirmation was sent during diagnosis.

## Reproduced defect and correction
A valid fresh M5 move could be merged with stale usable fields queued while
the robot was executing the previous move. Since M5 may send the move once,
no subsequent frame would clean up and recover the candidate. The existing
fresh-software-move replacement guard applied only without a physical board.
The guard now applies in both virtual and physical machine games, only when
the incoming complete mask independently resolves to exactly one legal move.
Robot execution and exact full-position confirmation remain mandatory.

Regression uses a constructed legal game, not a recovered live game:
1. d3 d5 2. Bd2 Nf6 3. e4 dxe4 4. Bb4.
During ...dxe4 the M5 echo supplies a partial e4 field. After robot confirmation
the fresh one-shot Bd2-b4 must replace the stale field, dispatch once eligible,
and commit only after the robot reports the complete target.
Before the fix cynus_stale_oneshot fails at the candidate assertion.
After the fix cynus_stale_oneshot and move_stale_oneshot pass.
Existing machine gateway and virtual operator suites pass (existing compiler
conversion warnings and vswhere discovery warning remain).

Periodic USB machine-state diagnostics now include physical mode too, with
board type, online/synchronized/start state, replay state and expected target.
This supplies evidence on the next physical stall without claiming an ACK
means mechanical execution.

## Starting position and board discovery
A recognized explicit new game resets the virtual-only selection to pending
machine selection if no physical board is online. Existing physical selection
is retained. Disconnect alone never unlocks a running virtual game.
Physical board search still runs only while no board is connected.
A disconnected board cannot communicate its physical starting position;
the gateway needs a known new starting position from the game/reset path.
Tests cover hard reset, soft new game, software starting-position report,
Cynus attachment and execution after reopen, and retaining a connected board.

Active build environment: esp32-c3-supermini. Frozen versions untouched.
Flashed after explicit user approval on 2026-11-08 to ESP32-C3 at COM4,
MAC e0:72:a1:23:5e:e0. All written segments hash-verified by esptool;
RTS hardware reset issued. Board gameplay validation remains with the user.
Firmware SHA256: 439b5443222f1d332645116ad469ae5215f5a17c0ad2f4a638b45f7f57e5ab15.
