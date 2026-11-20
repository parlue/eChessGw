// Virgin - UCI chess engine
// Bitboard constants, attacks and population count helpers.
#pragma once

#include "types.h"
#include <array>

#if defined(_MSC_VER)
#  include <intrin.h>
#endif

constexpr Bitboard FileABB = 0x0101010101010101ULL;
constexpr Bitboard FileBBB = FileABB << 1;
constexpr Bitboard FileCBB = FileABB << 2;
constexpr Bitboard FileDBB = FileABB << 3;
constexpr Bitboard FileEBB = FileABB << 4;
constexpr Bitboard FileFBB = FileABB << 5;
constexpr Bitboard FileGBB = FileABB << 6;
constexpr Bitboard FileHBB = FileABB << 7;

constexpr Bitboard Rank1BB = 0xFFULL;
constexpr Bitboard Rank2BB = Rank1BB << 8;
constexpr Bitboard Rank3BB = Rank1BB << 16;
constexpr Bitboard Rank4BB = Rank1BB << 24;
constexpr Bitboard Rank5BB = Rank1BB << 32;
constexpr Bitboard Rank6BB = Rank1BB << 40;
constexpr Bitboard Rank7BB = Rank1BB << 48;
constexpr Bitboard Rank8BB = Rank1BB << 56;

constexpr Bitboard DarkSquares  = 0xAA55AA55AA55AA55ULL;
constexpr Bitboard LightSquares = ~DarkSquares;

extern Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
extern Bitboard KnightAttacks[SQUARE_NB];
extern Bitboard KingAttacks[SQUARE_NB];
#ifdef VIRGIN_EMBEDDED
extern const std::array<std::array<Bitboard, 64>, 64> BetweenBB;
extern const std::array<std::array<Bitboard, 64>, 64> LineBB;
#else
extern Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
extern Bitboard LineBB[SQUARE_NB][SQUARE_NB];
#endif

// ------------------------------------------------------------ single bits
inline Bitboard square_bb(Square s) { return 1ULL << int(s); }
inline Bitboard square_bb(int s) { return 1ULL << s; }

inline Bitboard file_bb(Square s) { return FileABB << file_of(s); }
inline Bitboard file_bb(int f) { return FileABB << f; }
inline Bitboard rank_bb(Square s) { return Rank1BB << (8 * rank_of(s)); }
inline Bitboard rank_bb(int r) { return Rank1BB << (8 * r); }

inline Bitboard shift_north(Bitboard b) { return b << 8; }
inline Bitboard shift_south(Bitboard b) { return b >> 8; }
inline Bitboard shift_east(Bitboard b) { return (b & ~FileHBB) << 1; }
inline Bitboard shift_west(Bitboard b) { return (b & ~FileABB) >> 1; }
inline Bitboard shift_ne(Bitboard b) { return (b & ~FileHBB) << 9; }
inline Bitboard shift_nw(Bitboard b) { return (b & ~FileABB) << 7; }
inline Bitboard shift_se(Bitboard b) { return (b & ~FileHBB) >> 7; }
inline Bitboard shift_sw(Bitboard b) { return (b & ~FileABB) >> 9; }

// ------------------------------------------------------------ popcount
inline int popcount(Bitboard b) {
#if defined(_MSC_VER)
    return int(__popcnt64(b));
#else
    return __builtin_popcountll(b);
#endif
}

inline int lsb(Bitboard b) {
#if defined(_MSC_VER)
    unsigned long i;
    _BitScanForward64(&i, b);
    return int(i);
#else
    return __builtin_ctzll(b);
#endif
}

inline int msb(Bitboard b) {
#if defined(_MSC_VER)
    unsigned long i;
    _BitScanReverse64(&i, b);
    return int(i);
#else
    return 63 - __builtin_clzll(b);
#endif
}

inline Square lsb_square(Bitboard b) { return Square(lsb(b)); }
inline Square msb_square(Bitboard b) { return Square(msb(b)); }
inline Square pop_lsb(Bitboard& b) {
    const Square s = lsb_square(b);
    b &= b - 1;
    return s;
}

inline int distance(Square a, Square b) {
    const int fd = file_of(a) - file_of(b);
    const int rd = rank_of(a) - rank_of(b);
    return (fd < 0 ? -fd : fd) > (rd < 0 ? -rd : rd) ? (fd < 0 ? -fd : fd) : (rd < 0 ? -rd : rd);
}

// Reverses the rank order of a bitboard (rank 1 <-> rank 8, files unchanged).
inline Bitboard flip_rank_bb(Bitboard b) {
#if defined(_MSC_VER)
    return _byteswap_uint64(b);
#else
    return __builtin_bswap64(b);
#endif
}

inline int file_distance(Square a, Square b) {
    const int d = file_of(a) - file_of(b);
    return d < 0 ? -d : d;
}
inline int rank_distance(Square a, Square b) {
    const int d = rank_of(a) - rank_of(b);
    return d < 0 ? -d : d;
}

// ------------------------------------------------------------ attacks
inline Bitboard pawn_attacks(Color c, Square s) { return PawnAttacks[c][s]; }
inline Bitboard knight_attacks(Square s) { return KnightAttacks[s]; }
inline Bitboard king_attacks(Square s) { return KingAttacks[s]; }

Bitboard bishop_attacks(Square s, Bitboard occ);
Bitboard rook_attacks(Square s, Bitboard occ);
inline Bitboard queen_attacks(Square s, Bitboard occ) {
    return bishop_attacks(s, occ) | rook_attacks(s, occ);
}

inline Bitboard attacks_bb(PieceType pt, Square s, Bitboard occ) {
    switch (pt) {
    case PAWN:   return PawnAttacks[WHITE][s] | PawnAttacks[BLACK][s];
    case KNIGHT: return KnightAttacks[s];
    case BISHOP: return bishop_attacks(s, occ);
    case ROOK:   return rook_attacks(s, occ);
    case QUEEN:  return queen_attacks(s, occ);
    default:     return KingAttacks[s];
    }
}

void init_bitboards();
