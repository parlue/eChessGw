# Certabo / Spectrum beta: early setup assistance, 27 October 2026

ESP32-C3 SuperMini. Build marker: ble256-early-setup-20261027.

- Certabo: normal starting position + white extra queen d3, black d6.
- Spectrum: normal starting position + white extra queen e3, black e6.
- One extra chip on EITHER selection square, unique and stable for three full
  scans, selects the driver and enables setup LEDs. No writes before selection.
- Missing, additional or duplicate chips light after two real scans; a correction
  clears immediately. Polling cannot manufacture votes. Chip values/prefixes,
  packet sizes, MAC and L/D replies never select the model.
- Mixed d/e choice before selection stays dark. After selection the choice is
  locked until reconnect; opposite selection squares are errors, not a new model.
- All 34 unique IDs and the full correct layout are still required, with seven
  scan majority AFTER early selection. Swapped piece types cannot be detected
  during learning: placement defines the chip-to-piece assignment.
- After full learning: three centre flashes, remove both extra queens when
  their squares light. Play starts only at the correct 32-piece start position.
- Spectrum setup and confirmation: blue 80%. Play defaults to blue 80%; saved
  user gesture settings remain available.

Eight host test groups, eight exact V11 Mono transmission comparisons and the
SuperMini build passed. Flash 731784 bytes; static RAM 79084 bytes. Parser,
King/Phoenix cable code, MTU request256 and stable v11 remain unchanged.
Physical tests on both boards remain required. No freeze yet.

Matching source: source.zip. Checksums: SHA256.json.
Previous beta retained unchanged at ../spectrum-20261027-queen-layout/.
