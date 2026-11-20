// Memory constrained backend: constant flash geometry and ray/blocker attacks.
#include "bitboard.h"
#include <array>

Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
Bitboard KnightAttacks[SQUARE_NB];
Bitboard KingAttacks[SQUARE_NB];

namespace {
constexpr int DF[8] = {1, -1, 0, 0, 1, -1, 1, -1};
constexpr int DR[8] = {0, 0, 1, -1, 1, 1, -1, -1};
constexpr bool inside(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }
constexpr int sign(int n) { return (n > 0) - (n < 0); }
using Geometry = std::array<std::array<Bitboard, 64>, 64>;
using Rays = std::array<std::array<Bitboard, 8>, 64>;
constexpr Rays makeRays() {
    Rays rays{};
    for (int s = 0; s < 64; ++s) for (int d = 0; d < 8; ++d)
        for (int f = s % 8 + DF[d], r = s / 8 + DR[d]; inside(f, r); f += DF[d], r += DR[d])
            rays[s][d] |= Bitboard(1) << (r * 8 + f);
    return rays;
}
constexpr Geometry makeGeometry(bool wholeLine) {
    Geometry result{};
    for (int a = 0; a < 64; ++a) for (int b = 0; b < 64; ++b) {
        const int dx = b % 8 - a % 8, dy = b / 8 - a / 8;
        if (a == b || (dx && dy && dx != dy && dx != -dy)) continue;
        const int df = sign(dx), dr = sign(dy);
        if (wholeLine) {
            int f = a % 8, r = a / 8;
            while (inside(f - df, r - dr)) { f -= df; r -= dr; }
            for (; inside(f, r); f += df, r += dr) result[a][b] |= Bitboard(1) << (r * 8 + f);
        } else {
            int f = a % 8 + df, r = a / 8 + dr;
            for (; f != b % 8 || r != b / 8; f += df, r += dr)
                result[a][b] |= Bitboard(1) << (r * 8 + f);
        }
    }
    return result;
}
constexpr Rays Ray = makeRays();
Bitboard sliding(Square square, Bitboard occupied, int first, int last) {
    Bitboard result = 0;
    for (int d = first; d < last; ++d) {
        Bitboard ray = Ray[size_t(square)][size_t(d)];
        const Bitboard blockers = ray & occupied;
        if (blockers) {
            const bool positive = DR[d] * 8 + DF[d] > 0;
            const int blocker = positive ? lsb(blockers) : msb(blockers);
            ray ^= Ray[size_t(blocker)][size_t(d)];
        }
        result |= ray;
    }
    return result;
}
}
const std::array<std::array<Bitboard, 64>, 64> BetweenBB = makeGeometry(false);
const std::array<std::array<Bitboard, 64>, 64> LineBB = makeGeometry(true);
Bitboard rook_attacks(Square square, Bitboard occupied) { return sliding(square, occupied, 0, 4); }
Bitboard bishop_attacks(Square square, Bitboard occupied) { return sliding(square, occupied, 4, 8); }
void init_bitboards() {
    for (int s = 0; s < 64; ++s) {
        const Bitboard b = square_bb(s);
        PawnAttacks[WHITE][s] = shift_ne(b) | shift_nw(b);
        PawnAttacks[BLACK][s] = shift_se(b) | shift_sw(b);
        KnightAttacks[s] = ((b & ~FileABB) << 15) | ((b & ~FileHBB) << 17)
            | ((b & ~(FileABB | FileBBB)) << 6) | ((b & ~(FileGBB | FileHBB)) << 10)
            | ((b & ~FileABB) >> 17) | ((b & ~FileHBB) >> 15)
            | ((b & ~(FileABB | FileBBB)) >> 10) | ((b & ~(FileGBB | FileHBB)) >> 6);
        KingAttacks[s] = shift_north(b) | shift_south(b) | shift_east(b) | shift_west(b)
            | shift_ne(b) | shift_nw(b) | shift_se(b) | shift_sw(b);
    }
}
