#pragma once

#include "board_driver.h"

// Enabled by the normal esp32-c3-supermini environment (build_flags
// -D CHESSLINK_ENABLE_CERTABO=1) and by esp32-c3-supermini_CertaboBLE; the
// frozen snapshot environments exclude certabo_board.cpp via build_src_filter.
extern const char kCertaboBoardName[];
bool certaboConnect(const NimBLEAddress& address);
bool certaboIsConnected();
void certaboPoll();
void certaboSetKingMode(bool enabled);
void certaboSetHighlightedSquares(const SquareHighlight* squares, size_t count);
void certaboClearLeds();
void certaboSetMoveLights(const SquareHighlight* squares, size_t count);

// Direct display for a cable host that sends each LED suggestion once and never
// repeats it (a real King -- see board_driver.h's dispatchCableLedFrameToBoard()).
// Unlike certaboSetMoveLights(), nothing here depends on the host re-sending the
// same squares, and the whole decoded frame replaces the mask instead of being
// filtered through the Phoenix accumulator. Only MoveLights' completion
// bookkeeping (position() -> observe()) and its pure-echo filter are reused, so
// a finished move still clears and a host replay of the human's own move cannot
// re-light it.
void certaboSetOneShotLedSquares(const SquareHighlight* squares, size_t count);
