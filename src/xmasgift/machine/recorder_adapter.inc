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

// Count distinct endpoint pairs; retain all promotion pieces as choices.
// No figure is selected by this operation.
static int machineFindVirtualIn(const GameState& position, uint64_t squares, int& from, int& to) {
  ChessMove moves[128];
  const int n = generateLegalMoves(position, moves, 128);
  int matches = 0, lastFrom = -1, lastTo = -1;
  for (int i = 0; i < n; ++i) {
    const ChessMove& m = moves[i];
    uint64_t endpoints = (uint64_t(1) << m.from) | (uint64_t(1) << m.to);
    uint64_t allowed = endpoints;
    if (m.isEnPassant) allowed |= uint64_t(1) << makeSquare(squareFile(m.to), squareRank(m.from));
    if ((squareFile(m.from) == squareFile(m.to) && abs(m.to - m.from) == 16) ||
        (squareRank(m.from) == squareRank(m.to) && abs(m.to - m.from) == 2))
      allowed |= uint64_t(1) << ((m.from + m.to) / 2);
    if (m.isCastleK || m.isCastleQ) {
      int base = squareRank(m.from) * 8;
      allowed |= uint64_t(1) << (base + (m.isCastleK ? 7 : 0));
      allowed |= uint64_t(1) << (base + (m.isCastleK ? 5 : 3));
    }
    if ((squares & endpoints) != endpoints || (squares & ~allowed)) continue;
    if (lastFrom != m.from || lastTo != m.to) { ++matches; lastFrom = m.from; lastTo = m.to; }
    from = m.from; to = m.to;
  }
  return matches;
}

int machineFindVirtualMove(uint64_t squares, int& from, int& to) {
  return machineFindVirtualIn(virtualGame, squares, from, to);
}

unsigned machineVirtualPlans(int from, int to, MachineVirtualPlan* plans, unsigned capacity) {
  ChessMove moves[128];
  const int n = generateLegalMoves(virtualGame, moves, 128);
  unsigned count = 0;
  // Deliberate enumeration of all pieces; none is accepted without feedback.
  for (char piece : {'\0', 'N', 'B', 'R', 'Q'}) for (int i = 0; i < n; ++i) {
    const auto& m = moves[i];
    if (m.from != from || m.to != to || m.promotion != piece || count == capacity) continue;
    MachineVirtualPlan& plan = plans[count++]; plan = MachineVirtualPlan{};
    plan.from = from; plan.to = to; plan.promotion = piece;
    GameState result = virtualGame; applyMoveToBoard(result, m);
    plan.enPassant = result.enPassantSquare;
    plan.castleFlags = (result.whiteCastleK ? 1 : 0) | (result.whiteCastleQ ? 2 : 0) |
                       (result.blackCastleK ? 4 : 0) | (result.blackCastleQ ? 8 : 0);
    if (m.isCastleK || m.isCastleQ) {
      plan.count = 4; plan.holdMs = 1000;
      const int rookFrom = squareRank(from) * 8 + (m.isCastleK ? 7 : 0);
      const int rookTo = squareRank(from) * 8 + (m.isCastleK ? 5 : 3);
      for (unsigned stage = 0; stage < 4; ++stage) {
        memcpy(plan.steps[stage], virtualGame.board, 64);
        plan.steps[stage][from] = '.';
        if (stage >= 1) plan.steps[stage][to] = virtualGame.board[from];
        if (stage >= 2) plan.steps[stage][rookFrom] = '.';
        if (stage >= 3) plan.steps[stage][rookTo] = virtualGame.board[rookFrom];
      }
    } else {
      memcpy(plan.steps[0], virtualGame.board, 64); plan.steps[0][from] = '.';
      const int captured = m.isEnPassant ? makeSquare(squareFile(to), squareRank(from)) :
          (virtualGame.board[to] != '.' ? to : -1);
      plan.count = captured >= 0 ? 3 : 2;
      if (captured >= 0) {
        memcpy(plan.steps[1], plan.steps[0], 64); plan.steps[1][captured] = '.';
      }
      memcpy(plan.steps[plan.count - 1], result.board, 64);
    }
  }
  return count;
}

int machineFindReply(const MachineVirtualPlan& plan, uint64_t fields, int& from, int& to) {
  GameState next = virtualGame;
  memcpy(next.board, plan.target(), 64); next.whiteToMove = !next.whiteToMove;
  next.enPassantSquare = plan.enPassant;
  next.whiteCastleK = (plan.castleFlags & 1) != 0; next.whiteCastleQ = (plan.castleFlags & 2) != 0;
  next.blackCastleK = (plan.castleFlags & 4) != 0; next.blackCastleQ = (plan.castleFlags & 8) != 0;
  return machineFindVirtualIn(next, fields, from, to);
}

bool machineCommitVirtual(const MachineVirtualPlan& plan) {
  ChessMove moves[128]; const int n = generateLegalMoves(virtualGame, moves, 128);
  for (int i = 0; i < n; ++i) if (moves[i].from == plan.from && moves[i].to == plan.to && moves[i].promotion == plan.promotion) {
    GameState next = virtualGame; applyMoveToBoard(next, moves[i]);
    if (memcmp(next.board, plan.target(), 64)) return false;
    if (!next.whiteToMove) ++next.fullmoveNumber;
    next.whiteToMove = !next.whiteToMove; virtualGame = next; return true;
  }
  return false;
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
