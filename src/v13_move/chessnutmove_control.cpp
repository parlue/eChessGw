#include "chessnutmove_control.h"
#include <cstring>
#include <cstdlib>
#include <initializer_list>

// Private copy of the gateway's existing array-based chess rules. Keeping it
// here avoids changing PGN, virtual-operator or other board-driver state.
namespace {
constexpr char kStartBoard[64] =
    // a1..h1                  a2..h2                  a3..h3
    {'R','N','B','Q','K','B','N','R', 'P','P','P','P','P','P','P','P',
     '.','.','.','.','.','.','.','.', '.','.','.','.','.','.','.','.',
     '.','.','.','.','.','.','.','.', '.','.','.','.','.','.','.','.',
     'p','p','p','p','p','p','p','p', 'r','n','b','q','k','b','n','r'};

inline int squareFile(int sq) { return sq % 8; }
inline int squareRank(int sq) { return sq / 8; }  // 0-based (rank 1 == 0)
inline bool onBoard(int file, int rank) { return file >= 0 && file < 8 && rank >= 0 && rank < 8; }
inline int makeSquare(int file, int rank) { return rank * 8 + file; }
inline bool isWhitePiece(char p) { return p >= 'A' && p <= 'Z'; }
inline bool isBlackPiece(char p) { return p >= 'a' && p <= 'z'; }
inline bool isOwnPiece(char p, bool white) { return white ? isWhitePiece(p) : isBlackPiece(p); }
inline bool isEnemyPiece(char p, bool white) { return white ? isBlackPiece(p) : isWhitePiece(p); }
inline char toUpperPiece(char p) { return isBlackPiece(p) ? static_cast<char>(p - 32) : p; }

struct ChessMove {
  int from = -1;
  int to = -1;
  char promotion = 0;  // 'Q'/'R'/'B'/'N' (always uppercase), or 0
  bool isCastleK = false;
  bool isCastleQ = false;
  bool isEnPassant = false;
};

using GameState = ChessnutMovePosition;


void resetGameState(GameState& gs) {
  memcpy(gs.board, kStartBoard, sizeof(gs.board));
  gs.whiteToMove = true;
  gs.whiteCastleK = gs.whiteCastleQ = gs.blackCastleK = gs.blackCastleQ = true;
  gs.enPassantSquare = -1;
  gs.fullmoveNumber = 1;
}

// ---------------------------------------------------------------------------
// Attack detection and pseudo-legal move generation. Deliberately simple,
// array-based (no dynamic allocation) -- fine for an occasional per-move
// call, not a hot loop.
// ---------------------------------------------------------------------------

constexpr int kRookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
constexpr int kBishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
constexpr int kKnightOffsets[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
constexpr int kKingOffsets[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};

bool squareAttacked(const char board[64], int square, bool byWhite) {
  const int file = squareFile(square), rank = squareRank(square);

  // Pawns: attacked from behind the attacker's own forward direction.
  const int pawnRank = byWhite ? rank - 1 : rank + 1;
  const char pawnChar = byWhite ? 'P' : 'p';
  for (int df : {-1, 1}) {
    if (onBoard(file + df, pawnRank) && board[makeSquare(file + df, pawnRank)] == pawnChar) return true;
  }

  for (auto& o : kKnightOffsets) {
    if (onBoard(file + o[0], rank + o[1])) {
      const char p = board[makeSquare(file + o[0], rank + o[1])];
      if (p == (byWhite ? 'N' : 'n')) return true;
    }
  }

  for (auto& o : kKingOffsets) {
    if (onBoard(file + o[0], rank + o[1])) {
      const char p = board[makeSquare(file + o[0], rank + o[1])];
      if (p == (byWhite ? 'K' : 'k')) return true;
    }
  }

  for (auto& d : kRookDirs) {
    int f = file + d[0], r = rank + d[1];
    while (onBoard(f, r)) {
      const char p = board[makeSquare(f, r)];
      if (p != '.') {
        if (p == (byWhite ? 'R' : 'r') || p == (byWhite ? 'Q' : 'q')) return true;
        break;
      }
      f += d[0];
      r += d[1];
    }
  }

  for (auto& d : kBishopDirs) {
    int f = file + d[0], r = rank + d[1];
    while (onBoard(f, r)) {
      const char p = board[makeSquare(f, r)];
      if (p != '.') {
        if (p == (byWhite ? 'B' : 'b') || p == (byWhite ? 'Q' : 'q')) return true;
        break;
      }
      f += d[0];
      r += d[1];
    }
  }

  return false;
}

int findKing(const char board[64], bool white) {
  const char k = white ? 'K' : 'k';
  for (int sq = 0; sq < 64; ++sq) {
    if (board[sq] == k) return sq;
  }
  return -1;
}

// Appends pseudo-legal moves (not yet filtered for leaving own king in
// check) for the piece on `from` to out[], starting at *count.
void generatePseudoMovesFrom(const GameState& gs, int from, ChessMove* out, int maxOut, int* count) {
  const char piece = gs.board[from];
  if (piece == '.') return;
  const bool white = isWhitePiece(piece);
  const int file = squareFile(from), rank = squareRank(from);
  const char upper = toUpperPiece(piece);

  auto tryAdd = [&](int to, char promotion = 0) {
    if (*count >= maxOut) return;
    ChessMove m;
    m.from = from;
    m.to = to;
    m.promotion = promotion;
    out[(*count)++] = m;
  };

  if (upper == 'P') {
    const int dir = white ? 1 : -1;
    const int startRank = white ? 1 : 6;   // 0-based
    const int lastRank = white ? 7 : 0;    // 0-based rank reached by promotion
    // Single push.
    if (onBoard(file, rank + dir) && gs.board[makeSquare(file, rank + dir)] == '.') {
      if (rank + dir == lastRank) {
        for (char promo : {'Q', 'R', 'B', 'N'}) tryAdd(makeSquare(file, rank + dir), promo);
      } else {
        tryAdd(makeSquare(file, rank + dir));
      }
      // Double push, only possible if the single push square was empty.
      if (rank == startRank && gs.board[makeSquare(file, rank + 2 * dir)] == '.') {
        tryAdd(makeSquare(file, rank + 2 * dir));
      }
    }
    // Captures (including en passant).
    for (int df : {-1, 1}) {
      if (!onBoard(file + df, rank + dir)) continue;
      const int to = makeSquare(file + df, rank + dir);
      if (isEnemyPiece(gs.board[to], white)) {
        if (rank + dir == lastRank) {
          for (char promo : {'Q', 'R', 'B', 'N'}) tryAdd(to, promo);
        } else {
          tryAdd(to);
        }
      } else if (to == gs.enPassantSquare) {
        ChessMove m;
        m.from = from;
        m.to = to;
        m.isEnPassant = true;
        if (*count < maxOut) out[(*count)++] = m;
      }
    }
  } else if (upper == 'N') {
    for (auto& o : kKnightOffsets) {
      if (onBoard(file + o[0], rank + o[1])) {
        const int to = makeSquare(file + o[0], rank + o[1]);
        if (!isOwnPiece(gs.board[to], white)) tryAdd(to);
      }
    }
  } else if (upper == 'K') {
    for (auto& o : kKingOffsets) {
      if (onBoard(file + o[0], rank + o[1])) {
        const int to = makeSquare(file + o[0], rank + o[1]);
        if (!isOwnPiece(gs.board[to], white)) tryAdd(to);
      }
    }
    // Castling: rights + empty-between + not-through-check, all required.
    const int homeRank = white ? 0 : 7;
    if (rank == homeRank && file == 4) {
      const bool canK = white ? gs.whiteCastleK : gs.blackCastleK;
      const bool canQ = white ? gs.whiteCastleQ : gs.blackCastleQ;
      if (canK && gs.board[makeSquare(5, homeRank)] == '.' && gs.board[makeSquare(6, homeRank)] == '.' &&
          !squareAttacked(gs.board, makeSquare(4, homeRank), !white) &&
          !squareAttacked(gs.board, makeSquare(5, homeRank), !white) &&
          !squareAttacked(gs.board, makeSquare(6, homeRank), !white)) {
        ChessMove m;
        m.from = from;
        m.to = makeSquare(6, homeRank);
        m.isCastleK = true;
        if (*count < maxOut) out[(*count)++] = m;
      }
      if (canQ && gs.board[makeSquare(3, homeRank)] == '.' && gs.board[makeSquare(2, homeRank)] == '.' &&
          gs.board[makeSquare(1, homeRank)] == '.' &&
          !squareAttacked(gs.board, makeSquare(4, homeRank), !white) &&
          !squareAttacked(gs.board, makeSquare(3, homeRank), !white) &&
          !squareAttacked(gs.board, makeSquare(2, homeRank), !white)) {
        ChessMove m;
        m.from = from;
        m.to = makeSquare(2, homeRank);
        m.isCastleQ = true;
        if (*count < maxOut) out[(*count)++] = m;
      }
    }
  } else {
    const bool sliding = (upper == 'R' || upper == 'B' || upper == 'Q');
    if (!sliding) return;
    auto walk = [&](const int (*dirs)[2], int n) {
      for (int i = 0; i < n; ++i) {
        int f = file + dirs[i][0], r = rank + dirs[i][1];
        while (onBoard(f, r)) {
          const int to = makeSquare(f, r);
          if (isOwnPiece(gs.board[to], white)) break;
          tryAdd(to);
          if (gs.board[to] != '.') break;  // captured enemy piece, ray stops
          f += dirs[i][0];
          r += dirs[i][1];
        }
      }
    };
    if (upper == 'R' || upper == 'Q') walk(kRookDirs, 4);
    if (upper == 'B' || upper == 'Q') walk(kBishopDirs, 4);
  }
}

// Applies a move (already known pseudo-legal) to gs's board, updating
// castling rights and en-passant target. Does not touch whiteToMove or
// fullmoveNumber -- caller's responsibility, since inference/legality
// checks sometimes need to try a move without committing turn order yet.
void applyMoveToBoard(GameState& gs, const ChessMove& m) {
  const char piece = gs.board[m.from];
  const bool white = isWhitePiece(piece);
  gs.board[m.from] = '.';

  if (m.isEnPassant) {
    const int capturedSquare = makeSquare(squareFile(m.to), squareRank(m.from));
    gs.board[capturedSquare] = '.';
    gs.board[m.to] = piece;
  } else if (m.isCastleK || m.isCastleQ) {
    const int homeRank = squareRank(m.from);
    gs.board[m.to] = piece;
    if (m.isCastleK) {
      gs.board[makeSquare(7, homeRank)] = '.';
      gs.board[makeSquare(5, homeRank)] = white ? 'R' : 'r';
    } else {
      gs.board[makeSquare(0, homeRank)] = '.';
      gs.board[makeSquare(3, homeRank)] = white ? 'R' : 'r';
    }
  } else if (m.promotion != 0) {
    gs.board[m.to] = white ? m.promotion : static_cast<char>(m.promotion + 32);
  } else {
    gs.board[m.to] = piece;
  }

  gs.enPassantSquare = -1;
  if (toUpperPiece(piece) == 'P' && !m.isEnPassant && abs(squareRank(m.to) - squareRank(m.from)) == 2) {
    gs.enPassantSquare = makeSquare(squareFile(m.from), (squareRank(m.from) + squareRank(m.to)) / 2);
  }

  if (toUpperPiece(piece) == 'K') {
    if (white) { gs.whiteCastleK = gs.whiteCastleQ = false; }
    else { gs.blackCastleK = gs.blackCastleQ = false; }
  }
  auto clearRookRight = [&](int square) {
    if (square == makeSquare(0, 0)) gs.whiteCastleQ = false;
    else if (square == makeSquare(7, 0)) gs.whiteCastleK = false;
    else if (square == makeSquare(0, 7)) gs.blackCastleQ = false;
    else if (square == makeSquare(7, 7)) gs.blackCastleK = false;
  };
  clearRookRight(m.from);
  clearRookRight(m.to);  // covers a rook being captured on its home square
}

// Legal moves for the side to move: pseudo-legal, minus any that leave that
// side's own king in check.
int generateLegalMoves(const GameState& gs, ChessMove* out, int maxOut) {
  int pseudoCount = 0;
  ChessMove pseudo[128];
  for (int sq = 0; sq < 64; ++sq) {
    if (gs.board[sq] == '.' || !isOwnPiece(gs.board[sq], gs.whiteToMove)) continue;
    generatePseudoMovesFrom(gs, sq, pseudo, 128, &pseudoCount);
  }
  int count = 0;
  for (int i = 0; i < pseudoCount; ++i) {
    GameState trial = gs;
    applyMoveToBoard(trial, pseudo[i]);
    if (!squareAttacked(trial.board, findKing(trial.board, gs.whiteToMove), !gs.whiteToMove)) {
      if (count < maxOut) out[count++] = pseudo[i];
    }
  }
  return count;
}

bool inCheck(const GameState& gs, bool white) {
  const int kingSq = findKing(gs.board, white);
  return kingSq >= 0 && squareAttacked(gs.board, kingSq, !white);
}


constexpr char codes[] = ".qkbpnRPrBNQK";
int nibble(char p) {
  const char* v = std::strchr(codes, p);
  return v && p ? int(v-codes) : -1;
}
bool valid(const char board[64]) {
  int white = 0, black = 0;
  for (int i=0;i<64;++i) {
    if (nibble(board[i]) < 0) return false;
    white += board[i]=='K'; black += board[i]=='k';
    if ((i<8 || i>=56) && (board[i]=='P' || board[i]=='p')) return false;
  }
  return white==1 && black==1;
}
void finish(GameState& state, const ChessMove& move) {
  applyMoveToBoard(state, move);
  state.whiteToMove = !state.whiteToMove;
}
}
void ChessnutMoveControl::reset() {
  phase=Idle; haveGame=haveSensor=false; sentAt=0;
  lastMoveFields[0]=lastMoveFields[1]=0;
  memset(actual,0,64); memset(target,0,64);
  resetGameState(position);
}
bool ChessnutMoveControl::sensor(const char board[64]) {
  memcpy(actual,board,64); haveSensor=true;
  if (phase==Fault) return false;
  const bool completing=phase==Moving;
  if (phase==Moving) {
    if (memcmp(board,target,64)) return false;
    phase=Idle;
  }
  if (!memcmp(board,kStartBoard,64)) {
    resetGameState(position); haveGame=true; lastMoveFields[0]=lastMoveFields[1]=0;
    return true;
  }
  if (haveGame && memcmp(board,position.board,64)) {
    ChessMove moves[128]; const int n=generateLegalMoves(position,moves,128);
    for (int i=0;i<n;++i) {
      GameState next=position; finish(next,moves[i]);
      if (!memcmp(board,next.board,64)) {
        lastMoveFields[position.whiteToMove?0:1]=(uint64_t(1)<<moves[i].from)|(uint64_t(1)<<moves[i].to);
        position=next; return true;
      }
    }
    // Human lift/capture intermediate: preserve last complete game state.
    // Full-position setup is still accepted through begin(), independently.
  }
  if (completing) haveGame=false; // A setup target is not a guessed game continuation.
  return true;
}
bool ChessnutMoveControl::begin(const char board[64], uint32_t now) {
  if (phase!=Idle || !haveSensor || !valid(board) || !memcmp(board,actual,64)) return false;
  memcpy(target,board,64); sentAt=now; phase=Moving;
  return true;
}
bool ChessnutMoveControl::timeout(uint32_t now) {
  if (phase==Moving && uint32_t(now-sentAt)>=120000) { phase=Fault; return true; }
  return false;
}
bool ChessnutMoveControl::hintTarget(uint64_t fields, char board[64]) const {
  if (!fields || phase!=Idle || !haveGame || !haveSensor || memcmp(actual,position.board,64)) return false;
  ChessMove moves[128]; const int n=generateLegalMoves(position,moves,128);
  if (n>=128) return false; // Refuse a potentially truncated candidate set.
  int matches=0;
  for (int i=0;i<n;++i) {
    const ChessMove& m=moves[i];
    const uint64_t endpoints=(uint64_t(1)<<m.from)|(uint64_t(1)<<m.to);
    uint64_t allowed=endpoints;
    if (m.isEnPassant) allowed |= uint64_t(1)<<makeSquare(squareFile(m.to),squareRank(m.from));
    if ((squareFile(m.from)==squareFile(m.to) && abs(m.to-m.from)==16) ||
        (squareRank(m.from)==squareRank(m.to) && abs(m.to-m.from)==2))
      allowed |= uint64_t(1)<<((m.from+m.to)/2);
    if (m.isCastleK || m.isCastleQ) {
      const int base=squareRank(m.from)*8;
      allowed |= (uint64_t(1)<<(base+(m.isCastleK?7:0))) | (uint64_t(1)<<(base+(m.isCastleK?5:3)));
    }
    if ((fields & endpoints)!=endpoints || (fields & ~allowed)) continue;
    // The previous move's LED echo and an immediate return use the same
    // endpoints. Only an explicit full target can disambiguate that case.
    if (endpoints==lastMoveFields[position.whiteToMove?0:1]) return false;
    GameState next=position; finish(next,m);
    memcpy(board,next.board,64); ++matches;
  }
  return matches==1;
}
bool chessnutMoveEncodeTarget(const char board[64], uint8_t frame[35]) {
  if (!valid(board)) return false;
  frame[0]=0x42; frame[1]=0x21; frame[34]=1;
  for (int i=0;i<32;++i) frame[2+i]=uint8_t(nibble(board[63-2*i]) | (nibble(board[62-2*i])<<4));
  return true;
}
bool chessnutMoveDecodeTarget(const uint8_t frame[35], char board[64]) {
  if (frame[0]!=0x42 || frame[1]!=0x21 || frame[34]!=1) return false;
  for (int i=0;i<64;++i) {
    const int n=(frame[2+i/2]>>(4*(i%2)))&15;
    if (n>12) return false;
    board[63-i]=codes[n];
  }
  return valid(board);
}
void chessnutMoveEncodeLeds(uint64_t fields, uint8_t frame[34]) {
  memset(frame,0,34); frame[0]=0x43; frame[1]=0x20;
  for (int sq=0;sq<64;++sq) if (fields & (uint64_t(1)<<sq)) {
    const int raw=63-sq;
    frame[2+raw/2] |= uint8_t(2<<(4*(raw%2)));
  }
}
