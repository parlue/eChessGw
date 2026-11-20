// Virgin - UCI chess engine
// Basic types: colors, pieces, squares, moves, scores.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

using Bitboard = uint64_t;
using Key      = uint64_t;
using Move     = uint16_t;
using Score    = int;

// ---------------------------------------------------------------- colors
enum Color : int { WHITE = 0, BLACK = 1, COLOR_NB = 2 };
inline Color operator~(Color c) { return Color(int(c) ^ 1); }

// ---------------------------------------------------------------- pieces
enum PieceType : int {
    PAWN = 0, KNIGHT = 1, BISHOP = 2, ROOK = 3, QUEEN = 4, KING = 5,
    PIECE_TYPE_NB = 6, NO_PIECE_TYPE = 6
};

enum Piece : int { NO_PIECE = 12, PIECE_NB = 12 };

inline Piece make_piece(Color c, PieceType pt) { return Piece(int(c) * 6 + int(pt)); }
inline PieceType type_of(Piece pc) { return PieceType(int(pc) % 6); }
inline Color color_of(Piece pc) { return Color(int(pc) / 6); }
inline bool is_ok(Piece pc) { return int(pc) >= 0 && int(pc) < PIECE_NB; }

// ---------------------------------------------------------------- squares
// a1 = 0, b1 = 1, ... h8 = 63 (little endian rank file mapping)
enum Square : int {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE = 64, SQUARE_NB = 64
};

inline Square make_square(int f, int r) { return Square(r * 8 + f); }
inline int file_of(Square s) { return int(s) & 7; }
inline int rank_of(Square s) { return int(s) >> 3; }
inline bool is_ok(Square s) { return s != SQ_NONE && int(s) >= 0 && int(s) < 64; }
inline Square relative_square(Color c, Square s) { return c == WHITE ? s : Square(int(s) ^ 56); }
inline Square flip_rank(Square s) { return Square(int(s) ^ 56); }
inline Square flip_file(Square s) { return Square(int(s) ^ 7); }

inline char file_char(Square s) { return char('a' + file_of(s)); }
inline char rank_char(Square s) { return char('1' + rank_of(s)); }
inline std::string square_to_string(Square s) {
    if (!is_ok(s)) return "-";
    std::string r;
    r += file_char(s);
    r += rank_char(s);
    return r;
}

// ---------------------------------------------------------------- moves
// 16 bit encoding: from(6) | to(6) | flag(2) | promo(2)
enum MoveFlag : int { MF_NORMAL = 0, MF_PROMOTION = 1, MF_ENPASSANT = 2, MF_CASTLE = 3 };

inline Move make_move(Square from, Square to, MoveFlag f = MF_NORMAL, PieceType promo = KNIGHT) {
    const int promoIdx = (f == MF_PROMOTION) ? (int(promo) - int(KNIGHT)) : 0;
    return Move(int(from) | (int(to) << 6) | (int(f) << 12) | (promoIdx << 14));
}

inline Square    from_sq(Move m) { return Square(int(m) & 0x3F); }
inline Square    to_sq(Move m)   { return Square((int(m) >> 6) & 0x3F); }
inline MoveFlag  move_flag(Move m){ return MoveFlag((int(m) >> 12) & 3); }
inline PieceType promotion_type(Move m) { return PieceType(KNIGHT + ((int(m) >> 14) & 3)); }
inline bool      is_promotion(Move m) { return move_flag(m) == MF_PROMOTION; }

std::string move_to_uci(Move m);
bool parse_square(const std::string& s, Square& out);
Move parse_uci_move(const std::string& s, bool& ok);

// ---------------------------------------------------------------- scores
constexpr Score SCORE_ZERO       = 0;
constexpr Score SCORE_DRAW       = 0;
constexpr Score SCORE_INFINITE   = 32000;
constexpr Score SCORE_MATE       = 31000;
constexpr Score SCORE_MATE_BOUND = SCORE_MATE - 1000;
constexpr Score SCORE_NONE       = -32000;

inline bool is_mate_score(Score s) { return s > SCORE_MATE_BOUND || s < -SCORE_MATE_BOUND; }
