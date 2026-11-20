#pragma once
#include "movegen.h"
#include "opening_book_data.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace OpeningBook {
inline constexpr char startFen[] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
inline constexpr size_t dataBytes = sizeof(data) + sizeof(startFen);
static_assert(dataBytes < 10000, "Opening book exceeds the agreed 10 KB data budget");

inline std::vector<Move> moves(const Position& root) {
    const int64_t ply = int64_t(root.fullmoveNumber() - 1) * 2 + (root.sideToMove() == BLACK);
    if (ply < 0 || ply >= maxPly) return {};
    std::vector<Move> result;
    for (size_t offset = 0; offset < sizeof(data) / sizeof(data[0]);) {
        const size_t begin = offset;
        while (data[offset]) ++offset;
        const size_t length = offset++ - begin;
        if (size_t(ply) >= length) continue;
        Position pos; pos.setFen(startFen);
        for (int i = 0; i < ply; ++i) pos.applyMove(data[begin + i]);
        // Key includes side, castling and en passant; counters do not prevent
        // transposition recognition. Validate the selected move at lookup.
        if (pos.key() != root.key()) continue;
        const Move move = data[begin + size_t(ply)];
        if (std::find(result.begin(), result.end(), move) == result.end() &&
            find_legal_move(root, move_to_uci(move)) == move)
            result.push_back(move);
    }
    return result;
}

inline Move choose(const Position& root, double pick) {
    const auto options = moves(root);
    if (options.empty() || !std::isfinite(pick)) return 0;
    return options[std::min(options.size() - 1,
        size_t(std::clamp(pick, 0.0, 1.0) * options.size()))];
}
} // namespace OpeningBook
