#pragma once
#include <cstddef>
#include <cstdint>

// Move-only state. All arrays use a1..h8, independently of cable/wire order.
struct ChessnutMovePosition {
  char board[64] = {};
  bool whiteToMove = true;
  bool whiteCastleK = true, whiteCastleQ = true, blackCastleK = true, blackCastleQ = true;
  int enPassantSquare = -1;
  int fullmoveNumber = 1;
};
struct ChessnutMoveControl {
  enum Phase { Idle, Moving, Fault };
  Phase phase = Idle;
  ChessnutMovePosition position;
  bool haveGame = false, haveSensor = false;
  char actual[64] = {}, target[64] = {};
  uint32_t sentAt = 0;
  uint64_t lastMoveFields[2] = {}; // Echo ambiguity: keep each colour's last move.
  void reset();
  // False suppresses intermediate robot positions. Exact target confirms completion.
  bool sensor(const char board[64]);
  bool begin(const char board[64], uint32_t now);
  void writeFailed() { phase = Fault; }
  bool timeout(uint32_t now);
  // Only one legal continuation, from the last physically confirmed position.
  // Promotion endpoints alone remain ambiguous; no queen default.
  bool hintTarget(uint64_t fields, char board[64]) const;
};
bool chessnutMoveEncodeTarget(const char board[64], uint8_t frame[35]);
bool chessnutMoveDecodeTarget(const uint8_t frame[35], char board[64]);
void chessnutMoveEncodeLeds(uint64_t fields, uint8_t frame[34]);
