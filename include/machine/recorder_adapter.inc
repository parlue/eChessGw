namespace { GameState virtualGame; }
void machineReset() { resetGameState(virtualGame); }
bool machineWhiteToMove() { return virtualGame.whiteToMove; }
void machineBoard(char out[64]) { memcpy(out, virtualGame.board, 64); }

int machineCapturedSquare(int from, int to) {
  ChessMove moves[128];
  const int n = generateLegalMoves(virtualGame, moves, 128);
  for (int i = 0; i < n; ++i) if (moves[i].from == from && moves[i].to == to) {
    if (moves[i].isEnPassant) return makeSquare(squareFile(to), squareRank(from));
    return virtualGame.board[to] != '.' ? to : -1;
  }
  return -1;
}

bool machineCouldBeMoveFields(uint64_t fields) {
  if (!fields) return false;
  ChessMove moves[128];
  const int n = generateLegalMoves(virtualGame, moves, 128);
  for (int i = 0; i < n; ++i) {
    const uint64_t endpoints = (uint64_t(1) << moves[i].from) | (uint64_t(1) << moves[i].to);
    if ((fields & ~endpoints) == 0) return true;
  }
  return false;
}

bool machineCaptureRemoved(int from, int to, char out[64]) {
  const int captured = machineCapturedSquare(from, to);
  if (captured < 0) return false;
  memcpy(out, virtualGame.board, 64);
  out[from] = '.';
  out[captured] = '.';
  return true;
}

int machineFindMove(uint64_t squares, int& from, int& to) {
  ChessMove moves[128];
  const int n = generateLegalMoves(virtualGame, moves, 128);
  int matches = 0;
  for (int i = 0; i < n; ++i) {
    const auto& m = moves[i];
    const uint64_t endpoints = (uint64_t(1) << m.from) | (uint64_t(1) << m.to);
    uint64_t allowed = endpoints;
    if (m.isEnPassant) allowed |= uint64_t(1) << makeSquare(squareFile(m.to), squareRank(m.from));
    // Corner sharing can light the square between two endpoints too.
    if ((squareFile(m.from) == squareFile(m.to) && abs(m.to - m.from) == 16) ||
        (squareRank(m.from) == squareRank(m.to) && abs(m.to - m.from) == 2))
      allowed |= uint64_t(1) << ((m.from + m.to) / 2);
    if (m.isCastleK || m.isCastleQ) {
      const int base = squareRank(m.from) * 8;
      allowed |= uint64_t(1) << (base + (m.isCastleK ? 7 : 0));
      allowed |= uint64_t(1) << (base + (m.isCastleK ? 5 : 3));
    }
    if ((squares & endpoints) != endpoints || (squares & ~allowed)) continue;
    // Four promotion choices intentionally remain ambiguous: LED endpoints
    // alone cannot tell us which piece the engine selected.
    ++matches;
    from = m.from;
    to = m.to;
  }
  return matches;
}

bool machinePreview(int from, int to, char out[64]) {
  ChessMove moves[128];
  const int n = generateLegalMoves(virtualGame, moves, 128);
  for (int i = 0; i < n; ++i) if (moves[i].from == from && moves[i].to == to) {
    GameState next = virtualGame;
    applyMoveToBoard(next, moves[i]);
    memcpy(out, next.board, 64);
    return true;
  }
  return false;
}

void machineCommit(int from, int to) {
  ChessMove moves[128];
  const int n = generateLegalMoves(virtualGame, moves, 128);
  for (int i = 0; i < n; ++i) {
    if (moves[i].from != from || moves[i].to != to) continue;
    applyMoveToBoard(virtualGame, moves[i]);
    if (!virtualGame.whiteToMove) ++virtualGame.fullmoveNumber;
    virtualGame.whiteToMove = !virtualGame.whiteToMove;
    return;
  }
}

void machineSaveAndExport() {
  finalizeGame("*");
  usbPgnDumpTrigger();
}

void machineRecorderRestart() {
  // Only complete positions reach this recorder. Consume the last committed
  // position even if its normal settle timer has not yet expired.
  if (haveLatestFrame && !frameConsumed) {
    frameConsumed = true;
    if (!haveSettledFrame || memcmp(settledFrame, latestFrame, kModeBStatusFrameLength))
      processSettledBoard(latestFrame);
  }
  // Explicit abandonment also preserves short games in this experiment.
  if (pgnMovesLen) saveGameToLittleFs(pgnMoves, "*");
  haveLatestFrame = haveSettledFrame = false;
  frameConsumed = true;
  beginNewGame();
}
