"""Compile the integrated runtime itself against native transport doubles."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = root / '.pio'
out.mkdir(exist_ok=True)
pgn = (root / 'src/pgn_recorder.cpp').read_text(encoding='utf-8')
adapter = (root / 'include/machine/recorder_adapter.inc').read_text(encoding='utf-8')
rules = pgn[pgn.index('constexpr char kStartBoard'):pgn.index('// Move inference:')]
adapter = adapter[:adapter.index('void machineSaveAndExport')]
(out / 'machine_gateway_rules.inc').write_text(rules + adapter, encoding='utf-8')
header = (root / 'include/board_driver.h').read_text(encoding='utf-8')
header = header.replace('#include <Arduino.h>', '#include <cstddef>').replace('#include <NimBLEDevice.h>', '')
driver = (root / 'src/board_driver.cpp').read_text(encoding='utf-8')
decoder = driver[driver.index('size_t decodeKingLedFrame'):driver.index('void dispatchLedFrameToBoard')]
(out / 'machine_gateway_protocol.inc').write_text(header + '\nuint8_t ledGrid[81] = {};\n' + decoder, encoding='utf-8')
runtime = (root / 'src/machine_gateway.cpp').read_text(encoding='utf-8')
runtime = runtime[runtime.index('namespace {'):runtime.rindex('#endif')]
runtime = runtime.replace('#include "machine/virtual_operator_runtime.inc"',
                          (root / 'include/machine/virtual_operator_runtime.inc').read_text(encoding='utf-8'))
runtime = runtime.replace('#include "machine/physical_promotion_runtime.inc"', (root / 'include/machine/physical_promotion_runtime.inc').read_text(encoding='utf-8'))
(out / 'machine_gateway_runtime.inc').write_text(runtime, encoding='utf-8')
server = (root / 'src/chesslink_server.cpp').read_text(encoding='utf-8')
led = server[server.index("    case 'L': {"):server.index('    default:', server.index("    case 'L': {"))]
(out / 'machine_gateway_ble_route.inc').write_text(
    'void serverLed(const uint8_t* frame, size_t length) { switch (frame[0]) {\n'
    + led + '\ndefault: break; } }\n', encoding='utf-8')
reset = server[server.index("    case 'T':"):server.index("    case 'R':", server.index("    case 'T':"))]
(out / 'machine_gateway_ble_reset.inc').write_text(
    'bool serverResetLeavesTransportUntouched() {\n'
    'int resets = 0; auto resetRegisters = [&]() { ++resets; };\n'
    'bool haveSentStatus = true;\n'
    'struct Refresh { int count = 0; void reset() { ++count; } } statusRefresh;\n'
    "switch ('T') {\n" + reset + '\ndefault: break; }\n'
    'return resets == 0 && haveSentStatus && statusRefresh.count == 0; }\n',
    encoding='utf-8')

# Exercise the production Cynus endpoint decoder with generated machine frames.
cynus = (root / 'src/cynus_board.cpp').read_text(encoding='utf-8')
start = cynus.index('bool extractMoveFromLCommand(std::string& uci) {')
end = cynus.index('// Only commits a move once', start)
(out / 'machine_cynus_decoder.inc').write_text(cynus[start:end], encoding='utf-8')

start = cynus.index('bool cynusExecuteMachinePromotion(int from, int to, char piece) {')
end = cynus.index('void cynusCancelPendingMachineMove()', start)
(out / 'machine_cynus_promotion.inc').write_text(cynus[start:end], encoding='utf-8')
