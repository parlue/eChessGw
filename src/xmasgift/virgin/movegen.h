// Virgin - UCI chess engine
// Move generation and perft.
#pragma once

#include "position.h"

#include <array>
#include <iosfwd>

struct MoveList {
    std::array<Move, 256> moves{};
    int count = 0;

    void add(Move m) { moves[size_t(count++)] = m; }
    Move operator[](int i) const { return moves[size_t(i)]; }
    size_t size() const { return size_t(count); }
    Move* begin() { return moves.data(); }
    Move* end() { return moves.data() + count; }
    const Move* begin() const { return moves.data(); }
    const Move* end() const { return moves.data() + count; }
};

// Pseudo legal moves: everything except leaving the own king in check.
void generatePseudoLegal(const Position& pos, MoveList& list, bool tacticalOnly = false);

// Fully legal moves (pseudo legal moves filtered by a king safety test).
int generateLegal(const Position& pos, MoveList& list);

bool hasLegalMove(const Position& pos);

// Resolves UCI move text ("e2e4", "e7e8q", "e1g1") against the legal move list
// of the position. The move generator knows the real flags, so castling and en
// passant get their MF_CASTLE / MF_ENPASSANT bits - a move built by
// parse_uci_move() alone would only be a king or pawn step and would leave the
// board desynchronised. Returns 0 if the text is not legal in this position.
Move find_legal_move(const Position& pos, const std::string& text);

// Static exchange evaluation: same-square material gain >= threshold.
// Includes quiet moves, EP, promotions and legal recaptures; not a tactical proof.
bool seeGE(const Position& pos, Move m, int threshold);

uint64_t perft(const Position& pos, int depth);
uint64_t perftDivide(const Position& pos, int depth, std::ostream& os);
