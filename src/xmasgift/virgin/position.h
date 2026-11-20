// Virgin - UCI chess engine
// Board representation and move making. Uses copy-make (Position is a small
// trivially copyable value type) which keeps the engine bug-free first and is
// optimised to make/unmake later if profiling demands it.
#pragma once

#include "bitboard.h"

#include <iosfwd>
#include <string>

constexpr int CR_WK = 1, CR_WQ = 2, CR_BK = 4, CR_BQ = 8;

class Position {
public:
    Position();

    bool setFen(const std::string& fen, std::string* error = nullptr);
    std::string fen() const;

    // ------------------------------------------------------------ queries
    Color    sideToMove() const { return side; }
    Bitboard occupied() const { return occ; }
    Bitboard pieces() const { return occ; }
    Bitboard pieces(Color c) const { return byColor[c]; }
    Bitboard pieces(PieceType pt) const { return byType[pt]; }
    Bitboard pieces(Color c, PieceType pt) const { return byColor[c] & byType[pt]; }
    Piece    pieceOn(Square s) const { return board[s]; }
    Square   kingSquare(Color c) const { return lsb_square(pieces(c, KING)); }
    Square   epSquare() const { return ep; }
    int      castlingRights() const { return castling; }
    int      halfmoveClock() const { return halfmove; }
    int      fullmoveNumber() const { return fullmove; }
    Key      key() const { return zobrist; }
    Key      computeKey() const;

    bool isSquareAttacked(Square s, Color by) const;
    // All pieces of both colours that attack s, for the given occupancy.
    Bitboard attackersTo(Square s, Bitboard occ) const;
    bool inCheck() const { return isSquareAttacked(kingSquare(side), ~side); }
    bool givesCheck(Move m) const;

    bool isCapture(Move m) const;
    bool isEnPassant(Move m) const { return move_flag(m) == MF_ENPASSANT; }
    bool isCastle(Move m) const { return move_flag(m) == MF_CASTLE; }
    bool isLegal(Move m) const;

    // ------------------------------------------------------------ mutation
    void applyMove(Move m);       // unchecked, flips side to move
    void applyNullMove();

    // ------------------------------------------------------------ output
    void print(std::ostream& os) const;
    bool sanityCheck(std::string* msg = nullptr) const;

private:
    void putPiece(Piece pc, Square s);
    void removePiece(Square s);
    void movePiece(Square from, Square to);

    Bitboard byType[PIECE_TYPE_NB];
    Bitboard byColor[COLOR_NB];
    Bitboard occ;
    Piece    board[SQUARE_NB];
    Color    side;
    int      castling;
    Square   ep;
    int      halfmove;
    int      fullmove;
    Key      zobrist;
};

// Zobrist keys (exposed for the transposition table in a later milestone).
extern Key ZobristPsq[PIECE_NB][SQUARE_NB];
extern Key ZobristSide;
extern Key ZobristCastling[16];
extern Key ZobristEpFile[8];
void init_zobrist();

// Per-square mask applied to castling rights when a move touches that square.
extern int CastlingRightsMask[SQUARE_NB];
