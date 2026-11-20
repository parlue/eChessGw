#pragma once
#include "board_driver.h"

// Dedicated Chessnut Move driver. Sensor codes, 34-byte LEDs (43 20), and
// 35-byte Normal target positions (42 21 ... 01) follow the hardware-tested
// eChessTactics SensorProtocol/CynusClient. Ordinary Chessnut is separate.
extern const char kChessnutMoveBoardName[];
extern const char kChessnutMoveBoardNameAlt[];
bool chessnutmoveConnect(const NimBLEAddress& address);
bool chessnutmoveIsConnected();
void chessnutmovePoll();
// Display only: never starts motors. Suppressed while moving or faulted.
void chessnutmoveSetHighlightedSquares(const SquareHighlight*, size_t);
void chessnutmoveShowLocalBoardDeviations();
bool chessnutmoveHasLocalDeviation();
void chessnutmoveDeferLedFrame(const uint8_t frame167[167]);
void chessnutmoveTrackSelectionQueen(int mode);
uint32_t chessnutmoveDroppedBoardPackets();
// Explicit full target, a1..h8. Supports both colours and exact promotion
// pieces. No command retry or timer-based success; sensors confirm all 64.
bool chessnutmoveSetTargetPosition(const char board[64]);
// Compatibility adapter for hosts that supply LED endpoints only. Executes
// only a unique legal continuation of a physically confirmed game position.
// Setup/error marks and ambiguous promotions remain display-only.
void chessnutmoveExecuteHighlightedMove(const SquareHighlight*, size_t);
bool chessnutmoveIsBusyMoving();
